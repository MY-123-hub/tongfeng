/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "core_delay.h" 
#include "interrupt.h"
#include "bsp_led.h"
#include "stdio.h"
#include "string.h"
#include "bsp_dht11.h"
#include "bsp_ds18b20.h"
#include "lora.h"
#include "iic.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

SysVariTypeDef SysVariType={0};

DHT11_Data_TypeDef DHT11_Data;

/* 理论每个通道可以读取 8 个温度数目 */
GPIO_TypeDef* DS18B20_ChannelPort[6]={GPIOA,GPIOA,GPIOA,GPIOA,GPIOB,GPIOB};   // 6个温度传感器接口端口
uint16_t DS18B20_ChannelPin[6]={GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7,GPIO_PIN_8,GPIO_PIN_15,GPIO_PIN_14};   // 6个温度传感器接口引脚
float DS18B20_Last[6][6];    //分别记录 6 路上的 6 个温度点，通过对比本次数据来过滤异常数据，防止某次温度数据异常导致的错误处理


char lora_sp[256];

uint8_t sensor_exist_flag = 0; 
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM4_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_I2C2_Init();
  HAL_I2C_MspInit(&hi2c2);
  /* USER CODE BEGIN 2 */
  printf("\r\n uart1 printf init success!! \r\n");
  
  HAL_TIM_Base_Start_IT(&htim4);      // 开启基础定时器
  HAL_UART_Receive_IT(&huart2,&rx2_data,1);   // uart2 接受中断开启

  HAL_Delay(50);
  LORA_Init();    // LORA 参数初始化
  
  led2_on;    // LED2 灯亮——LORA 初始化成功
	uint8_t ch;
for(ch=0;ch<6;ch++)
{
    // ???????IO
    DS18B20_Init(DS18B20_ChannelPort[ch], DS18B20_ChannelPin[ch]);
    // ??????????ROM
    DS18B20_Search_Rom(DS18B20_ChannelPort[ch], DS18B20_ChannelPin[ch], ch);
    printf("??%d ?????:%d\r\n",ch,DS18B20_SensorNum[ch]);
}

  /* USER CODE END 2 */

  /* Infinite loop */
 /* Infinite loop */
/* USER CODE BEGIN WHILE */
while (1)
{
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint8_t channel, sen_idx;
    float temp_val;
	  if(time_100ms >= 10)
	{
	    time_100ms = 0;
	    sensor_exist_flag = 0;

	    for(channel = 0; channel < 6; channel++)
	    {
	        GPIO_TypeDef *port = DS18B20_ChannelPort[channel];
	        uint16_t pin   = DS18B20_ChannelPin[channel];
	        memset(lora_sp, 0, sizeof(lora_sp));
	        uint16_t buf_len = sprintf(lora_sp, "PORT:%02d,NUM:06,TM:", channel);

	        for(sen_idx = 0; sen_idx < 6; sen_idx++)
	        {
	            if(sen_idx < DS18B20_SensorNum[channel])
	            {
	                sensor_exist_flag = 1;
	                temp_val = DS18B20_Get_Temp(port, pin, channel, sen_idx);
	                if(temp_val > 80 || temp_val < -20) temp_val = 0;
	            }
	            else
	            {
	                temp_val = 0;
	            }
	            buf_len += sprintf(lora_sp + buf_len, "%.1f", temp_val);
	            if(sen_idx < 5)
	                buf_len += sprintf(lora_sp + buf_len, "/");
	        }

	        buf_len += sprintf(lora_sp + buf_len, ",DHT11_H:0.0,DHT11_T:0.0,Pressure:0.0");
	        LORA_SendData((unsigned char *)lora_sp, strlen(lora_sp));
	    }
	}

// ????????LED3??/??,??if??,????
if(sensor_exist_flag == 1)
{
    led3_on;  // ????,??
}
else
{
    led3_off; // ????,??
}
    /* USER CODE END 3 */
}
}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}

#endif /* USE_FULL_ASSERT */
