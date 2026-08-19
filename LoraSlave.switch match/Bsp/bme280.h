/**
 * @file    bme280.h
 * @brief   BME280 温湿度气压传感器驱动 (STM32 HAL I2C)
 * @note    适配 STM32F103C8T6，I2C2: PB10-SCL, PB11-SDA
 */

#ifndef __BME280_H__
#define __BME280_H__

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ==================== I2C 地址 ==================== */
#define BME280_I2C_ADDR_PRIM   0x76  /* SDO 接 GND */
#define BME280_I2C_ADDR_SEC    0x77  /* SDO 接 VDD */

/* ==================== 寄存器映射 ==================== */
#define BME280_REG_DIG_T1      0x88  /* 温度校准 (unsigned short) */
#define BME280_REG_DIG_T2      0x8A  /* 温度校准 (signed short)   */
#define BME280_REG_DIG_T3      0x8C  /* 温度校准 (signed short)   */
#define BME280_REG_DIG_P1      0x8E  /* 气压校准 (unsigned short) */
#define BME280_REG_DIG_P2      0x90  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P3      0x92  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P4      0x94  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P5      0x96  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P6      0x98  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P7      0x9A  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P8      0x9C  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_P9      0x9E  /* 气压校准 (signed short)   */
#define BME280_REG_DIG_H1      0xA1  /* 湿度校准 (unsigned char)  */
#define BME280_REG_DIG_H2      0xE1  /* 湿度校准 (signed short)   */
#define BME280_REG_DIG_H3      0xE3  /* 湿度校准 (unsigned char)  */
#define BME280_REG_DIG_H4_H5   0xE4  /* 湿度校准 H4(12bit) H5(12bit) */
#define BME280_REG_DIG_H6      0xE7  /* 湿度校准 (signed char)    */

#define BME280_REG_CHIPID      0xD0  /* 芯片 ID，应为 0x60        */
#define BME280_REG_SOFTRESET   0xE0  /* 软复位，写 0xB6           */
#define BME280_REG_CTRL_HUM    0xF2  /* 湿度 oversampling 控制    */
#define BME280_REG_STATUS      0xF3  /* 状态寄存器                */
#define BME280_REG_CTRL_MEAS   0xF4  /* 测量控制                  */
#define BME280_REG_CONFIG      0xF5  /* 配置 (IIR filter, t_sb)  */
#define BME280_REG_PRESS_MSB   0xF7  /* 气压 MSB                  */
#define BME280_REG_PRESS_LSB   0xF8  /* 气压 LSB                  */
#define BME280_REG_PRESS_XLSB  0xF9  /* 气压 XLSB                 */
#define BME280_REG_TEMP_MSB    0xFA  /* 温度 MSB                  */
#define BME280_REG_TEMP_LSB    0xFB  /* 温度 LSB                  */
#define BME280_REG_TEMP_XLSB   0xFC  /* 温度 XLSB                 */
#define BME280_REG_HUM_MSB     0xFD  /* 湿度 MSB                  */
#define BME280_REG_HUM_LSB     0xFE  /* 湿度 LSB                  */

/* ==================== 模式 / 采样 / 滤波器枚举 ==================== */

/** 传感器工作模式 */
typedef enum {
    BME280_MODE_SLEEP  = 0x00,  /* 休眠   */
    BME280_MODE_FORCED = 0x01,  /* 单次   (推荐，读完自动回 sleep) */
    BME280_MODE_NORMAL = 0x03   /* 连续   */
} BME280_Mode;

/** 过采样率 (osrs) — 越高精度越高、功耗越大 */
typedef enum {
    BME280_OVERSAMPLING_SKIP = 0x00,  /* 跳过 (湿度/气压可跳过)      */
    BME280_OVERSAMPLING_X1   = 0x01,  /* ×1                          */
    BME280_OVERSAMPLING_X2   = 0x02,  /* ×2                          */
    BME280_OVERSAMPLING_X4   = 0x03,  /* ×4                          */
    BME280_OVERSAMPLING_X8   = 0x04,  /* ×8                          */
    BME280_OVERSAMPLING_X16  = 0x05   /* ×16                         */
} BME280_Oversampling;

/** IIR 滤波器系数 */
typedef enum {
    BME280_FILTER_OFF = 0x00,
    BME280_FILTER_X2  = 0x01,
    BME280_FILTER_X4  = 0x02,
    BME280_FILTER_X8  = 0x03,
    BME280_FILTER_X16 = 0x04
} BME280_Filter;

/** 正常模式下的待机时间 (t_sb)，仅 MODE_NORMAL 有效 */
typedef enum {
    BME280_STANDBY_0_5ms  = 0x00,
    BME280_STANDBY_62_5ms = 0x01,
    BME280_STANDBY_125ms  = 0x02,
    BME280_STANDBY_250ms  = 0x03,
    BME280_STANDBY_500ms  = 0x04,
    BME280_STANDBY_1000ms = 0x05,
    BME280_STANDBY_10ms   = 0x06,
    BME280_STANDBY_20ms   = 0x07
} BME280_StandbyTime;

/** BME280 句柄 */
typedef struct {
    I2C_HandleTypeDef *hi2c;             /* HAL I2C 句柄               */
    uint8_t            i2c_addr;         /* 7-bit I2C 地址             */

    /* 校准数据 (芯片出厂固化，不可改) */
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1, dig_H3;
    int16_t  dig_H2;
    int8_t   dig_H4_msb, dig_H4_lsb, dig_H5_msb, dig_H5_lsb, dig_H6;

    int32_t  t_fine;                    /* 温度精细值 (补偿中间量) */
} BME280_HandleTypeDef;

/* ==================== 公开 API ==================== */

/**
 * @brief  初始化 BME280
 * @param  bme        BME280 句柄指针
 * @param  hi2c       HAL I2C 句柄指针
 * @param  i2c_addr   I2C 地址 (0x76 或 0x77)
 * @return HAL_OK / HAL_ERROR
 */
HAL_StatusTypeDef BME280_Init(BME280_HandleTypeDef *bme,
                              I2C_HandleTypeDef   *hi2c,
                              uint8_t              i2c_addr);

/**
 * @brief  配置传感器参数并进入强制模式
 * @param  bme       BME280 句柄指针
 * @param  osrs_t    温度过采样
 * @param  osrs_p    气压过采样
 * @param  osrs_h    湿度过采样
 * @param  filter    IIR 滤波器系数
 * @return HAL_OK / HAL_ERROR
 */
HAL_StatusTypeDef BME280_Config(BME280_HandleTypeDef *bme,
                                BME280_Oversampling    osrs_t,
                                BME280_Oversampling    osrs_p,
                                BME280_Oversampling    osrs_h,
                                BME280_Filter          filter);

/**
 * @brief  触发一次测量 (FORCED mode)，阻塞等待完成后读取数据
 * @param  bme          BME280 句柄指针
 * @param  temperature  输出温度 (×100, 单位 °C)
 * @param  pressure     输出气压 (Pa)
 * @param  humidity     输出湿度 (×1024, 单位 %RH)
 * @return HAL_OK / HAL_ERROR
 * @note   温度精度 ±0.01°C，气压精度 1Pa，湿度精度 ~0.008%RH(×1024 时)
 */
HAL_StatusTypeDef BME280_ReadAll(BME280_HandleTypeDef *bme,
                                 int32_t *temperature,
                                 uint32_t *pressure,
                                 uint32_t *humidity);

/**
 * @brief  软复位传感器
 */
HAL_StatusTypeDef BME280_SoftReset(BME280_HandleTypeDef *bme);

/* ==================== 底层 I2C 操作 (如果 HAL 不同可替换) ==================== */
HAL_StatusTypeDef BME280_WriteReg(BME280_HandleTypeDef *bme,
                                  uint8_t reg, uint8_t val);
HAL_StatusTypeDef BME280_ReadRegs(BME280_HandleTypeDef *bme,
                                  uint8_t reg, uint8_t *buf, uint8_t len);

#endif /* __BME280_H__ */
