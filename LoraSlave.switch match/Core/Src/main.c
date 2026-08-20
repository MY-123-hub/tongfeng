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
typedef enum { PHASE_CONVERT, PHASE_WAIT, PHASE_READ } SamplePhase_t;

static SamplePhase_t s_phase = PHASE_CONVERT;
static uint8_t       s_wait  = 0;

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

#if 0 /* 旧文本上报路径已停用：从机仅在收到主机轮询后回传二进制温度帧。 */
/* READ 阶段：读所有传感器 → 组帧 → 发 LoRa → LED 指示 */
static void Sensor_Read_Send_All(void)
{
    int32_t  bme_temp  = 0;
    uint32_t bme_press = 0, bme_hum = 0;
    if (bme_ok) {
        BME280_ReadAll(&bme280, &bme_temp, &bme_press, &bme_hum);
    }
    float bme_t = bme_temp / 100.0f;    /* C */
    float bme_h = bme_hum / 1024.0f;    /* %RH */

    uint8_t channel, sen_idx;
    for (channel = 0; channel < 6; channel++) {
        GPIO_TypeDef *port = DS18B20_ChannelPort[channel];
        uint16_t pin   = DS18B20_ChannelPin[channel];
        float tvals[6], hvals[6];
        uint8_t real_count = 0;

        for (sen_idx = 0; sen_idx < DS18B20_SensorNum[channel] && real_count < 6; sen_idx++) {
            if (is_ghost_id(channel, sen_idx)) continue;

            GXHT3W_Read_TempHum(port, pin, channel, sen_idx, &tvals[real_count], &hvals[real_count]);   /* 全部按 GXHT3W 处理 */
#if DEBUG_LOG
            printf("[D] ch%d[%d] fam=0x%02X T=%.2f H=%.2f\r\n",
                   channel, sen_idx, DS18B20_ID[channel][sen_idx][0],
                   tvals[real_count], hvals[real_count]);
#endif
            if (tvals[real_count] > 80 || tvals[real_count] < -20) tvals[real_count] = 0;
            if (hvals[real_count] > 100 || hvals[real_count] < 0) hvals[real_count] = 0;
            real_count++;
        }

        for (sen_idx = real_count; sen_idx < 6; sen_idx++) {
            tvals[sen_idx] = 0; hvals[sen_idx] = 0;
        }

        if (real_count == 0) {
            tvals[0] = 0; tvals[1] = 0; tvals[2] = 0;
            tvals[3] = 0; tvals[4] = 0; tvals[5] = 0;
            hvals[0] = 0; hvals[1] = 0; hvals[2] = 0;
            hvals[3] = 0; hvals[4] = 0; hvals[5] = 0;
        }

        memset(lora_sp, 0, sizeof(lora_sp));
        uint16_t buf_len = sprintf(lora_sp, "PORT:%02d,NUM:%02d,TM:", channel, real_count);
        for (sen_idx = 0; sen_idx < 6; sen_idx++) {
            buf_len += sprintf(lora_sp + buf_len, "%.1f", tvals[sen_idx]);
            if (sen_idx < 5) buf_len += sprintf(lora_sp + buf_len, "/");
        }
        buf_len += sprintf(lora_sp + buf_len, ",HM:");
        for (sen_idx = 0; sen_idx < 6; sen_idx++) {
            buf_len += sprintf(lora_sp + buf_len, "%.1f", hvals[sen_idx]);
            if (sen_idx < 5) buf_len += sprintf(lora_sp + buf_len, "/");
        }
        buf_len += sprintf(lora_sp + buf_len, ",BME_H:%.1f,BME_T:%.1f,Pressure:%lu",
                           bme_h, bme_t, (unsigned long)(bme_press / 100));
        LORA_SendData((unsigned char *)lora_sp, strlen(lora_sp));
    }

    /* LED2：每 10 个采样周期闪烁一次传感器总数 */
    {
        static uint8_t cnt_cycle = 0;
        cnt_cycle++;
        if (cnt_cycle >= 10) {
            cnt_cycle = 0;
            uint8_t total = 0;
            for (uint8_t ch = 0; ch < 6; ch++) {
                for (uint8_t n = 0; n < DS18B20_SensorNum[ch]; n++) {
                    if (!is_ghost_id(ch, n)) total++;
                }
            }
            if (total > 0) {
                led2_off; HAL_Delay(2000);
                for (uint8_t b = 0; b < total; b++) {
                    led2_on;  HAL_Delay(800);
                    led2_off; HAL_Delay(400);
                }
                led2_on;
            }
        }
    }
}
#endif

static int16_t SensorTemperatureToTenths(float temperature)
{
    if (temperature >= 0.0f)
    {
        return (int16_t)(temperature * 10.0f + 0.5f);
    }

    return (int16_t)(temperature * 10.0f - 0.5f);
}

/* READ 阶段：只更新 36 路温度缓存；LoRa 回包由 LoraSlaveProcess() 按主机轮询触发。 */
static void Sensor_UpdateTemperatureCache(void)
{
    int16_t temperature[LORA_PROTOCOL_TEMPERATURE_COUNT];
    uint8_t channel;
    uint8_t sensor_index;
    uint8_t cache_index;
    static uint8_t sample_count;

    for (cache_index = 0U; cache_index < LORA_PROTOCOL_TEMPERATURE_COUNT; cache_index++)
    {
        temperature[cache_index] = LORA_PROTOCOL_TEMPERATURE_INVALID;
    }

    for (channel = 0U; channel < 6U; channel++)
    {
        GPIO_TypeDef *port = DS18B20_ChannelPort[channel];
        uint16_t pin = DS18B20_ChannelPin[channel];
        uint8_t channel_slot = 0U;

        for (sensor_index = 0U;
             (sensor_index < DS18B20_SensorNum[channel]) && (channel_slot < 6U);
             sensor_index++)
        {
            float measured_temperature;
            float measured_humidity;

            if (is_ghost_id(channel, sensor_index))
            {
                continue;
            }

            GXHT3W_Read_TempHum(port, pin, channel, sensor_index,
                                 &measured_temperature, &measured_humidity);
            if ((measured_temperature >= -20.0f) && (measured_temperature <= 80.0f))
            {
                temperature[channel * 6U + channel_slot] =
                    SensorTemperatureToTenths(measured_temperature);
            }
            channel_slot++;
        }
    }

    LoraSlaveUpdateTemperatureCache(temperature);

    /* 不再使用阻塞式 LED 闪烁，以免错过主机轮询。 */
    sample_count++;
    if (sample_count >= 10U)
    {
        sample_count = 0U;
        led2_toggle;
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
    /* 主机轮询一到即处理；不得被 100ms 采样节拍限制。 */
    LoraSlaveProcess();

    /* 三阶段采样状态机：CONVERT → WAIT(750ms) → READ，用 time_100ms(100ms 节拍) 推进 */
    if(time_100ms >= 10)
    {
      time_100ms = 0;

      if(sensor_type == SENSOR_TYPE_NONE) { led3_toggle; }

      switch(s_phase)
      {
        case PHASE_CONVERT:
          Sensor_Convert_All();
          s_phase = PHASE_WAIT;
          s_wait = 0;
          break;

        case PHASE_WAIT:
          if(++s_wait >= 8) { s_phase = PHASE_READ; }   /* 8 × 100ms = 800ms ≥ 750ms 转换时间 */
          break;

        case PHASE_READ:
          Sensor_UpdateTemperatureCache();
          s_phase = PHASE_CONVERT;
          break;
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
#endif /* USE_FULL_ASSERT */
