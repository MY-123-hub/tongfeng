#include "interrupt.h"
#include "usart.h"
#include "core_delay.h"
#include "stdio.h"
#include "bsp_ds18b20.h"
#include "bsp_led.h"

volatile uint16_t time_100ms = 0;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance==TIM4)
    {
      time_100ms++;
    }
}

volatile uint8_t Rx2Buffer[100], rx2_pointer, rx2_data;
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (rx2_pointer < sizeof(Rx2Buffer)) {
    Rx2Buffer[rx2_pointer++] = rx2_data;
  }
  HAL_UART_Receive_IT(&huart2, &rx2_data, 1);
}

SensorType Sensor_Detect(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    uint8_t i, j;
    uint8_t found_ds18b20 = 0, found_gxht3w = 0;

    /* charge */
    DS18B20_GPIO_Config(GPIOx, PINx);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
    HAL_Delay(2000);

    /* try standard Search ROM first */
    DS18B20_Search_Rom(GPIOx, PINx, 0);
    for (i = 0; i < DS18B20_SensorNum[0]; i++) {
        uint8_t zero = 1;
        for (j = 0; j < 8; j++) {
            if (DS18B20_ID[0][i][j] != 0x00) { zero = 0; break; }
        }
        if (zero) continue;
        if (DS18B20_ID[0][i][0] == 0x28) found_ds18b20 = 1;
        if (DS18B20_ID[0][i][0] == 0x2C) found_gxht3w = 1;
    }

    /* if nothing found, try Skip ROM to confirm any device exists */
    if (!found_ds18b20 && !found_gxht3w) {
        uint8_t buf[9];
        DS18B20_Rst(GPIOx, PINx);
        /* shorter wait: 60us instead of 480us */
        CPU_TS_Tmr_Delay_US(60);
        DS18B20_Write_Byte(GPIOx, PINx, 0xCC);  /* Skip ROM */
        DS18B20_Write_Byte(GPIOx, PINx, 0xBE);  /* Read Scratchpad */
        for (i = 0; i < 9; i++) {
            buf[i] = DS18B20_Read_Byte(GPIOx, PINx);
        }
        /* check if any byte is not 0xFF */
        for (i = 0; i < 9; i++) {
            if (buf[i] != 0xFF) {
                found_gxht3w = 1;
                break;
            }
        }
    }

    if (found_gxht3w) return SENSOR_TYPE_NEW;    /* GXHT3W: LED3 off */
    if (found_ds18b20) return SENSOR_TYPE_OLD;   /* DS18B20: LED3 on */
    return SENSOR_TYPE_NONE;                      /* nothing: blink */
}

