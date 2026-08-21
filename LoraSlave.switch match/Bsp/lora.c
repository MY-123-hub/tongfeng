#include "lora.h"

#include "dip_switch.h"
#include "interrupt.h"
#include "slave_protocol_runtime.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define LORA_AT_RETRY_COUNT          (3U)
#define LORA_AT_REPLY_TIMEOUT_MS     (300U)
#define LORA_APP_TX_TIMEOUT_MS       (30U)

volatile uint32_t LoraSlaveConfigErrorCount;
volatile uint32_t LoraSlaveTxErrorCount;

/**
 ******************************************************************************
  @功能：清空 LoRa 参数配置阶段的串口接收缓存。
  @日期：2026-08-21
  @参数：无
  @返回值：无
  @使用说明：仅在 LoRa 模块处于参数模式时调用；临界区防止 USART2 中断并发写入。
 ******************************************************************************
 */
void LORA_Clear(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memset((void *)Rx2Buffer, 0, sizeof(Rx2Buffer));
    rx2_pointer = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static uint8_t LORA_ConfigContains(const char *expected)
{
    uint8_t response[sizeof(Rx2Buffer) + 1U];
    uint8_t length;
    uint32_t primask;

    if ((expected == NULL) || (expected[0] == '\0'))
    {
        return 1U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    length = rx2_pointer;
    if (length > sizeof(Rx2Buffer))
    {
        length = sizeof(Rx2Buffer);
    }
    if (length != 0U)
    {
        memcpy(response, (const void *)Rx2Buffer, length);
    }
    response[length] = '\0';
    if (primask == 0U)
    {
        __enable_irq();
    }

    return (strstr((const char *)response, expected) != NULL) ? 1U : 0U;
}

/**
 ******************************************************************************
  @功能：有限次数发送一条 WH-L101-L AT 参数命令并等待指定回显。
  @日期：2026-08-21
  @参数：[输入] command - 以 CRLF 结尾的 AT 命令
          [输入] expected - 期望回显；空字符串表示仅发送后短暂等待
  @返回值：uint8_t - 1 表示收到期望回显或无需回显，0 表示三次内失败
  @使用说明：仅初始化阶段调用；失败只记录诊断，不能无限阻塞从机启动。
 ******************************************************************************
 */
static uint8_t LORA_SendAtCommand(const char *command, const char *expected)
{
    uint8_t attempt;
    uint32_t started_at;

    if (command == NULL)
    {
        return 0U;
    }

    for (attempt = 0U; attempt < LORA_AT_RETRY_COUNT; attempt++)
    {
        LORA_Clear();
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
        while ((uint32_t)(HAL_GetTick() - started_at) < LORA_AT_REPLY_TIMEOUT_MS)
        {
            if (LORA_ConfigContains(expected) != 0U)
            {
                return 1U;
            }
            HAL_Delay(10U);
        }
    }

    LoraSlaveConfigErrorCount++;
    return 0U;
}

/**
 ******************************************************************************
  @功能：向 LoRa 模块串口发送一帧完整应用层二进制报文。
  @日期：2026-08-21
  @参数：[输入] data - 完整协议帧缓存
          [输入] len - 协议帧长度，范围 1~109 字节
  @返回值：uint8_t - 1 表示 UART 已发送完成，0 表示参数或 UART 发送失败
  @使用说明：调用方在失败后最多重试有限次数；data 在函数返回前不得被改写。
 ******************************************************************************
 */
uint8_t LORA_SendData(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U) || (len > 109U))
    {
        LoraSlaveTxErrorCount++;
        return 0U;
    }

    if (HAL_UART_Transmit(&huart2, (uint8_t *)data, len,
                          LORA_APP_TX_TIMEOUT_MS) != HAL_OK)
    {
        LoraSlaveTxErrorCount++;
        return 0U;
    }
    return 1U;
}

/**
 ******************************************************************************
  @功能：将 WH-L101-L 配置为本项目统一的 NODE 透明传输参数。
  @日期：2026-08-21
  @参数：无
  @返回值：无
  @使用说明：配置失败不会阻止已有正确参数的模块进入业务运行态；失败次数由
             LoraSlaveConfigErrorCount 提供现场诊断。
 ******************************************************************************
 */
void LORA_Init(void)
{
    static const char * const commands[] =
    {
        "AT+LORAPROT=NODE\r\n", "AT+WMODE=TRANS\r\n", "AT+ITM=20\r\n",
        "AT+WTM=2000\r\n", "AT+RTO=500\r\n", "AT+FDMODE=OFF\r\n",
        "AT+CH=4700\r\n", "AT+SPD=10\r\n", "AT+PWR=22\r\n",
        "AT+FEC=1\r\n", "AT+LBT=OFF\r\n", "AT+ADDR=0\r\n",
        "AT+LRTO=3\r\n", "AT+UARTFT=10\r\n", "AT+MTU=128\r\n",
        "AT+UART=115200,8,1,NONE,NFC\r\n", "AT+PMODE=RUN\r\n"
    };
    uint8_t index;
    uint8_t entered_config_mode = 1U;
    uint8_t local_group;

    LORA_Clear();
    /* 先退出可能残留的参数态，再按模块握手重新进入。 */
    (void)LORA_SendAtCommand("AT+ENTM\r\n", "");
    if (LORA_SendAtCommand("+++", "a") == 0U)
    {
        entered_config_mode = 0U;
    }
    if ((entered_config_mode != 0U) &&
        (LORA_SendAtCommand("a", "OK") == 0U))
    {
        entered_config_mode = 0U;
    }

    if (entered_config_mode != 0U)
    {
        for (index = 0U; index < (uint8_t)(sizeof(commands) / sizeof(commands[0])); index++)
        {
            (void)LORA_SendAtCommand(commands[index], "OK");
        }
        (void)LORA_SendAtCommand("AT+Z\r\n", "Start");
    }

    /* 拨码非法时 SlaveRuntime 保持非业务态，不会伪造任意组号回复。 */
    local_group = DIP_Switch_Read();
    SlaveRuntime_Init(local_group);
    if (LoraSlaveConfigErrorCount != 0U)
    {
        printf("[LORA] config errors:%lu\r\n", (unsigned long)LoraSlaveConfigErrorCount);
    }
    if (SlaveRuntime_IsApplicationMode() == 0U)
    {
        printf("[LORA] invalid slave group:%u\r\n", (unsigned int)local_group);
    }
}

/**
 ******************************************************************************
  @功能：在主循环中处理 LoRa 收发状态机。
  @日期：2026-08-21
  @参数：无
  @返回值：无
  @使用说明：不得在 USART2 中断中调用。
 ******************************************************************************
 */
void LoraP2PTrans(void)
{
    SlaveRuntime_Process(HAL_GetTick());
}
