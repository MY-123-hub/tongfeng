#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"


#define REV_WAIT    1   // ����δ��ɱ�־
#define REV_OK      0   // ������ɱ�־


void LORA_Init(void);
void LORA_SendData(unsigned char *data, unsigned short len);


#endif /*__LORA_H__*/
