#ifndef __LORA_H__
#define __LORA_H__

#include "main.h"
#include "stdio.h"
#include "string.h"
#include "gpio.h"
#include "usart.h"
#include "modbus.h"


/**
  * @brief  DGUS 变量结构体定义
  */
typedef struct
{
    volatile int DS18B20_PORT,DS18B20_NUM;           // 变量：记录 DS18B20 的端口号的 数量 
    volatile int DS18B20_Data[8];                  // 变量：记录 DS18B20 一个通道的温度数据 
	    volatile int HM_Data[8];                     // variable: humidity data
    volatile int DHT11_Humi,DHT11_Temp;            // 变量：记录一次 DHT11 的温湿度数据 
    volatile int WindPressure;            // 变量：记录风压 
}LoRaTypeDef;



/**
  * @brief  基础功能码定义
  */
#define REV_WAIT    1   // 接受未完成标志
#define REV_OK      0   // 接受完成标志



/* 变量声明 */
extern LoRaTypeDef LoRaType;



/* 函数声明 */
void LORA_Init(void);
void LoraP2PTX(void);
void LoraP2PRX(void);


#endif /*__LORA_H__*/
