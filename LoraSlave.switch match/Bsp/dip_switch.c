#include "dip_switch.h"

/**
 * @brief  拨码开关 GPIO 初始化：PB8/PB9/PB12/PB13 配置为输入上拉
 * @note   接法为「一端接 GND + 内部上拉」，故拨 ON=读到 0、OFF=读到 1。
 *         拨码是电平配置（不是边沿触发），无需消抖、无需中断。
 * @param  无
 * @retval 无
 */
void DIP_Switch_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 端口时钟（四个引脚都在 GPIOB，使能一次即可） */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;   /* F103 无内部下拉，必须「上拉 + 外部接地」 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    /* PB8(bit0) / PB9(bit1) / PB12(bit2) / PB13(bit3) */
    GPIO_InitStruct.Pin = DIP_BIT0_Pin | DIP_BIT1_Pin | DIP_BIT2_Pin | DIP_BIT3_Pin;
    HAL_GPIO_Init(DIP_BIT0_GPIO_Port, &GPIO_InitStruct);
}

/**
 * @brief  读取 4 位拨码开关，拼成一个 0~15 的数值
 * @note   bit0=PB8, bit1=PB9, bit2=PB12, bit3=PB13；
 *         返回逻辑值：拨 ON=1、OFF=0（硬件 ON=接地读 0，此处已取反）。
 *         具体功能映射（拨到几代表什么）由调用方决定。
 * @param  无
 * @retval 拨码值（0~15）
 */
uint8_t DIP_Switch_Read(void)
{
    uint8_t val = 0U;

    /* ON(接地,读 0) 记为 1；OFF(悬空上拉,读 1) 记为 0 */
    if (HAL_GPIO_ReadPin(DIP_BIT0_GPIO_Port, DIP_BIT0_Pin) == GPIO_PIN_RESET) { val |= (1U << 0); }
    if (HAL_GPIO_ReadPin(DIP_BIT1_GPIO_Port, DIP_BIT1_Pin) == GPIO_PIN_RESET) { val |= (1U << 1); }
    if (HAL_GPIO_ReadPin(DIP_BIT2_GPIO_Port, DIP_BIT2_Pin) == GPIO_PIN_RESET) { val |= (1U << 2); }
    if (HAL_GPIO_ReadPin(DIP_BIT3_GPIO_Port, DIP_BIT3_Pin) == GPIO_PIN_RESET) { val |= (1U << 3); }

    return val;
}
