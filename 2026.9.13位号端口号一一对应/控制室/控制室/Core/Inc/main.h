/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
  
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/**
  * @brief  通风温度 变量结构体定义
  */
typedef struct
{
    volatile uint16_t vent_temp;                     // 通风要求温度数，通常内环流控温温度要求：大于26℃开启，低于22℃后关闭

    volatile uint16_t vent_temp_outmax_num;          // 超出通风要求温度数，本例程暂时用：3——超过三个温度温度点则开启通风
    
    volatile uint8_t vent_open_flag;                // 通风开启标志位
}SysVariTypeDef;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
extern uint8_t uart3_rx_data;  /* 串口 3 接收数据变量 */
extern SysVariTypeDef SysVariType;

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED2_Pin GPIO_PIN_15
#define LED2_GPIO_Port GPIOA
#define LED1_Pin GPIO_PIN_3
#define LED1_GPIO_Port GPIOB
#define LORA_WAKE_Pin GPIO_PIN_5
#define LORA_WAKE_GPIO_Port GPIOB
#define LORA_RELOAD_Pin GPIO_PIN_6
#define LORA_RELOAD_GPIO_Port GPIOB
#define LORA_RESET_Pin GPIO_PIN_7
#define LORA_RESET_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
