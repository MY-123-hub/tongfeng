/**
 * @file    bme280.c
 * @brief   BME280 驱动实现 — Bosch 补偿公式
 */

#include "bme280.h"

/* ==================== 底层 I2C 读写 ==================== */

HAL_StatusTypeDef BME280_WriteReg(BME280_HandleTypeDef *bme,
                                  uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(bme->hi2c,
                             (uint16_t)(bme->i2c_addr << 1),
                             reg, I2C_MEMADD_SIZE_8BIT,
                             &val, 1, 100);
}

HAL_StatusTypeDef BME280_ReadRegs(BME280_HandleTypeDef *bme,
                                  uint8_t reg, uint8_t *buf, uint8_t len)
{
    return HAL_I2C_Mem_Read(bme->hi2c,
                            (uint16_t)(bme->i2c_addr << 1),
                            reg, I2C_MEMADD_SIZE_8BIT,
                            buf, len, 100);
}

/* ==================== 初始化 ==================== */

HAL_StatusTypeDef BME280_Init(BME280_HandleTypeDef *bme,
                              I2C_HandleTypeDef   *hi2c,
                              uint8_t              i2c_addr)
{
    uint8_t chip_id = 0;

    bme->hi2c     = hi2c;
    bme->i2c_addr = i2c_addr;
    bme->t_fine   = 0;

    /* 读取 Chip ID 验证通信 */
    if (BME280_ReadRegs(bme, BME280_REG_CHIPID, &chip_id, 1) != HAL_OK) {
        return HAL_ERROR;
    }
    if (chip_id != 0x60 && chip_id != 0x58) {
        return HAL_ERROR;  /* 不是 BME280/BMP280 */
    }

    /* 软复位 */
    BME280_SoftReset(bme);
    HAL_Delay(10);

    /* ---- 读取校准数据 ---- */
    uint8_t calib[26]; /* 0x88..0xA1 = 26 bytes */
    BME280_ReadRegs(bme, BME280_REG_DIG_T1, calib, 26);

    bme->dig_T1 = (uint16_t)(calib[0]  | (calib[1]  << 8));
    bme->dig_T2 = (int16_t) (calib[2]  | (calib[3]  << 8));
    bme->dig_T3 = (int16_t) (calib[4]  | (calib[5]  << 8));
    bme->dig_P1 = (uint16_t)(calib[6]  | (calib[7]  << 8));
    bme->dig_P2 = (int16_t) (calib[8]  | (calib[9]  << 8));
    bme->dig_P3 = (int16_t) (calib[10] | (calib[11] << 8));
    bme->dig_P4 = (int16_t) (calib[12] | (calib[13] << 8));
    bme->dig_P5 = (int16_t) (calib[14] | (calib[15] << 8));
    bme->dig_P6 = (int16_t) (calib[16] | (calib[17] << 8));
    bme->dig_P7 = (int16_t) (calib[18] | (calib[19] << 8));
    bme->dig_P8 = (int16_t) (calib[20] | (calib[21] << 8));
    bme->dig_P9 = (int16_t) (calib[22] | (calib[23] << 8));
    bme->dig_H1 = calib[25]; /* 0xA1 */

    /* 湿度校准 H2..H6 (不连续区域) */
    uint8_t calib_h[7]; /* 0xE1..0xE7 = 7 bytes */
    BME280_ReadRegs(bme, BME280_REG_DIG_H2, calib_h, 7);

    bme->dig_H2 = (int16_t)(calib_h[0] | (calib_h[1] << 8));
    bme->dig_H3 = calib_h[2];

    /* H4 & H5 是跨字节拼接的 12-bit 有符号 */
    bme->dig_H4_msb = (int8_t)calib_h[3];
    bme->dig_H4_lsb = (int8_t)(calib_h[4] & 0x0F);
    bme->dig_H5_msb = (int8_t)calib_h[5];
    bme->dig_H5_lsb = (int8_t)((calib_h[4] >> 4) & 0x0F);
    bme->dig_H6     = (int8_t)calib_h[6];

    return HAL_OK;
}

/* ==================== 软复位 ==================== */

HAL_StatusTypeDef BME280_SoftReset(BME280_HandleTypeDef *bme)
{
    return BME280_WriteReg(bme, BME280_REG_SOFTRESET, 0xB6);
}

/* ==================== 传感器配置 ==================== */

HAL_StatusTypeDef BME280_Config(BME280_HandleTypeDef *bme,
                                BME280_Oversampling    osrs_t,
                                BME280_Oversampling    osrs_p,
                                BME280_Oversampling    osrs_h,
                                BME280_Filter          filter)
{
    uint8_t val;

    /* ctrl_hum: osrs_h[2:0] */
    val = (uint8_t)(osrs_h & 0x07);
    if (BME280_WriteReg(bme, BME280_REG_CTRL_HUM, val) != HAL_OK)
        return HAL_ERROR;

    /* config: t_sb[7:5]=000(forced 下无关), filter[4:2], spi3w_en[0]=0 */
    val = (uint8_t)((filter & 0x07) << 2);
    if (BME280_WriteReg(bme, BME280_REG_CONFIG, val) != HAL_OK)
        return HAL_ERROR;

    /* ctrl_meas: osrs_t[7:5], osrs_p[4:2], mode[1:0] — 先写 sleep 再切 forced */
    val = (uint8_t)(((osrs_t & 0x07) << 5) |
                    ((osrs_p & 0x07) << 2) |
                    BME280_MODE_SLEEP);
    if (BME280_WriteReg(bme, BME280_REG_CTRL_MEAS, val) != HAL_OK)
        return HAL_ERROR;

    return HAL_OK;
}

/* ==================== 读取原始数据 ==================== */

static HAL_StatusTypeDef BME280_ReadRaw(BME280_HandleTypeDef *bme,
                                        int32_t *adc_T,
                                        int32_t *adc_P,
                                        int32_t *adc_H)
{
    uint8_t raw[8];
    HAL_StatusTypeDef ret;

    /* 1. 设置 forced 模式触发测量 */
    uint8_t ctrl = 0;
    ret = BME280_ReadRegs(bme, BME280_REG_CTRL_MEAS, &ctrl, 1);
    if (ret != HAL_OK) return ret;

    ctrl &= 0xFC;           /* 清 mode bits */
    ctrl |= BME280_MODE_FORCED;
    ret = BME280_WriteReg(bme, BME280_REG_CTRL_MEAS, ctrl);
    if (ret != HAL_OK) return ret;

    /* 2. 等待测量完成 (status[3]=measuring, status[0]=im_update) */
    uint8_t status;
    do {
        HAL_Delay(1);
        BME280_ReadRegs(bme, BME280_REG_STATUS, &status, 1);
    } while (status & 0x08);  /* measuring bit */

    /* 3. 读取 8 字节数据: P[0:2] T[3:5] H[6:7] */
    ret = BME280_ReadRegs(bme, BME280_REG_PRESS_MSB, raw, 8);
    if (ret != HAL_OK) return ret;

    *adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    *adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    *adc_H = ((int32_t)raw[6] << 8)  |  (int32_t)raw[7];

    return HAL_OK;
}

/* ==================== 补偿公式 (Bosch datasheet) ==================== */

/**
 * @brief 温度补偿，返回 t_fine 和 ×100 的温度值
 */
static int32_t BME280_Compensate_T(BME280_HandleTypeDef *bme, int32_t adc_T)
{
    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)bme->dig_T1 << 1))) *
            ((int32_t)bme->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)bme->dig_T1)) *
              ((adc_T >> 4) - ((int32_t)bme->dig_T1))) >> 12) *
            ((int32_t)bme->dig_T3)) >> 14;
    bme->t_fine = var1 + var2;
    T = (bme->t_fine * 5 + 128) >> 8;   /* °C × 100 */
    return T;
}

/**
 * @brief 气压补偿，返回 Pa (uint32, 精度 1 Pa)
 */
static uint32_t BME280_Compensate_P(BME280_HandleTypeDef *bme, int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)bme->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)bme->dig_P6;
    var2 = var2 + ((var1 * (int64_t)bme->dig_P5) << 17);
    var2 = var2 + (((int64_t)bme->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)bme->dig_P3) >> 8) +
           ((var1 * (int64_t)bme->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)bme->dig_P1) >> 33;

    if (var1 == 0) return 0;

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)bme->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)bme->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)bme->dig_P7) << 4);

    return (uint32_t)(p >> 8);   /* Q24.8 (Pa*256) -> Pa */
}

/**
 * @brief 湿度补偿 (Bosch BME280 datasheet §4.2.3)，返回 %RH × 1024
 */
static uint32_t BME280_Compensate_H(BME280_HandleTypeDef *bme, int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = bme->t_fine - (int32_t)76800;

    /* 还原 H4 / H5 有符号值 (12-bit 有符号 → 16-bit，需手动符号扩展) */
    int16_t h4 = (int16_t)((uint8_t)bme->dig_H4_msb << 4 | (bme->dig_H4_lsb & 0x0F));
    int16_t h5 = (int16_t)((uint8_t)bme->dig_H5_msb << 4 | (bme->dig_H5_lsb & 0x0F));
    if (h4 > 0x07FF) h4 -= 0x1000;
    if (h5 > 0x07FF) h5 -= 0x1000;

    int32_t var_H;

    var_H = (((((int32_t)adc_H << 14) - ((int32_t)h4 << 20) - ((int32_t)h5 * v_x1_u32r))
              + (int32_t)16384) >> 15)
          * (((((((v_x1_u32r * (int32_t)bme->dig_H6) >> 10)
               * (((v_x1_u32r * (int32_t)bme->dig_H3) >> 11) + (int32_t)32768)) >> 10)
             + (int32_t)2097152) * (int32_t)bme->dig_H2 + 8192) >> 14);

    var_H = var_H - (((((var_H >> 15) * (var_H >> 15)) >> 7)
                     * (int32_t)bme->dig_H1) >> 4);

    var_H = (var_H < 0) ? 0 : var_H;
    var_H = (var_H > 419430400) ? 419430400 : var_H;

    return (uint32_t)(var_H >> 12);
}

/* ==================== 公开读取 API ==================== */

HAL_StatusTypeDef BME280_ReadAll(BME280_HandleTypeDef *bme,
                                 int32_t   *temperature,
                                 uint32_t  *pressure,
                                 uint32_t  *humidity)
{
    int32_t adc_T, adc_P, adc_H;

    if (BME280_ReadRaw(bme, &adc_T, &adc_P, &adc_H) != HAL_OK)
        return HAL_ERROR;

    *temperature = BME280_Compensate_T(bme, adc_T);
    *pressure    = BME280_Compensate_P(bme, adc_P);
    *humidity    = BME280_Compensate_H(bme, adc_H);

    return HAL_OK;
}
