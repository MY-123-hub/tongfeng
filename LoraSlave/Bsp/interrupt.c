#include "interrupt.h"
#include "usart.h"

uint16_t time_10s,time_100ms=0;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance==TIM4)
    {
      time_10s++;time_100ms++;
    }
}

uint8_t Rx2Buffer[100],rx2_pointer,rx2_data;
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  Rx2Buffer[rx2_pointer++]=rx2_data;
  HAL_UART_Receive_IT(&huart2,&rx2_data,1);
}

