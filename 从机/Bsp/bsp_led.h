#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#include "main.h"

#define led2_on             HAL_GPIO_WritePin(LED2_GPIO_Port,LED2_Pin,GPIO_PIN_RESET)
#define led2_off            HAL_GPIO_WritePin(LED2_GPIO_Port,LED2_Pin,GPIO_PIN_SET)
#define led2_toggle         HAL_GPIO_TogglePin(LED2_GPIO_Port,LED2_Pin)

#define led3_on             HAL_GPIO_WritePin(LED3_GPIO_Port,LED3_Pin,GPIO_PIN_RESET)
#define led3_off            HAL_GPIO_WritePin(LED3_GPIO_Port,LED3_Pin,GPIO_PIN_SET)
#define led3_toggle         HAL_GPIO_TogglePin(LED3_GPIO_Port,LED3_Pin)

#endif /*__BSP_LED_H__*/
