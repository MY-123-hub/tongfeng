#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modbus_async.h"
#include "usart.h"
#include "vfd_modbus_codec.h"

UART_HandleTypeDef huart3;

static HAL_StatusTypeDef g_tx_status;
static uint8_t g_last_tx[VFD_MODBUS_REQUEST_SIZE];
static uint16_t g_last_tx_length;
static uint32_t g_tx_count;
static uint32_t g_abort_count;
static uint32_t g_primask;
static uint32_t g_checks;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        g_checks++;                                                             \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                      uint8_t *data,
                                      uint16_t length)
{
    (void)uart;
    g_tx_count++;
    g_last_tx_length = length;
    if (length <= sizeof(g_last_tx))
    {
        memcpy(g_last_tx, data, length);
    }
    return g_tx_status;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
    (void)uart;
    g_abort_count++;
    return HAL_OK;
}

uint32_t __get_PRIMASK(void)
{
    return g_primask;
}

void __disable_irq(void)
{
    g_primask = 1U;
}

void __enable_irq(void)
{
    g_primask = 0U;
}

static VfdJob MakeJob(uint16_t flow_id)
{
    VfdJob job;

    memset(&job, 0, sizeof(job));
    job.flow_id = flow_id;
    job.frequency_x100 = 5000U;
    job.action = VFD_ACTION_RUN_FORWARD;
    job.epoch = 7U;
    job.request_type = LORA_MSG_MANUAL_RUN;
    job.origin = VFD_JOB_ORIGIN_REMOTE;
    return job;
}

static void ResetDriver(void)
{
    memset(&huart3, 0, sizeof(huart3));
    memset(g_last_tx, 0, sizeof(g_last_tx));
    g_tx_status = HAL_OK;
    g_last_tx_length = 0U;
    g_tx_count = 0U;
    g_abort_count = 0U;
    g_primask = 0U;
    VfdModbus_Init();
}

static void FeedFrame(const uint8_t *frame,
                      uint16_t length,
                      uint32_t first_tick)
{
    uint16_t i;

    for (i = 0U; i < length; i++)
    {
        VfdModbus_OnRxByteFromIsr(frame[i], first_tick + i);
    }
}

static void TestSuccessAndBusy(void)
{
    static const uint8_t reply[VFD_MODBUS_NORMAL_REPLY_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x4AU, 0x08U
    };
    VfdJob job = MakeJob(100U);
    VfdResult result;

    ResetDriver();
    CHECK(VfdModbus_IsIdle() == 1U);
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_ACCEPTED);
    CHECK(g_tx_count == 1U);
    CHECK(g_last_tx_length == VFD_MODBUS_REQUEST_SIZE);
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_BUSY);
    VfdModbus_OnTxCompleteFromIsr(10U);
    VfdModbus_Process(10U, 7U);
    FeedFrame(reply, sizeof(reply), 11U);
    VfdModbus_Process(23U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_OK);
    CHECK(result.flow_id == 100U);
    CHECK(result.frequency_x100 == 5000U);
    CHECK(result.epoch == 7U);
    CHECK(result.attempts == 1U);
    CHECK(VfdModbus_IsIdle() == 0U);
    CHECK(VfdModbus_Start(&job, 24U) == VFD_MODBUS_START_BUSY);
    VfdModbus_AcknowledgeResult();
    CHECK(VfdModbus_IsIdle() == 1U);
}

static void TestTimeoutRetryAndTickWrap(void)
{
    VfdJob job = MakeJob(101U);
    VfdResult result;
    uint32_t start = 0xFFFFFFF0UL;

    ResetDriver();
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(1U);
    VfdModbus_Process(1U, 7U);
    VfdModbus_Process(200U, 7U);
    CHECK(g_tx_count == 1U);
    VfdModbus_Process(201U, 7U);
    CHECK(g_tx_count == 2U);
    VfdModbus_OnTxCompleteFromIsr(202U);
    VfdModbus_Process(202U, 7U);
    VfdModbus_Process(402U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_TIMEOUT);
    CHECK(result.attempts == 2U);
    CHECK(VfdModbusDiag.retry_count == 1U);
    VfdModbus_AcknowledgeResult();

    CHECK(VfdModbus_Start(&job, start) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(start);
    VfdModbus_Process(start, 7U);
    VfdModbus_Process((uint32_t)(start + VFD_MODBUS_RESPONSE_TIMEOUT_MS), 7U);
    CHECK(g_tx_count == 4U);
}

static void TestBoundaryReplyWinsOverTimeout(void)
{
    static const uint8_t reply[VFD_MODBUS_NORMAL_REPLY_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x4AU, 0x08U
    };
    VfdJob job = MakeJob(102U);
    VfdResult result;

    ResetDriver();
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(1U);
    VfdModbus_Process(1U, 7U);
    FeedFrame(reply, sizeof(reply), 194U);
    VfdModbus_Process(206U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_OK);
    CHECK(g_tx_count == 1U);
}

static void TestResponseAndUartErrors(void)
{
    static const uint8_t exception[VFD_MODBUS_EXCEPTION_REPLY_SIZE] = {
        0x01U, 0x90U, 0x08U, 0x4DU, 0xC6U
    };
    static const uint8_t bad_crc[VFD_MODBUS_NORMAL_REPLY_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x4AU, 0x09U
    };
    VfdJob job = MakeJob(103U);
    VfdResult result;
    uint8_t i;

    ResetDriver();
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(1U);
    VfdModbus_Process(1U, 7U);
    FeedFrame(exception, sizeof(exception), 2U);
    VfdModbus_Process(11U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_EXCEPTION);
    CHECK(result.exception_code == 8U);
    VfdModbus_AcknowledgeResult();

    CHECK(VfdModbus_Start(&job, 20U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(21U);
    VfdModbus_Process(21U, 7U);
    FeedFrame(bad_crc, sizeof(bad_crc), 22U);
    VfdModbus_Process(35U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_CRC_ERROR);
    VfdModbus_AcknowledgeResult();

    CHECK(VfdModbus_Start(&job, 40U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnUartErrorFromIsr(1U);
    VfdModbus_Process(41U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_UART_ERROR);
    VfdModbus_AcknowledgeResult();

    CHECK(VfdModbus_Start(&job, 50U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(51U);
    VfdModbus_Process(51U, 7U);
    for (i = 0U; i < (VFD_MODBUS_RX_CAPACITY + 1U); i++)
    {
        VfdModbus_OnRxByteFromIsr(i, (uint32_t)(52U + i));
    }
    VfdModbus_Process(80U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_WRONG_REPLY);
}

static void TestTxFailureGuardsAndStray(void)
{
    VfdJob job = MakeJob(104U);
    VfdResult result;

    ResetDriver();
    VfdModbus_OnRxByteFromIsr(0xAAU, 0U);
    CHECK(VfdModbusDiag.stray_byte_count == 1U);
    CHECK(VfdModbus_Start(NULL, 0U) == VFD_MODBUS_START_INVALID);
    job.action = 2U;
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_INVALID);

    job = MakeJob(104U);
    g_tx_status = HAL_ERROR;
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_ACCEPTED);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_TX_ERROR);
    CHECK(result.attempts == 1U);
    CHECK(VfdModbus_PeekResult(NULL) == 0U);
}

static void TestEpochCancellationPreventsRetry(void)
{
    VfdJob job = MakeJob(105U);
    VfdResult result;

    ResetDriver();
    CHECK(VfdModbus_Start(&job, 0U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_OnTxCompleteFromIsr(1U);
    VfdModbus_Process(1U, 7U);
    VfdModbus_Process(201U, 8U);
    CHECK(g_tx_count == 1U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_CANCELED);
    VfdModbus_AcknowledgeResult();

    CHECK(VfdModbus_Start(&job, 300U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_Process(310U, 8U);
    CHECK(VfdModbus_PeekResult(&result) == 0U);
    VfdModbus_OnTxCompleteFromIsr(311U);
    VfdModbus_Process(311U, 8U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_CANCELED);
    CHECK(g_tx_count == 2U);
}

static void TestTxCompletionTimeoutIsBounded(void)
{
    VfdJob job = MakeJob(106U);
    VfdResult result;

    ResetDriver();
    CHECK(VfdModbus_Start(&job, 100U) == VFD_MODBUS_START_ACCEPTED);
    VfdModbus_Process(149U, 7U);
    CHECK(VfdModbus_PeekResult(&result) == 0U);
    VfdModbus_Process(150U, 7U);
    CHECK(g_abort_count == 1U);
    CHECK(VfdModbus_PeekResult(&result) == 1U);
    CHECK(result.code == VFD_RESULT_TX_ERROR);
    VfdModbus_AcknowledgeResult();
    CHECK(VfdModbus_IsIdle() == 1U);
    CHECK(VfdModbus_Start(&job, 200U) == VFD_MODBUS_START_ACCEPTED);
}

int main(void)
{
    TestSuccessAndBusy();
    TestTimeoutRetryAndTickWrap();
    TestBoundaryReplyWinsOverTimeout();
    TestResponseAndUartErrors();
    TestTxFailureGuardsAndStray();
    TestEpochCancellationPreventsRetry();
    TestTxCompletionTimeoutIsBounded();
    printf("modbus_async: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
