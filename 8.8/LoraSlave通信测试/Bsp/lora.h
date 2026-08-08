#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"


#define REV_WAIT    1   // 接受未完成标志
#define REV_OK      0   // 接受完成标志


void LORA_Init(void);
void LORA_SendData(unsigned char *data, unsigned short len);
void LoraP2PTrans(void);


#endif /*__LORA_H__*/
