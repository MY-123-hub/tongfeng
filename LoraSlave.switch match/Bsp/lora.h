#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"
#include "lora_protocol.h"


#define REV_WAIT    1   // ����δ��ɱ�־
#define REV_OK      0   // ������ɱ�־


void LORA_Init(void);
void LORA_SendData(unsigned char *data, unsigned short len);
void LoraProtocolFeedByte(uint8_t data);
void LoraSlaveProcess(void);
void LoraSlaveUpdateTemperatureCache(const int16_t temperature[LORA_PROTOCOL_TEMPERATURE_COUNT]);

extern volatile uint32_t LoraSlavePollRxCount;
extern volatile uint32_t LoraSlaveTemperatureTxCount;


#endif /*__LORA_H__*/
