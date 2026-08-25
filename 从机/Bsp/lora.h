#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"


#define REV_WAIT    1   // ����δ��ɱ�־
#define REV_OK      0   // ������ɱ�־


void LORA_Init(void);
uint8_t LORA_SendData(const uint8_t *data, uint16_t len);
void LoraP2PTrans(void);

extern volatile uint32_t LoraSlaveConfigErrorCount;
extern volatile uint32_t LoraSlaveTxErrorCount;


#endif /*__LORA_H__*/
