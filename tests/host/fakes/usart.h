#ifndef TEST_FAKE_USART_H
#define TEST_FAKE_USART_H

#include <stdint.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef struct
{
    uint32_t ErrorCode;
} UART_HandleTypeDef;

extern UART_HandleTypeDef huart3;

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                      uint8_t *data,
                                      uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);

#endif /* TEST_FAKE_USART_H */
