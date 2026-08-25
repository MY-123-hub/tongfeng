#include "modbus_async.h"

#include <string.h>

#include "usart.h"
#include "vfd_modbus_codec.h"

VfdModbusDiagnostics VfdModbusDiag;

static VfdJob g_active_job;
static VfdResult g_result;
static uint8_t g_tx_frame[VFD_MODBUS_REQUEST_SIZE];
static volatile uint8_t g_rx_frame[VFD_MODBUS_RX_CAPACITY];
static volatile uint8_t g_rx_length;
static volatile uint8_t g_rx_overflow;
static volatile uint8_t g_tx_complete;
static volatile uint8_t g_uart_error;
static volatile uint32_t g_last_rx_tick;
static volatile uint32_t g_tx_complete_tick;
static uint32_t g_tx_start_tick;
static uint8_t g_attempts;
static volatile uint8_t g_state;

static void VfdModbus_ResetAttemptFlags(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_rx_length = 0U;
    g_rx_overflow = 0U;
    g_tx_complete = 0U;
    g_uart_error = 0U;
    g_last_rx_tick = 0U;
    g_tx_complete_tick = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static void VfdModbus_Finish(uint8_t code, uint8_t exception_code)
{
    memset(&g_result, 0, sizeof(g_result));
    g_result.flow_id = g_active_job.flow_id;
    g_result.frequency_x100 = g_active_job.frequency_x100;
    g_result.action = g_active_job.action;
    g_result.epoch = g_active_job.epoch;
    g_result.request_type = g_active_job.request_type;
    g_result.origin = g_active_job.origin;
    g_result.code = code;
    g_result.exception_code = exception_code;
    g_result.attempts = g_attempts;
    g_state = VFD_MODBUS_RESULT_PENDING;
    VfdModbusDiag.state = g_state;
}

static void VfdModbus_StartAttempt(uint32_t now_ms)
{
    VfdModbus_ResetAttemptFlags();
    g_attempts++;
    g_tx_start_tick = now_ms;
    g_state = VFD_MODBUS_TX_ACTIVE;
    VfdModbusDiag.state = g_state;
    if (HAL_UART_Transmit_IT(&huart3,
                             g_tx_frame,
                             VFD_MODBUS_REQUEST_SIZE) != HAL_OK)
    {
        VfdModbus_Finish(VFD_RESULT_TX_ERROR, 0U);
    }
}

void VfdModbus_Init(void)
{
    memset(&VfdModbusDiag, 0, sizeof(VfdModbusDiag));
    memset(&g_active_job, 0, sizeof(g_active_job));
    memset(&g_result, 0, sizeof(g_result));
    VfdModbus_ResetAttemptFlags();
    g_attempts = 0U;
    g_state = VFD_MODBUS_IDLE;
    VfdModbusDiag.state = g_state;
}

VfdModbusStartStatus VfdModbus_Start(const VfdJob *job, uint32_t now_ms)
{
    VfdCodecStatus codec_status;

    if (job == NULL)
    {
        return VFD_MODBUS_START_INVALID;
    }
    if (g_state != VFD_MODBUS_IDLE)
    {
        VfdModbusDiag.busy_count++;
        return VFD_MODBUS_START_BUSY;
    }

    codec_status = VfdCodec_BuildWriteCommand(1U,
                                               job->action,
                                               job->frequency_x100,
                                               g_tx_frame,
                                               sizeof(g_tx_frame));
    if (codec_status != VFD_CODEC_OK)
    {
        return VFD_MODBUS_START_INVALID;
    }

    g_active_job = *job;
    g_attempts = 0U;
    VfdModbusDiag.start_count++;
    VfdModbus_StartAttempt(now_ms);
    return VFD_MODBUS_START_ACCEPTED;
}

void VfdModbus_Process(uint32_t now_ms, uint32_t current_epoch)
{
    uint8_t frame[VFD_MODBUS_RX_CAPACITY];
    uint8_t frame_length = 0U;
    uint8_t frame_ready = 0U;
    uint8_t exception_code;
    uint32_t primask;
    uint32_t last_rx_tick;
    VfdCodecStatus status;

    if (g_state == VFD_MODBUS_TX_ACTIVE)
    {
        if (g_uart_error != 0U)
        {
            VfdModbusDiag.uart_error_count++;
            VfdModbus_Finish(VFD_RESULT_UART_ERROR, 0U);
            return;
        }
        if (g_tx_complete != 0U)
        {
            if (g_active_job.epoch != current_epoch)
            {
                VfdModbus_Finish(VFD_RESULT_CANCELED, 0U);
                return;
            }
            g_state = VFD_MODBUS_WAIT_RESPONSE;
            VfdModbusDiag.state = g_state;
        }
        else if ((uint32_t)(now_ms - g_tx_start_tick) >=
                 VFD_MODBUS_TX_TIMEOUT_MS)
        {
            (void)HAL_UART_AbortTransmit(&huart3);
            VfdModbus_Finish((g_active_job.epoch != current_epoch) ?
                             VFD_RESULT_CANCELED : VFD_RESULT_TX_ERROR,
                             0U);
            return;
        }
    }

    if (g_state != VFD_MODBUS_WAIT_RESPONSE)
    {
        return;
    }

    if (g_active_job.epoch != current_epoch)
    {
        VfdModbus_Finish(VFD_RESULT_CANCELED, 0U);
        return;
    }

    if (g_uart_error != 0U)
    {
        VfdModbusDiag.uart_error_count++;
        VfdModbus_Finish(VFD_RESULT_UART_ERROR, 0U);
        return;
    }
    if (g_rx_overflow != 0U)
    {
        VfdModbusDiag.rx_overflow_count++;
        VfdModbus_Finish(VFD_RESULT_WRONG_REPLY, 0U);
        return;
    }

    /* 在同一个临界区中判断静默时间并复制，
       避免判断后、关中断前又到一个字节。 */
    primask = __get_PRIMASK();
    __disable_irq();
    frame_length = g_rx_length;
    last_rx_tick = g_last_rx_tick;
    if ((frame_length != 0U) &&
        ((uint32_t)(now_ms - last_rx_tick) >= VFD_MODBUS_FRAME_GAP_MS))
    {
        memcpy(frame, (const void *)g_rx_frame, frame_length);
        g_rx_length = 0U;
        frame_ready = 1U;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    if (frame_ready != 0U)
    {
        status = VfdCodec_ParseWriteResponse(frame,
                                             frame_length,
                                             1U,
                                             &exception_code);
        if (status == VFD_CODEC_OK)
        {
            VfdModbusDiag.success_count++;
            VfdModbus_Finish(VFD_RESULT_OK, 0U);
        }
        else if (status == VFD_CODEC_EXCEPTION)
        {
            VfdModbusDiag.response_error_count++;
            VfdModbus_Finish(VFD_RESULT_EXCEPTION, exception_code);
        }
        else if (status == VFD_CODEC_CRC_ERROR)
        {
            VfdModbusDiag.response_error_count++;
            VfdModbus_Finish(VFD_RESULT_CRC_ERROR, 0U);
        }
        else
        {
            VfdModbusDiag.response_error_count++;
            VfdModbus_Finish(VFD_RESULT_WRONG_REPLY, 0U);
        }
        return;
    }

    if ((uint32_t)(now_ms - g_tx_complete_tick) >=
        VFD_MODBUS_RESPONSE_TIMEOUT_MS)
    {
        if (g_active_job.epoch != current_epoch)
        {
            VfdModbus_Finish(VFD_RESULT_CANCELED, 0U);
            return;
        }
        if (g_attempts < VFD_MODBUS_MAX_ATTEMPTS)
        {
            VfdModbusDiag.retry_count++;
            VfdModbus_StartAttempt(now_ms);
        }
        else
        {
            VfdModbusDiag.timeout_count++;
            VfdModbus_Finish(VFD_RESULT_TIMEOUT, 0U);
        }
    }
}

void VfdModbus_OnRxByteFromIsr(uint8_t byte, uint32_t now_ms)
{
    if ((g_state != VFD_MODBUS_TX_ACTIVE) &&
        (g_state != VFD_MODBUS_WAIT_RESPONSE))
    {
        VfdModbusDiag.stray_byte_count++;
        return;
    }

    if (g_rx_length < VFD_MODBUS_RX_CAPACITY)
    {
        g_rx_frame[g_rx_length] = byte;
        g_rx_length++;
    }
    else
    {
        g_rx_overflow = 1U;
    }
    g_last_rx_tick = now_ms;
}

void VfdModbus_OnTxCompleteFromIsr(uint32_t now_ms)
{
    if (g_state == VFD_MODBUS_TX_ACTIVE)
    {
        g_tx_complete_tick = now_ms;
        g_tx_complete = 1U;
    }
}

void VfdModbus_OnUartErrorFromIsr(uint32_t hal_error)
{
    (void)hal_error;
    if ((g_state == VFD_MODBUS_TX_ACTIVE) ||
        (g_state == VFD_MODBUS_WAIT_RESPONSE))
    {
        g_uart_error = 1U;
    }
}

uint8_t VfdModbus_IsIdle(void)
{
    return (g_state == VFD_MODBUS_IDLE) ? 1U : 0U;
}

uint8_t VfdModbus_PeekResult(VfdResult *result)
{
    if ((result == NULL) || (g_state != VFD_MODBUS_RESULT_PENDING))
    {
        return 0U;
    }
    *result = g_result;
    return 1U;
}

void VfdModbus_AcknowledgeResult(void)
{
    if (g_state == VFD_MODBUS_RESULT_PENDING)
    {
        memset(&g_active_job, 0, sizeof(g_active_job));
        memset(&g_result, 0, sizeof(g_result));
        g_state = VFD_MODBUS_IDLE;
        VfdModbusDiag.state = g_state;
    }
}
