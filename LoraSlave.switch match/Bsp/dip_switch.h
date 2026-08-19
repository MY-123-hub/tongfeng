#ifndef __DIP_SWITCH_H__
#define __DIP_SWITCH_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* ===== 拨码开关引脚定义 =====
 * 4 位拨码，独立 IO 读取，接法：一端接 GND，MCU 内部上拉。
 *   bit0(最低位) = PB8
 *   bit1         = PB9
 *   bit2         = PB12
 *   bit3(最高位) = PB13
 * 拨 ON  = 接地 = 硬件读 0；本模块已取反，DIP_Switch_Read 返回「ON=1、OFF=0」的逻辑值。
 */
#define DIP_BIT0_Pin        GPIO_PIN_8
#define DIP_BIT0_GPIO_Port  GPIOB
#define DIP_BIT1_Pin        GPIO_PIN_9
#define DIP_BIT1_GPIO_Port  GPIOB
#define DIP_BIT2_Pin        GPIO_PIN_12
#define DIP_BIT2_GPIO_Port  GPIOB
#define DIP_BIT3_Pin        GPIO_PIN_13
#define DIP_BIT3_GPIO_Port  GPIOB

void DIP_Switch_Init(void);
uint8_t DIP_Switch_Read(void);
void DIP_SendHexViaLoRa(uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* __DIP_SWITCH_H__ */
