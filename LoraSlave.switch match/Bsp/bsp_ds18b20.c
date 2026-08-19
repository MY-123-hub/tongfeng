#include "bsp_ds18b20.h"
#include "core_delay.h"
#include "stdio.h"

unsigned char DS18B20_ID[6][MaxSensorNum][8];
unsigned char DS18B20_SensorNum[6];

/* ==================== 1-Wire GPIO 模式切换 ==================== */

void DS18B20_GPIO_Config(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = PINx;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
}

void DS18B20_Mode_IPU(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = PINx;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

void DS18B20_Mode_Out(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = PINx;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

/* 推挽输出（强上拉）：GXHT3W 纯寄生供电，转换期间需强上拉 DQ 供电 */
void DS18B20_Mode_Out_PP(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = PINx;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}

/* ==================== 底层时序 ==================== */

/* 复位：拉低 480us，释放后等待从机应答 */
void DS18B20_Rst(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    DS18B20_Mode_Out(GPIOx, PINx);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_RESET);   /* 拉低 480us 复位信号 */
    DS18B20_DELAY_US(480);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);     /* 释放总线 */
    DS18B20_DELAY_US(15);
}

/* 等待从机应答脉冲（15~60us 后出现 60~240us 低电平），超时返回 1 */
uint8_t DS18B20_Answer_Check(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    uint8_t delay = 0;
    DS18B20_Mode_IPU(GPIOx, PINx);

    /* 等总线释放（从机拉低），超 100us 无应答则失败 */
    while (HAL_GPIO_ReadPin(GPIOx, PINx) && delay < 100)
    {
        delay++;
        DS18B20_DELAY_US(1);
    }
    if (delay >= 100)
        return 1;
    else
        delay = 0;

    /* 等应答低电平结束，超 240us 则失败 */
    while (!HAL_GPIO_ReadPin(GPIOx, PINx) && delay < 240)
    {
        delay++;
        DS18B20_DELAY_US(1);
    }
    if (delay >= 240)
        return 1;
    return 0;
}

/* 读 1 个 bit */
uint8_t DS18B20_Read_Bit(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    uint8_t data = 0;
    DS18B20_Mode_Out(GPIOx, PINx);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_RESET);   /* 拉低 2us 启动读时隙 */
    DS18B20_DELAY_US(2);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
    DS18B20_Mode_IPU(GPIOx, PINx);
    DS18B20_DELAY_US(12);                             /* 释放 12us 后采样 */
    if (HAL_GPIO_ReadPin(GPIOx, PINx))
        data = 1;
    else
        data = 0;
    DS18B20_DELAY_US(50);
    return data;
}

/* 读 2 个 bit（Search ROM 用，返回两位原码+反码的合并值） */
uint8_t DS18B20_Read_2Bit(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    uint8_t i;
    uint8_t dat = 0;
    for (i = 2; i > 0; i--)
    {
        dat = dat << 1;
        DS18B20_Mode_Out(GPIOx, PINx);
        HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_RESET);
        DS18B20_DELAY_US(2);
        HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
        DS18B20_Mode_IPU(GPIOx, PINx);
        DS18B20_DELAY_US(12);
        if (HAL_GPIO_ReadPin(GPIOx, PINx)) dat |= 0x01;
        DS18B20_DELAY_US(50);
    }
    return dat;
}

/* 读 1 字节（LSB 先出） */
uint8_t DS18B20_Read_Byte(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    uint8_t i, j, dat;
    dat = 0;
    for (i = 0; i < 8; i++)
    {
        j = DS18B20_Read_Bit(GPIOx, PINx);
        dat = (dat) | (j << i);
    }
    return dat;
}

/* 写 1 个 bit */
void DS18B20_Write_Bit(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t dat)
{
    DS18B20_Mode_Out(GPIOx, PINx);
    if (dat)
    {
        HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_RESET);   /* 写 1：拉低 2us 再释放 */
        DS18B20_DELAY_US(2);
        HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
        DS18B20_DELAY_US(60);
    }
    else
    {
        HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_RESET);   /* 写 0：拉低 60us */
        DS18B20_DELAY_US(60);
        HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
        DS18B20_DELAY_US(2);
    }
}

/* 写 1 字节（LSB 先出） */
void DS18B20_Write_Byte(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t dat)
{
    uint8_t j;
    for (j = 0; j < 8; j++)
    {
        DS18B20_Write_Bit(GPIOx, PINx, dat & 0x01);
        dat >>= 1;
    }
}

uint8_t DS18B20_Init(GPIO_TypeDef *GPIOx, uint16_t PINx)
{
    DS18B20_GPIO_Config(GPIOx, PINx);
    DS18B20_Rst(GPIOx, PINx);
    return DS18B20_Answer_Check(GPIOx, PINx);
}

/* ==================== 非阻塞采样：启动转换 / 读取结果 ==================== */

/* 启动 DS18B20 温度转换（Match ROM + 0x44），随后释放总线；750ms 等待由主循环状态机控制 */
void DS18B20_Start_Convert(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel, uint8_t idx)
{
    uint8_t j;
    DS18B20_Rst(GPIOx, PINx);
    DS18B20_Answer_Check(GPIOx, PINx);
    DS18B20_Write_Byte(GPIOx, PINx, 0x55);          /* Match ROM */
    for (j = 0; j < 8; j++) DS18B20_Write_Byte(GPIOx, PINx, DS18B20_ID[channel][idx][j]);
    DS18B20_Write_Byte(GPIOx, PINx, 0x44);          /* Convert T */
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);   /* 释放总线 */
}

/* 读取 DS18B20 温度（Match ROM + 0xBE），CRC 校验，失败返回 -85 哨兵 */
float DS18B20_Read_Temp(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel, uint8_t idx)
{
    uint8_t j, TL, TH;
    uint8_t crc_t[10];
    short Temperature;
    float Temperature1;

    DS18B20_Rst(GPIOx, PINx);
    DS18B20_Answer_Check(GPIOx, PINx);
    DS18B20_Write_Byte(GPIOx, PINx, 0x55);          /* Match ROM */
    for (j = 0; j < 8; j++) DS18B20_Write_Byte(GPIOx, PINx, DS18B20_ID[channel][idx][j]);
    DS18B20_Write_Byte(GPIOx, PINx, 0xBE);          /* Read Scratchpad */

    TL = DS18B20_Read_Byte(GPIOx, PINx);            /* byte0: 温度 LSB */
    TH = DS18B20_Read_Byte(GPIOx, PINx);            /* byte1: 温度 MSB */
    crc_t[0] = TL;
    crc_t[1] = TH;
    crc_t[2] = DS18B20_Read_Byte(GPIOx, PINx);
    crc_t[3] = DS18B20_Read_Byte(GPIOx, PINx);
    crc_t[4] = DS18B20_Read_Byte(GPIOx, PINx);
    crc_t[5] = DS18B20_Read_Byte(GPIOx, PINx);
    crc_t[6] = DS18B20_Read_Byte(GPIOx, PINx);
    crc_t[7] = DS18B20_Read_Byte(GPIOx, PINx);
    crc_t[8] = DS18B20_Read_Byte(GPIOx, PINx);      /* byte8: 器件 CRC */
    crc_t[9] = DS18B20_Crc(crc_t, 8);               /* 计算前 8 字节 CRC */
    DS18B20_Rst(GPIOx, PINx);                        /* 复位结束读取 */
    DS18B20_Answer_Check(GPIOx, PINx);

    if (crc_t[9] != crc_t[8]) return -85.0f;         /* CRC 失败 → 无效哨兵(main 当 0) */
    Temperature = (short)((TH << 8) | TL);           /* 16 位补码，负数按符号解释 */
    Temperature1 = Temperature * 0.0625f;
    return Temperature1;
}

/* 启动 GXHT3W 转换（Match ROM + 0x44）；复位后用固定延时兜底应答（GXHT3W 应答时序不同） */
void GXHT3W_Start_Convert(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel, uint8_t idx)
{
    uint8_t j;
    __disable_irq();
    DS18B20_Rst(GPIOx, PINx);
    DS18B20_DELAY_US(480);
    DS18B20_Write_Byte(GPIOx, PINx, 0x55);          /* Match ROM */
    for (j = 0; j < 8; j++) DS18B20_Write_Byte(GPIOx, PINx, DS18B20_ID[channel][idx][j]);
    DS18B20_Write_Byte(GPIOx, PINx, 0x44);          /* Convert */
    __enable_irq();
    /* 不强上拉：由主循环 CONVERT 阶段末尾统一强上拉（寄生供电设备需要） */
}

/* 读取 GXHT3W 温湿度（Match ROM + 0xBE），先切回开漏结束强上拉 */
void GXHT3W_Read_TempHum(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel, uint8_t idx, float *temp, float *hum)
{
    uint8_t buf[9], j;
    uint16_t hum_raw;

    *temp = 0;
    *hum = 0;

    DS18B20_Mode_Out(GPIOx, PINx);   /* 结束强上拉，切回开漏 */

    __disable_irq();
    DS18B20_Rst(GPIOx, PINx);
    DS18B20_DELAY_US(480);
    DS18B20_Write_Byte(GPIOx, PINx, 0x55);          /* Match ROM */
    for (j = 0; j < 8; j++) DS18B20_Write_Byte(GPIOx, PINx, DS18B20_ID[channel][idx][j]);
    DS18B20_Write_Byte(GPIOx, PINx, 0xBE);          /* Read Scratchpad */
    for (j = 0; j < 9; j++) buf[j] = DS18B20_Read_Byte(GPIOx, PINx);
    __enable_irq();

    if (DS18B20_Crc(buf, 8) != buf[8])
    {
#if DEBUG_LOG
        printf("[CRC] id=%02X%02X%02X%02X%02X%02X%02X%02X buf=%02X..%02X calc=%02X\r\n",
               DS18B20_ID[channel][idx][0], DS18B20_ID[channel][idx][1],
               DS18B20_ID[channel][idx][2], DS18B20_ID[channel][idx][3],
               DS18B20_ID[channel][idx][4], DS18B20_ID[channel][idx][5],
               DS18B20_ID[channel][idx][6], DS18B20_ID[channel][idx][7],
               buf[0], buf[8], DS18B20_Crc(buf, 8));
#endif
        return;
    }

#if DEBUG_LOG
    printf("[BUF] %02X %02X %02X %02X %02X %02X %02X %02X | %02X\r\n",
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
#endif

    /* GXHT3W 寄存器布局：
       温度 Byte0(LSB)/Byte1(MSB)，12bit 有效，T = -45 + 175*S/4095
       湿度 Byte6(MSB)/Byte7(LSB)，16bit，RH = 100*S/65535 */
    {
        uint16_t raw = (uint16_t)(((buf[1] << 8) | buf[0]) & 0x0FFF);
        *temp = 175.0f * (float)raw / 4095.0f - 45.0f;
        hum_raw = (uint16_t)((buf[6] << 8) | buf[7]);
        *hum = 100.0f * (float)hum_raw / 65535.0f;
    }
}

/* ==================== Search ROM / CRC ==================== */

/* 自动搜索总线上的 ROM 序列号 */
void DS18B20_Search_Rom(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel)
{
    uint8_t k, l, chongtuwei, m, n, num;
    uint8_t zhan[MaxSensorNum - 1] = {0};
    uint8_t ss[64];
    uint8_t tempp;
    l = 0;
    num = 0;
    do
    {
        DS18B20_Rst(GPIOx, PINx);
        DS18B20_DELAY_US(480);
        DS18B20_Write_Byte(GPIOx, PINx, 0xf0);          /* Search ROM */
        for (m = 0; m < 8; m++)
        {
            uint8_t s = 0;
            for (n = 0; n < 8; n++)
            {
                k = DS18B20_Read_2Bit(GPIOx, PINx);     /* 读两位（原码+反码） */
                k = k & 0x03;
                s >>= 1;
                if (k == 0x01)                          /* 01：该位为 0 */
                {
                    DS18B20_Write_Bit(GPIOx, PINx, 0);
                    ss[(m * 8 + n)] = 0;
                }
                else if (k == 0x02)                     /* 10：该位为 1 */
                {
                    s = s | 0x80;
                    DS18B20_Write_Bit(GPIOx, PINx, 1);
                    ss[(m * 8 + n)] = 1;
                }
                else if (k == 0x00)                     /* 00：冲突位 */
                {
                    chongtuwei = m * 8 + n + 1;
                    if (chongtuwei > zhan[l])
                    {
                        DS18B20_Write_Bit(GPIOx, PINx, 0);
                        ss[(m * 8 + n)] = 0;
                        zhan[++l] = chongtuwei;
                    }
                    else if (chongtuwei < zhan[l])
                    {
                        s = s | ((ss[(m * 8 + n)] & 0x01) << 7);
                        DS18B20_Write_Bit(GPIOx, PINx, ss[(m * 8 + n)]);
                    }
                    else if (chongtuwei == zhan[l])
                    {
                        s = s | 0x80;
                        DS18B20_Write_Bit(GPIOx, PINx, 1);
                        ss[(m * 8 + n)] = 1;
                        l = l - 1;
                    }
                }
                else
                {
                    /* 11：无器件 */
                }
            }
            tempp = s;
            DS18B20_ID[channel][num][m] = tempp;        /* 保存一字节 ID */
        }
        num = num + 1;                                  /* 器件数 +1 */
    } while (zhan[l] != 0 && (num < MaxSensorNum));
    DS18B20_SensorNum[channel] = num;
}

/* CRC-8/MAXIM：x^8 + x^5 + x^4 + 1，初值 0 */
uint8_t DS18B20_Crc(uint8_t *src, uint8_t size)
{
    uint8_t ret = 0;
    uint8_t *p;
    int i = 0;
    uint8_t pBuf = 0;
    p = (uint8_t *)src;

    while (size--)
    {
        pBuf = *p++;
        for (i = 0; i < 8; i++)
        {
            if ((ret ^ pBuf) & 0x01)
            {
                ret ^= 0x18;
                ret >>= 1;
                ret |= 0x80;
            }
            else
            {
                ret >>= 1;
            }
            pBuf >>= 1;
        }
    }
    return ret;
}
