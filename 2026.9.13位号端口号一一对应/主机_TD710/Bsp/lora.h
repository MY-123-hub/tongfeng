#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"
#include "stdio.h"
#include "string.h"
#include "gpio.h"
#include "usart.h"


typedef struct
{
    volatile uint32_t accepted_frame_count;   /* 已通过完整协议校验的帧数 */
    volatile uint32_t rejected_frame_count;   /* 长度、CRC 或字段校验失败的帧数 */
    volatile uint32_t data_loss_count;        /* 串口错误或环形缓冲区满次数 */
    volatile uint32_t aborted_frame_count;    /* 数据丢失后放弃的半帧数 */
    volatile uint32_t event_queue_drop_count; /* 合法帧因业务事件队列满而丢弃的次数 */
    volatile uint32_t tx_frame_count;         /* 已从队列编码并成功交给串口的帧数 */
    volatile uint32_t tx_encode_error_count;  /* 发送消息未通过协议编码检查的次数 */
    volatile uint32_t tx_uart_error_count;    /* 串口发送失败次数 */
    volatile uint32_t address_filter_drop_count; /* 入业务队列前丢弃的非本机帧 */
    volatile uint32_t config_failure_count;   /* AT命令用尽有限重试的次数 */
    volatile uint8_t configuration_degraded;  /* 1=上电参数未全部获得OK确认 */
    volatile uint8_t last_message_type;       /* 最近合法报文类型 */
    volatile uint16_t last_flow_id;           /* 最近合法报文流水号 */
} LoRaDiagnostics;



/**
  * @brief  基础功能码定义
  */
#define REV_WAIT    1   // 接受未完成标志
#define REV_OK      0   // 接受完成标志



/* 变量声明 */
extern LoRaDiagnostics LoRaDiag;



/* 函数声明 */
void LORA_Init(void);
void LoraP2PTX(void);
void LoraP2PRX(void);


#endif /*__LORA_H__*/
