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

GPIO_TypeDef* DS18B20_ChannelPort[6]={GPIOA,GPIOA,GPIOA,GPIOA,GPIOB,GPIOB};
uint16_t DS18B20_ChannelPin[6]={GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7,GPIO_PIN_8,GPIO_PIN_15,GPIO_PIN_14};
float DS18B20_Last[6][6];


char lora_sp[200];
SensorType sensor_type;
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
  uint32_t num_i=0;
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

  HAL_TIM_Base_Start_IT(&htim4);
  HAL_UART_Receive_IT(&huart2,&rx2_data,1);

  HAL_Delay(50);
  LORA_Init();

  led2_on;

  CPU_TS_TmrInit();
  sensor_type = Sensor_Detect(DS18B20_ChannelPort[0], DS18B20_ChannelPin[0]);
  if (sensor_type == SENSOR_TYPE_OLD) { led3_on; }
  else if (sensor_type == SENSOR_TYPE_NEW) { led3_off; }

  /* init all 6 channels: search ROM once */
  for(uint8_t ch=0; ch<6; ch++)
  {
    DS18B20_Init(DS18B20_ChannelPort[ch], DS18B20_ChannelPin[ch]);
    HAL_Delay(500);
    DS18B20_Search_Rom(DS18B20_ChannelPort[ch], DS18B20_ChannelPin[ch], ch);
    printf("CH%d sensors:%d\r\n", ch, DS18B20_SensorNum[ch]);
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint8_t channel, sen_idx;
    float temp_val, hum_val;

    if(time_100ms >= 10)
    {
      time_100ms = 0;

      if(sensor_type == SENSOR_TYPE_NONE) { led3_toggle; }

      for(channel = 0; channel < 6; channel++)
      {
        GPIO_TypeDef *port = DS18B20_ChannelPort[channel];
        uint16_t pin   = DS18B20_ChannelPin[channel];
        float tvals[6], hvals[6];
        uint8_t real_count = 0;

        /* pre-scan: count real sensors and read temp+hum in one pass */
        for(sen_idx = 0; sen_idx < DS18B20_SensorNum[channel] && real_count < 6; sen_idx++)
        {
          /* skip ghost */
          uint8_t zero = 1;
          for(uint8_t k=0; k<8; k++) {
            if(DS18B20_ID[channel][sen_idx][k] != 0x00) { zero = 0; break; }
          }
          if(zero) continue;

          if(DS18B20_ID[channel][sen_idx][0] != 0x28) {
            GXHT3W_Read_TempHum(port, pin, channel, sen_idx, &tvals[real_count], &hvals[real_count]);
          } else {
            tvals[real_count] = DS18B20_Get_Temp(port, pin, channel, sen_idx);
            hvals[real_count] = 0;
            if(tvals[real_count] == -85) tvals[real_count] = 0;
          }
          if(tvals[real_count] > 80 || tvals[real_count] < -20) tvals[real_count] = 0;
          if(hvals[real_count] > 100 || hvals[real_count] < 0) hvals[real_count] = 0;
          real_count++;
        }

        /* fill remaining slots with 0 */
        for(sen_idx = real_count; sen_idx < 6; sen_idx++) {
          tvals[sen_idx] = 0; hvals[sen_idx] = 0;
        }

        /* skip if no real sensors */
        if(real_count == 0)
        {
          tvals[0] = 0; tvals[1] = 0; tvals[2] = 0;
          tvals[3] = 0; tvals[4] = 0; tvals[5] = 0;
          hvals[0] = 0; hvals[1] = 0; hvals[2] = 0;
          hvals[3] = 0; hvals[4] = 0; hvals[5] = 0;
        }

        /* build LoRa frame */
        memset(lora_sp, 0, sizeof(lora_sp));
        uint16_t buf_len = sprintf(lora_sp, "PORT:%02d,NUM:%02d,TM:", channel, real_count);
        for(sen_idx = 0; sen_idx < 6; sen_idx++) {
          buf_len += sprintf(lora_sp + buf_len, "%.1f", tvals[sen_idx]);
          if(sen_idx < 5) buf_len += sprintf(lora_sp + buf_len, "/");
        }
        buf_len += sprintf(lora_sp + buf_len, ",HM:");
        for(sen_idx = 0; sen_idx < 6; sen_idx++) {
          buf_len += sprintf(lora_sp + buf_len, "%.1f", hvals[sen_idx]);
          if(sen_idx < 5) buf_len += sprintf(lora_sp + buf_len, "/");
        }
        buf_len += sprintf(lora_sp + buf_len, ",DHT11_H:0.0,DHT11_T:0.0,Pressure:0");
        LORA_SendData((unsigned char *)lora_sp, strlen(lora_sp));
      }

      /* LED2: blink total sensor count every 10 cycles */
      {
        static uint8_t cnt_cycle = 0;
        cnt_cycle++;
        if(cnt_cycle >= 10) {
          cnt_cycle = 0;
          uint8_t total = 0;
          for(uint8_t ch=0; ch<6; ch++) {
            for(uint8_t n=0; n<DS18B20_SensorNum[ch]; n++) {
              uint8_t zero = 1;
              for(uint8_t k=0; k<8; k++) {
                if(DS18B20_ID[ch][n][k] != 0x00) { zero = 0; break; }
              }
              if(!zero) total++;
            }
          }
          if(total > 0) {
            led2_off; HAL_Delay(2000);
            for(uint8_t b=0; b<total; b++) {
              led2_on;  HAL_Delay(800);
              led2_off; HAL_Delay(400);
            }
            led2_on;
          }
        }
      }
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
#endif /* USE_FULL_ASSERT */eader */
