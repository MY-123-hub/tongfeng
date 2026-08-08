#ifndef __DS18B20_H
#define __DS18B20_H

#include "main.h"


#define DS18B20_GPIO_X					  GPIOA
#define DS18B20_GPIO_NUM				  GPIO_PIN_5
#define DS18B20_GPIO_NUM1				  GPIO_PIN_6
#define DS18B20_GPIO_NUM2				  GPIO_PIN_7
#define DS18B20_GPIO_NUM3				  GPIO_PIN_8

#define DS18B20_GPIO_X1					  GPIOB
#define DS18B20_GPIO_NUM4				  GPIO_PIN_14
#define DS18B20_GPIO_NUM5				  GPIO_PIN_15

#define DS18B20_DELAY_US(us)    CPU_TS_Tmr_Delay_US(us)
#define DS18B20_DELAY_MS(ms)    CPU_TS_Tmr_Delay_MS(ms)

#define MaxSensorNum 8


uint8_t DS18B20_Init(GPIO_TypeDef * GPIOx,uint16_t PINx);
uint8_t DS18B20_Read_Byte(GPIO_TypeDef * GPIOx,uint16_t PINx);
uint8_t DS18B20_Read_Bit(GPIO_TypeDef * GPIOx,uint16_t PINx);
uint8_t DS18B20_Answer_Check(GPIO_TypeDef * GPIOx,uint16_t PINx);
void DS18B20_GPIO_Config(GPIO_TypeDef * GPIOx,uint16_t PINx);
void DS18B20_Mode_IPU(GPIO_TypeDef * GPIOx,uint16_t PINx);
void DS18B20_Mode_Out(GPIO_TypeDef * GPIOx,uint16_t PINx);
void DS18B20_Rst(GPIO_TypeDef * GPIOx,uint16_t PINx);
void DS18B20_Search_Rom(GPIO_TypeDef * GPIOx,uint16_t PINx, uint8_t channel);
void DS18B20_Write_Byte(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t dat);
float DS18B20_Get_Temp(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t channel, uint8_t i);
void GXHT3W_Read_TempHum(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel, uint8_t idx, float *temp, float *hum);
uint8_t DS18B20_Crc(uint8_t *addr, uint8_t len);


extern unsigned char DS18B20_ID[6][MaxSensorNum][8];
extern unsigned char DS18B20_SensorNum[6];
extern uint8_t crc_t[10];

#endif
