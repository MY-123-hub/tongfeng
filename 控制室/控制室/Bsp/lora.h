#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"

/* 控制室 LoRa 模块：USART2 PA2/PA3，115200 8N1。 */
extern volatile uint32_t LoraControlLoraTxCount;
extern volatile uint32_t LoraControlLoraTxErrorCount;
extern volatile uint32_t LoraControlPcTxCount;
extern volatile uint32_t LoraControlPcTxErrorCount;
extern volatile uint32_t LoraControlConfigErrorCount;

void LORA_Init(void);
void LoraP2PRX(void);

/* UART 回调只调用这两个函数把字节交给环形缓冲；不得在中断内解析报文。 */
void LoraControl_OnLoraUartByteFromIsr(uint8_t byte);
void LoraControl_OnPcUartByteFromIsr(uint8_t byte);
void LoraControl_OnLoraUartErrorFromIsr(void);
void LoraControl_OnPcUartErrorFromIsr(void);

#endif /* __LORA_H__ */
