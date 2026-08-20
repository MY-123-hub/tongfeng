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
#include "slave_protocol_runtime.h"
#include "core_delay.h"
#include "interrupt.h"
#include "bsp_led.h"
#include "stdio.h"
#include "string.h"
#include "bsp_dht11.h"
#include "bsp_ds18b20.h"
#include "lora.h"
#include "iic.h"
#include "dip_switch.h"
#include "bme280.h"
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
DHT11_Data_TypeDef DHT11_Data;

GPIO_TypeDef* DS18B20_ChannelPort[6]={GPIOA,GPIOA,GPIOA,GPIOA,GPIOB,GPIOB};
uint16_t DS18B20_ChannelPin[6]={GPIO_PIN_5,GPIO_PIN_6,GPIO_PIN_7,GPIO_PIN_8,GPIO_PIN_15,GPIO_PIN_14};


char lora_sp[200];
SensorType sensor_type;

BME280_HandleTypeDef bme280;
uint8_t bme_ok = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint8_t s_sample_active;
static uint32_t s_sample_ready_tick;
static uint16_t s_sample_flow_id;

/* 判断 ROM 序列号是否全 0（幽灵/空槽） */
static uint8_t is_ghost_id(uint8_t channel, uint8_t idx)
{
    uint8_t k;
    for (k = 0; k < 8; k++) {
        if (DS18B20_ID[channel][idx][k] != 0x00) return 0;
    }
    return 1;
}

/* CONVERT 阶段：对所有真实传感器发转换命令，随后统一强上拉 DQ */
static void Sensor_Convert_All(void)
{
    uint8_t ch, n;
    for (ch = 0; ch < 6; ch++) {
        GPIO_TypeDef *port = DS18B20_ChannelPort[ch];
        uint16_t pin = DS18B20_ChannelPin[ch];

        /* 该通道有真实传感器才转换 */
        uint8_t has = 0;
        for (n = 0; n < DS18B20_SensorNum[ch]; n++) {
            if (!is_ghost_id(ch, n)) { has = 1; break; }
        }
        if (!has) continue;

        /* 广播转换：Skip ROM(0xCC) + 0x44 一次启动该通道所有传感器，随后立即强上拉。
           不能逐个 Match ROM 发 0x44——后面的复位会把总线拉低，打断前面正在转换的传感器 */
        __disable_irq();
        DS18B20_Rst(port, pin);
        DS18B20_DELAY_US(480);
        DS18B20_Write_Byte(port, pin, 0xCC);   /* Skip ROM */
        DS18B20_Write_Byte(port, pin, 0x44);   /* Convert */
        __enable_irq();
        DS18B20_Mode_Out_PP(port, pin);        /* 强上拉，保持 750ms */
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    }
}

/* 读取36点。温度为0或不在传感器有效范围内，按项目约定写0（无效点）。 */
static void Sensor_Read_AllTemperatures(int16_t temperatures[36])
{
    uint8_t channel, sen_idx;
    for (channel = 0; channel < 6; channel++) {
        GPIO_TypeDef *port = DS18B20_ChannelPort[channel];
        uint16_t pin   = DS18B20_ChannelPin[channel];
        uint8_t point = (uint8_t)(channel * 6U);
        uint8_t real_count = 0;

        for (sen_idx = 0U; sen_idx < 6U; sen_idx++) {
            temperatures[point + sen_idx] = 0;
        }

        for (sen_idx = 0; sen_idx < DS18B20_SensorNum[channel] && real_count < 6; sen_idx++) {
            float temperature;
            float humidity;
            if (is_ghost_id(channel, sen_idx)) continue;

            GXHT3W_Read_TempHum(port, pin, channel, sen_idx, &temperature, &humidity);
#if DEBUG_LOG
            printf("[D] ch%d[%d] fam=0x%02X T=%.2f H=%.2f\r\n",
                    channel, sen_idx, DS18B20_ID[channel][sen_idx][0],
                    temperature, humidity);
#endif
            if ((temperature >= -20.0f) && (temperature <= 80.0f) && (temperature != 0.0f)) {
                temperatures[point + real_count] = (int16_t)(temperature * 10.0f +
                    ((temperature >= 0.0f) ? 0.5f : -0.5f));
            }
            real_count++;
        }
    }
}
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
  DIP_Switch_Init();

  /* BME280 温湿度气压传感器（I2C2: PB10-SCL, PB11-SDA），自动探测 0x76/0x77 */
  if (BME280_Init(&bme280, &hi2c2, BME280_I2C_ADDR_PRIM) == HAL_OK ||
      BME280_Init(&bme280, &hi2c2, BME280_I2C_ADDR_SEC) == HAL_OK)
  {
    BME280_Config(&bme280, BME280_OVERSAMPLING_X2,
                  BME280_OVERSAMPLING_X16, BME280_OVERSAMPLING_X1,
                  BME280_FILTER_X4);
    bme_ok = 1;
    printf("BME280 OK\r\n");
  }
  else
  {
    printf("BME280 NOT FOUND\r\n");
  }

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
    uint16_t requested_flow;
    uint32_t now = HAL_GetTick();
    LoraP2PTrans();

    if ((s_sample_active == 0U) &&
        (SlaveRuntime_TakeSampleRequest(&requested_flow) != 0U))
    {
      Sensor_Convert_All();
      s_sample_flow_id = requested_flow;
      s_sample_ready_tick = now + 800U;
      s_sample_active = 1U;
    }

    if ((s_sample_active != 0U) && ((uint32_t)(now - s_sample_ready_tick) < 0x80000000UL))
    {
      int16_t temperatures[36];
      Sensor_Read_AllTemperatures(temperatures);
      SlaveRuntime_CompleteSample(s_sample_flow_id, temperatures);
      s_sample_active = 0U;
    }

    if ((sensor_type == SENSOR_TYPE_NONE) && (time_100ms >= 10U))
    {
      time_100ms = 0U;
      led3_toggle;
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
