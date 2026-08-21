#include "lora.h"

#include "gateway_runtime.h"
#include "gpio.h"
#include "usart.h"

#include <string.h>

#define LORA_CONFIG_RX_SIZE            (128U)
#define LORA_CONFIG_RETRY_COUNT        (3U)
#define LORA_CONFIG_REPLY_TIMEOUT_MS   (600U)
#define LORA_APP_TX_TIMEOUT_MS         (30U)

volatile uint32_t LoraControlLoraTxCount;
volatile uint32_t LoraControlLoraTxErrorCount;
volatile uint32_t LoraControlPcTxCount;
volatile uint32_t LoraControlPcTxErrorCount;
volatile uint32_t LoraControlConfigErrorCount;

static volatile uint8_t s_lora_config_mode = 1U;
static volatile uint8_t s_application_ready;
static volatile uint8_t s_config_rx[LORA_CONFIG_RX_SIZE];
static volatile uint16_t s_config_rx_length;
static volatile uint8_t s_config_rx_overflow;

static void LoraControl_ClearConfigResponse(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_config_rx_length = 0U;
    s_config_rx_overflow = 0U;
    s_config_rx[0] = '\0';
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t LoraControl_ConfigContains(const char *expected)
{
    uint8_t local[LORA_CONFIG_RX_SIZE];
    uint16_t length;
    uint32_t primask;

    if ((expected == NULL) || (expected[0] == '\0'))
    {
        return 1U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    length = s_config_rx_length;
    if (length >= LORA_CONFIG_RX_SIZE)
    {
        length = LORA_CONFIG_RX_SIZE - 1U;
    }
    if (length != 0U)
    {
        memcpy(local, (const void *)s_config_rx, length);
    }
    local[length] = '\0';
    if (primask == 0U)
    {
        __enable_irq();
    }

    return (strstr((const char *)local, expected) != NULL) ? 1U : 0U;
}

static uint8_t LoraControl_SendAtCommand(const char *command, const char *expected)
{
    uint8_t attempt;
    uint32_t started_at;

    if (command == NULL)
    {
        return 0U;
    }

    for (attempt = 0U; attempt < LORA_CONFIG_RETRY_COUNT; attempt++)
    {
        LoraControl_ClearConfigResponse();
        if (HAL_UART_Transmit(&huart2, (uint8_t *)command,
                              (uint16_t)strlen(command), 100U) != HAL_OK)
        {
            continue;
        }

        if ((expected == NULL) || (expected[0] == '\0'))
        {
            HAL_Delay(50U);
            return 1U;
        }

        started_at = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - started_at) < LORA_CONFIG_REPLY_TIMEOUT_MS)
        {
            if (LoraControl_ConfigContains(expected) != 0U)
            {
                return 1U;
            }
            HAL_Delay(10U);
        }
    }

    LoraControlConfigErrorCount++;
    return 0U;
}

static uint8_t LoraControl_SendFrame(GatewayOutputPort port,
                                     const uint8_t *frame,
                                     uint16_t frame_length,
                                     void *context)
{
    UART_HandleTypeDef *uart;
    HAL_StatusTypeDef status;

    (void)context;
    if ((frame == NULL) || (frame_length == 0U) || (frame_length > 109U))
    {
        return 0U;
    }

    uart = (port == GATEWAY_OUTPUT_LORA) ? &huart2 : &huart1;
    status = HAL_UART_Transmit(uart, (uint8_t *)frame, frame_length,
                               LORA_APP_TX_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        if (port == GATEWAY_OUTPUT_LORA)
        {
            LoraControlLoraTxCount++;
        }
        else
        {
            LoraControlPcTxCount++;
        }
        return 1U;
    }

    if (port == GATEWAY_OUTPUT_LORA)
    {
        LoraControlLoraTxErrorCount++;
    }
    else
    {
        LoraControlPcTxErrorCount++;
    }
    return 0U;
}

void LoraControl_OnLoraUartByteFromIsr(uint8_t byte)
{
    uint16_t length;

    if (s_lora_config_mode != 0U)
    {
        length = s_config_rx_length;
        if (length < (LORA_CONFIG_RX_SIZE - 1U))
        {
            s_config_rx[length] = byte;
            s_config_rx_length = (uint16_t)(length + 1U);
            s_config_rx[length + 1U] = '\0';
        }
        else
        {
            s_config_rx_overflow = 1U;
        }
        return;
    }

    if (s_application_ready != 0U)
    {
        GatewayRuntime_PushLoRaByteFromIsr(byte);
    }
}

void LoraControl_OnPcUartByteFromIsr(uint8_t byte)
{
    if (s_application_ready != 0U)
    {
        GatewayRuntime_PushPcByteFromIsr(byte);
    }
}

void LoraControl_OnLoraUartErrorFromIsr(void)
{
    LoraControlLoraTxErrorCount++;
}

void LoraControl_OnPcUartErrorFromIsr(void)
{
    LoraControlPcTxErrorCount++;
}

void LORA_Init(void)
{
    static const char * const commands[] =
    {
        /* 先在参数模式写完传输参数，PMODE=RUN 放在最后，避免提前退出参数模式。 */
        "AT+LORAPROT=NODE\r\n", "AT+WMODE=TRANS\r\n", "AT+ITM=20\r\n",
        "AT+WTM=2000\r\n", "AT+RTO=500\r\n", "AT+FDMODE=OFF\r\n",
        "AT+CH=4700\r\n", "AT+SPD=10\r\n", "AT+PWR=22\r\n",
        "AT+FEC=1\r\n", "AT+LBT=OFF\r\n", "AT+ADDR=0\r\n",
        "AT+LRTO=3\r\n", "AT+UARTFT=10\r\n", "AT+MTU=128\r\n",
        "AT+UART=115200,8,1,NONE,NFC\r\n", "AT+PMODE=RUN\r\n"
    };
    uint8_t index;
    uint8_t configured = 1U;

    s_application_ready = 0U;
    s_lora_config_mode = 1U;
    LoraControl_ClearConfigResponse();

    lora_reset_off();
    HAL_Delay(10U);
    lora_reset_on();
    HAL_Delay(50U);

    /* 模块掉电后可能已退出参数模式；本条仅清理旧状态。 */
    (void)LoraControl_SendAtCommand("AT+ENTM\r\n", "");
    if (LoraControl_SendAtCommand("+++", "a") == 0U)
    {
        configured = 0U;
    }
    if (LoraControl_SendAtCommand("a", "OK") == 0U)
    {
        configured = 0U;
    }

    for (index = 0U; index < (uint8_t)(sizeof(commands) / sizeof(commands[0])); index++)
    {
        if (LoraControl_SendAtCommand(commands[index], "OK") == 0U)
        {
            configured = 0U;
        }
    }
    if (LoraControl_SendAtCommand("AT+Z\r\n", "Start") == 0U)
    {
        configured = 0U;
    }

    /*
     * 部分 WH-L101-L 固件在 PMODE=RUN 或 AT+Z 后不回复 OK/Start，
     * 但参数已写入并已进入运行态。不能因“缺少文本回显”永久锁住业务通信。
     * 失败次数仍保留在 LoraControlConfigErrorCount 供现场诊断。
     */
    (void)configured;
    GatewayRuntime_Init(LoraControl_SendFrame, NULL);
    s_lora_config_mode = 0U;
    s_application_ready = 1U;
}

void LoraP2PRX(void)
{
    if (s_application_ready != 0U)
    {
        GatewayRuntime_Process(HAL_GetTick());
    }
}
