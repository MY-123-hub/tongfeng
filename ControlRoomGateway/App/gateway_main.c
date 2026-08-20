#include "stm32f1xx_hal.h"
#include "gateway_runtime.h"

#include <string.h>

UART_HandleTypeDef huart1; /* 上位机：PA9/PA10，115200 8N1 */
UART_HandleTypeDef huart2; /* LoRa：PA2/PA3，115200 8N1 */

void Error_Handler(void);

static uint8_t g_pc_rx_byte;
static uint8_t g_lora_rx_byte;
static uint8_t g_lora_config_mode = 1U;
static uint8_t g_lora_config_rx[128];
static volatile uint16_t g_lora_config_rx_length;

static void Gateway_SystemClockConfig(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { Error_Handler(); }
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }
}

static void Gateway_GpioInit(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void Gateway_UartInit(UART_HandleTypeDef *uart, USART_TypeDef *instance)
{
    uart->Instance = instance;
    uart->Init.BaudRate = 115200U;
    uart->Init.WordLength = UART_WORDLENGTH_8B;
    uart->Init.StopBits = UART_STOPBITS_1;
    uart->Init.Parity = UART_PARITY_NONE;
    uart->Init.Mode = UART_MODE_TX_RX;
    uart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(uart) != HAL_OK) { Error_Handler(); }
}

static uint8_t Gateway_LoraCommand(const char *command, const char *expected)
{
    uint8_t attempt;
    for (attempt = 0U; attempt < 3U; attempt++)
    {
        g_lora_config_rx_length = 0U;
        if (HAL_UART_Transmit(&huart2, (uint8_t *)command,
                              (uint16_t)strlen(command), 500U) != HAL_OK)
        {
            continue;
        }
        if ((expected == NULL) || (expected[0] == '\0'))
        {
            HAL_Delay(50U);
            return 1U;
        }
        for (uint16_t wait = 0U; wait < 60U; wait++)
        {
            if (strstr((const char *)g_lora_config_rx, expected) != NULL)
            {
                return 1U;
            }
            HAL_Delay(10U);
        }
    }
    return 0U;
}

static void Gateway_LoraConfigure(void)
{
    static const char * const commands[] = {
        "AT+WMODE=TRANS\r\n", "AT+PMODE=RUN\r\n", "AT+ITM=20\r\n",
        "AT+WTM=2000\r\n", "AT+RTO=500\r\n", "AT+FDMODE=OFF\r\n",
        "AT+CH=4700\r\n", "AT+SPD=10\r\n", "AT+PWR=22\r\n",
        "AT+FEC=1\r\n", "AT+LBT=OFF\r\n", "AT+ADDR=0\r\n",
        "AT+LRTO=3\r\n", "AT+UARTFT=10\r\n", "AT+MTU=128\r\n",
        "AT+UART=115200,8,1,NONE,NFC\r\n"
    };
    uint8_t i;

    (void)Gateway_LoraCommand("AT+ENTM\r\n", "");
    (void)Gateway_LoraCommand("+++", "a");
    (void)Gateway_LoraCommand("a", "OK");
    for (i = 0U; i < (uint8_t)(sizeof(commands) / sizeof(commands[0])); i++)
    {
        (void)Gateway_LoraCommand(commands[i], "OK");
    }
    (void)Gateway_LoraCommand("AT+Z\r\n", "Start");
}

static void Gateway_Send(GatewayOutputPort port, const uint8_t *frame,
                         uint16_t frame_length, void *context)
{
    UART_HandleTypeDef *uart = (port == GATEWAY_OUTPUT_LORA) ? &huart2 : &huart1;
    (void)context;
    (void)HAL_UART_Transmit(uart, (uint8_t *)frame, frame_length, 200U);
}

int main(void)
{
    HAL_Init();
    Gateway_SystemClockConfig();
    Gateway_GpioInit();
    Gateway_UartInit(&huart1, USART1);
    Gateway_UartInit(&huart2, USART2);
    (void)HAL_UART_Receive_IT(&huart1, &g_pc_rx_byte, 1U);
    (void)HAL_UART_Receive_IT(&huart2, &g_lora_rx_byte, 1U);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(50U);
    Gateway_LoraConfigure();
    GatewayRuntime_Init(Gateway_Send, NULL);
    g_lora_config_mode = 0U;

    while (1)
    {
        GatewayRuntime_Process(HAL_GetTick());
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (uart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_9; gpio.Mode = GPIO_MODE_AF_PP; gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_10; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
        HAL_NVIC_SetPriority(USART1_IRQn, 1U, 0U);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
    else if (uart->Instance == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_2; gpio.Mode = GPIO_MODE_AF_PP; gpio.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_3; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &gpio);
        HAL_NVIC_SetPriority(USART2_IRQn, 1U, 0U);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1)
    {
        GatewayRuntime_PushPcByteFromIsr(g_pc_rx_byte);
        (void)HAL_UART_Receive_IT(&huart1, &g_pc_rx_byte, 1U);
    }
    else if (uart->Instance == USART2)
    {
        if (g_lora_config_mode != 0U)
        {
            if (g_lora_config_rx_length < (uint16_t)(sizeof(g_lora_config_rx) - 1U))
            {
                g_lora_config_rx[g_lora_config_rx_length++] = g_lora_rx_byte;
                g_lora_config_rx[g_lora_config_rx_length] = '\0';
            }
        }
        else
        {
            GatewayRuntime_PushLoRaByteFromIsr(g_lora_rx_byte);
        }
        (void)HAL_UART_Receive_IT(&huart2, &g_lora_rx_byte, 1U);
    }
}

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
void SysTick_Handler(void) { HAL_IncTick(); }
void Error_Handler(void) { __disable_irq(); while (1) { } }
