#include "BME280.h"

#include "main.h"

#include <string.h>

/*
 * Sensor register handling and compensation follow Bosch BME280 data sheet
 * revision 1.23. The software-I2C transport is project-specific because
 * PB14/PB15 are not hardware I2C pins on STM32F103C8T6.
 */

#define BME280_REG_CALIB_TP            (0x88U)
#define BME280_REG_CALIB_H1            (0xA1U)
#define BME280_REG_CHIP_ID             (0xD0U)
#define BME280_REG_RESET               (0xE0U)
#define BME280_REG_CALIB_H             (0xE1U)
#define BME280_REG_CTRL_HUM            (0xF2U)
#define BME280_REG_STATUS              (0xF3U)
#define BME280_REG_CTRL_MEAS           (0xF4U)
#define BME280_REG_CONFIG              (0xF5U)
#define BME280_REG_DATA                (0xF7U)

#define BME280_RESET_COMMAND           (0xB6U)
#define BME280_STATUS_MEASURING        (0x08U)
#define BME280_STATUS_IM_UPDATE        (0x01U)
#define BME280_MODE_FORCED             (0x01U)

#define BME280_SCL_PORT                BME280_SCL_GPIO_Port
#define BME280_SCL_PIN                 BME280_SCL_Pin
#define BME280_SDA_PORT                BME280_SDA_GPIO_Port
#define BME280_SDA_PIN                 BME280_SDA_Pin

#define BME280_I2C_HALF_PERIOD_US      (5U)
#define BME280_I2C_SCL_TIMEOUT_US      (200U)

static uint8_t g_dwt_ready;

static void BME280_DelayUs(uint32_t period_us)
{
    uint32_t cycles_per_us;
    uint32_t start;
    uint32_t required_cycles;

    if (g_dwt_ready == 0U)
    {
        volatile uint32_t fallback;

        while (period_us-- != 0U)
        {
            fallback = SystemCoreClock / 6000000U;
            while (fallback-- != 0U)
            {
                __NOP();
            }
        }
        return;
    }

    cycles_per_us = SystemCoreClock / 1000000U;
    required_cycles = cycles_per_us * period_us;
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < required_cycles)
    {
        /* Busy wait is limited to software-I2C bit timing. */
    }
}

static void BME280_SclLow(void)
{
    HAL_GPIO_WritePin(BME280_SCL_PORT, BME280_SCL_PIN, GPIO_PIN_RESET);
}

static void BME280_SclRelease(void)
{
    HAL_GPIO_WritePin(BME280_SCL_PORT, BME280_SCL_PIN, GPIO_PIN_SET);
}

static void BME280_SdaLow(void)
{
    HAL_GPIO_WritePin(BME280_SDA_PORT, BME280_SDA_PIN, GPIO_PIN_RESET);
}

static void BME280_SdaRelease(void)
{
    HAL_GPIO_WritePin(BME280_SDA_PORT, BME280_SDA_PIN, GPIO_PIN_SET);
}

static GPIO_PinState BME280_ReadScl(void)
{
    return HAL_GPIO_ReadPin(BME280_SCL_PORT, BME280_SCL_PIN);
}

static GPIO_PinState BME280_ReadSda(void)
{
    return HAL_GPIO_ReadPin(BME280_SDA_PORT, BME280_SDA_PIN);
}

static BME280_Status BME280_WaitSclHigh(void)
{
    uint32_t elapsed_us = 0U;

    BME280_SclRelease();
    while (BME280_ReadScl() == GPIO_PIN_RESET)
    {
        if (elapsed_us >= BME280_I2C_SCL_TIMEOUT_US)
        {
            return BME280_ERROR_TIMEOUT;
        }
        BME280_DelayUs(1U);
        elapsed_us++;
    }
    return BME280_OK;
}

static void BME280_I2CStop(void)
{
    BME280_SdaLow();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SclRelease();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SdaRelease();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
}

static BME280_Status BME280_RecoverBus(void)
{
    uint8_t pulse;

    BME280_SdaRelease();
    for (pulse = 0U; pulse < 9U; pulse++)
    {
        BME280_SclLow();
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
        if (BME280_WaitSclHigh() != BME280_OK)
        {
            return BME280_ERROR_TIMEOUT;
        }
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    }
    BME280_I2CStop();

    return ((BME280_ReadScl() == GPIO_PIN_SET) &&
            (BME280_ReadSda() == GPIO_PIN_SET)) ?
           BME280_OK : BME280_ERROR_BUS;
}

static BME280_Status BME280_I2CStart(void)
{
    BME280_Status status;

    BME280_SdaRelease();
    status = BME280_WaitSclHigh();
    if (status != BME280_OK)
    {
        return status;
    }
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);

    if (BME280_ReadSda() == GPIO_PIN_RESET)
    {
        status = BME280_RecoverBus();
        if (status != BME280_OK)
        {
            return status;
        }
        status = BME280_WaitSclHigh();
        if (status != BME280_OK)
        {
            return status;
        }
    }

    BME280_SdaLow();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SclLow();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    return BME280_OK;
}

static BME280_Status BME280_I2CRestart(void)
{
    BME280_Status status;

    BME280_SdaRelease();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    status = BME280_WaitSclHigh();
    if (status != BME280_OK)
    {
        return status;
    }
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SdaLow();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SclLow();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    return BME280_OK;
}

static BME280_Status BME280_I2CWriteByte(uint8_t value)
{
    uint8_t bit_index;
    BME280_Status status;
    GPIO_PinState acknowledgement;

    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        if ((value & 0x80U) != 0U)
        {
            BME280_SdaRelease();
        }
        else
        {
            BME280_SdaLow();
        }
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
        status = BME280_WaitSclHigh();
        if (status != BME280_OK)
        {
            return status;
        }
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
        BME280_SclLow();
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
        value <<= 1U;
    }

    BME280_SdaRelease();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    status = BME280_WaitSclHigh();
    if (status != BME280_OK)
    {
        return status;
    }
    acknowledgement = BME280_ReadSda();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SclLow();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);

    return (acknowledgement == GPIO_PIN_RESET) ?
           BME280_OK : BME280_ERROR_NO_ACK;
}

static BME280_Status BME280_I2CReadByte(uint8_t *value, uint8_t acknowledge)
{
    uint8_t bit_index;
    BME280_Status status;

    if (value == NULL)
    {
        return BME280_ERROR_ARGUMENT;
    }

    *value = 0U;
    BME280_SdaRelease();
    for (bit_index = 0U; bit_index < 8U; bit_index++)
    {
        *value <<= 1U;
        status = BME280_WaitSclHigh();
        if (status != BME280_OK)
        {
            return status;
        }
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
        if (BME280_ReadSda() == GPIO_PIN_SET)
        {
            *value |= 0x01U;
        }
        BME280_SclLow();
        BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    }

    if (acknowledge != 0U)
    {
        BME280_SdaLow();
    }
    else
    {
        BME280_SdaRelease();
    }
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    status = BME280_WaitSclHigh();
    if (status != BME280_OK)
    {
        return status;
    }
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    BME280_SclLow();
    BME280_SdaRelease();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    return BME280_OK;
}

static BME280_Status BME280_WriteRegs(const BME280_HandleTypeDef *device,
                                      uint8_t register_address,
                                      const uint8_t *data,
                                      uint8_t length)
{
    BME280_Status status;
    uint8_t index;

    if ((device == NULL) || (data == NULL) || (length == 0U))
    {
        return BME280_ERROR_ARGUMENT;
    }

    status = BME280_I2CStart();
    if (status == BME280_OK)
    {
        status = BME280_I2CWriteByte((uint8_t)(device->i2c_address << 1U));
    }
    if (status == BME280_OK)
    {
        status = BME280_I2CWriteByte(register_address);
    }
    for (index = 0U; (index < length) && (status == BME280_OK); index++)
    {
        status = BME280_I2CWriteByte(data[index]);
    }
    BME280_I2CStop();
    return status;
}

static BME280_Status BME280_WriteReg(const BME280_HandleTypeDef *device,
                                     uint8_t register_address,
                                     uint8_t value)
{
    return BME280_WriteRegs(device, register_address, &value, 1U);
}

static BME280_Status BME280_ReadRegs(const BME280_HandleTypeDef *device,
                                     uint8_t register_address,
                                     uint8_t *data,
                                     uint8_t length)
{
    BME280_Status status;
    uint8_t index;

    if ((device == NULL) || (data == NULL) || (length == 0U))
    {
        return BME280_ERROR_ARGUMENT;
    }

    status = BME280_I2CStart();
    if (status == BME280_OK)
    {
        status = BME280_I2CWriteByte((uint8_t)(device->i2c_address << 1U));
    }
    if (status == BME280_OK)
    {
        status = BME280_I2CWriteByte(register_address);
    }
    if (status == BME280_OK)
    {
        status = BME280_I2CRestart();
    }
    if (status == BME280_OK)
    {
        status = BME280_I2CWriteByte((uint8_t)((device->i2c_address << 1U) | 0x01U));
    }
    for (index = 0U; (index < length) && (status == BME280_OK); index++)
    {
        status = BME280_I2CReadByte(&data[index],
                                    (uint8_t)((index + 1U) < length));
    }
    BME280_I2CStop();
    return status;
}

static BME280_Status BME280_ProbeAddress(BME280_HandleTypeDef *device,
                                         uint8_t address)
{
    uint8_t chip_id = 0U;
    BME280_Status status;

    device->i2c_address = address;
    status = BME280_ReadRegs(device, BME280_REG_CHIP_ID, &chip_id, 1U);
    if (status != BME280_OK)
    {
        return status;
    }
    return (chip_id == BME280_CHIP_ID) ? BME280_OK : BME280_ERROR_CHIP_ID;
}

static BME280_Status BME280_ReadCalibration(BME280_HandleTypeDef *device)
{
    uint8_t calibration_tp[26];
    uint8_t calibration_h[7];
    uint16_t raw_h4;
    uint16_t raw_h5;
    BME280_Status status;

    status = BME280_ReadRegs(device, BME280_REG_CALIB_TP,
                             calibration_tp, sizeof(calibration_tp));
    if (status != BME280_OK)
    {
        return status;
    }
    status = BME280_ReadRegs(device, BME280_REG_CALIB_H,
                             calibration_h, sizeof(calibration_h));
    if (status != BME280_OK)
    {
        return status;
    }

    device->dig_T1 = (uint16_t)((uint16_t)calibration_tp[0] |
                                ((uint16_t)calibration_tp[1] << 8U));
    device->dig_T2 = (int16_t)((uint16_t)calibration_tp[2] |
                               ((uint16_t)calibration_tp[3] << 8U));
    device->dig_T3 = (int16_t)((uint16_t)calibration_tp[4] |
                               ((uint16_t)calibration_tp[5] << 8U));
    device->dig_P1 = (uint16_t)((uint16_t)calibration_tp[6] |
                                ((uint16_t)calibration_tp[7] << 8U));
    device->dig_P2 = (int16_t)((uint16_t)calibration_tp[8] |
                               ((uint16_t)calibration_tp[9] << 8U));
    device->dig_P3 = (int16_t)((uint16_t)calibration_tp[10] |
                               ((uint16_t)calibration_tp[11] << 8U));
    device->dig_P4 = (int16_t)((uint16_t)calibration_tp[12] |
                               ((uint16_t)calibration_tp[13] << 8U));
    device->dig_P5 = (int16_t)((uint16_t)calibration_tp[14] |
                               ((uint16_t)calibration_tp[15] << 8U));
    device->dig_P6 = (int16_t)((uint16_t)calibration_tp[16] |
                               ((uint16_t)calibration_tp[17] << 8U));
    device->dig_P7 = (int16_t)((uint16_t)calibration_tp[18] |
                               ((uint16_t)calibration_tp[19] << 8U));
    device->dig_P8 = (int16_t)((uint16_t)calibration_tp[20] |
                               ((uint16_t)calibration_tp[21] << 8U));
    device->dig_P9 = (int16_t)((uint16_t)calibration_tp[22] |
                               ((uint16_t)calibration_tp[23] << 8U));
    device->dig_H1 = calibration_tp[25];

    device->dig_H2 = (int16_t)((uint16_t)calibration_h[0] |
                               ((uint16_t)calibration_h[1] << 8U));
    device->dig_H3 = calibration_h[2];
    raw_h4 = (uint16_t)(((uint16_t)calibration_h[3] << 4U) |
                        ((uint16_t)calibration_h[4] & 0x000FU));
    raw_h5 = (uint16_t)(((uint16_t)calibration_h[5] << 4U) |
                        ((uint16_t)calibration_h[4] >> 4U));
    if ((raw_h4 & 0x0800U) != 0U)
    {
        raw_h4 |= 0xF000U;
    }
    if ((raw_h5 & 0x0800U) != 0U)
    {
        raw_h5 |= 0xF000U;
    }
    device->dig_H4 = (int16_t)raw_h4;
    device->dig_H5 = (int16_t)raw_h5;
    device->dig_H6 = (int8_t)calibration_h[6];

    if ((device->dig_T1 == 0U) || (device->dig_T1 == 0xFFFFU) ||
        (device->dig_P1 == 0U) || (device->dig_P1 == 0xFFFFU))
    {
        return BME280_ERROR_CALIBRATION;
    }
    return BME280_OK;
}

static int32_t BME280_CompensateTemperature(BME280_HandleTypeDef *device,
                                             int32_t adc_temperature)
{
    int32_t var1;
    int32_t var2;

    var1 = ((((adc_temperature >> 3) -
              ((int32_t)device->dig_T1 << 1)))*
            (int32_t)device->dig_T2) >> 11;
    var2 = (((((adc_temperature >> 4) - (int32_t)device->dig_T1) *
              ((adc_temperature >> 4) - (int32_t)device->dig_T1)) >> 12) *
            (int32_t)device->dig_T3) >> 14;
    device->t_fine = var1 + var2;
    return (device->t_fine * 5 + 128) >> 8;
}

static uint32_t BME280_CompensatePressure(const BME280_HandleTypeDef *device,
                                           int32_t adc_pressure)
{
    int64_t var1;
    int64_t var2;
    int64_t pressure;

    var1 = (int64_t)device->t_fine - 128000;
    var2 = var1 * var1 * (int64_t)device->dig_P6;
    var2 += (var1 * (int64_t)device->dig_P5) << 17;
    var2 += ((int64_t)device->dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)device->dig_P3) >> 8) +
           ((var1 * (int64_t)device->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) *
            (int64_t)device->dig_P1) >> 33;
    if (var1 == 0)
    {
        return 0U;
    }

    pressure = 1048576 - adc_pressure;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)device->dig_P9 * (pressure >> 13) *
            (pressure >> 13)) >> 25;
    var2 = ((int64_t)device->dig_P8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) +
               ((int64_t)device->dig_P7 << 4);
    return (uint32_t)(pressure >> 8);
}

static uint32_t BME280_CompensateHumidity(const BME280_HandleTypeDef *device,
                                           int32_t adc_humidity)
{
    int32_t value;

    value = device->t_fine - 76800;
    value = (((((adc_humidity << 14) -
                 ((int32_t)device->dig_H4 << 20) -
                 ((int32_t)device->dig_H5 * value)) + 16384) >> 15) *
             (((((((value * (int32_t)device->dig_H6) >> 10) *
                   (((value * (int32_t)device->dig_H3) >> 11) + 32768)) >> 10) +
                2097152) * (int32_t)device->dig_H2 + 8192) >> 14));
    value -= (((((value >> 15) * (value >> 15)) >> 7) *
               (int32_t)device->dig_H1) >> 4);
    if (value < 0)
    {
        value = 0;
    }
    else if (value > 419430400)
    {
        value = 419430400;
    }
    return (uint32_t)(value >> 12);
}

void BME280_BusInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, BME280_SCL_PIN | BME280_SDA_PIN, GPIO_PIN_SET);
    gpio.Pin = BME280_SCL_PIN | BME280_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_dwt_ready = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;

    BME280_SdaRelease();
    BME280_SclRelease();
    BME280_DelayUs(BME280_I2C_HALF_PERIOD_US);
    (void)BME280_RecoverBus();
}

BME280_Status BME280_Init(BME280_HandleTypeDef *device)
{
    BME280_Status status;
    uint8_t reset_command = BME280_RESET_COMMAND;
    uint8_t sensor_status = BME280_STATUS_IM_UPDATE;
    uint8_t attempt;

    if (device == NULL)
    {
        return BME280_ERROR_ARGUMENT;
    }

    memset(device, 0, sizeof(*device));
    BME280_BusInit();

    status = BME280_ProbeAddress(device, BME280_I2C_ADDR_PRIMARY);
    if (status != BME280_OK)
    {
        status = BME280_ProbeAddress(device, BME280_I2C_ADDR_SECONDARY);
    }
    if (status != BME280_OK)
    {
        return status;
    }

    status = BME280_WriteRegs(device, BME280_REG_RESET, &reset_command, 1U);
    if (status != BME280_OK)
    {
        return status;
    }

    for (attempt = 0U; attempt < 10U; attempt++)
    {
        HAL_Delay(2U);
        status = BME280_ReadRegs(device, BME280_REG_STATUS, &sensor_status, 1U);
        if (status != BME280_OK)
        {
            return status;
        }
        if ((sensor_status & BME280_STATUS_IM_UPDATE) == 0U)
        {
            break;
        }
    }
    if ((sensor_status & BME280_STATUS_IM_UPDATE) != 0U)
    {
        return BME280_ERROR_TIMEOUT;
    }

    return BME280_ReadCalibration(device);
}

BME280_Status BME280_Config(BME280_HandleTypeDef *device,
                            BME280_Oversampling osrs_temperature,
                            BME280_Oversampling osrs_pressure,
                            BME280_Oversampling osrs_humidity,
                            BME280_Filter filter)
{
    uint8_t ctrl_hum;
    uint8_t config;
    BME280_Status status;

    if ((device == NULL) ||
        (osrs_temperature > BME280_OVERSAMPLING_X16) ||
        (osrs_pressure > BME280_OVERSAMPLING_X16) ||
        (osrs_humidity > BME280_OVERSAMPLING_X16) ||
        (filter > BME280_FILTER_X16))
    {
        return BME280_ERROR_ARGUMENT;
    }

    ctrl_hum = (uint8_t)osrs_humidity;
    config = (uint8_t)((uint8_t)filter << 2U);
    device->ctrl_meas_sleep =
        (uint8_t)(((uint8_t)osrs_temperature << 5U) |
                  ((uint8_t)osrs_pressure << 2U));

    status = BME280_WriteReg(device, BME280_REG_CTRL_HUM, ctrl_hum);
    if (status == BME280_OK)
    {
        status = BME280_WriteReg(device, BME280_REG_CONFIG, config);
    }
    if (status == BME280_OK)
    {
        status = BME280_WriteReg(device, BME280_REG_CTRL_MEAS,
                                 device->ctrl_meas_sleep);
    }
    if (status == BME280_OK)
    {
        device->osrs_temperature = (uint8_t)osrs_temperature;
        device->osrs_pressure = (uint8_t)osrs_pressure;
        device->osrs_humidity = (uint8_t)osrs_humidity;
    }
    return status;
}

BME280_Status BME280_TriggerMeasurement(BME280_HandleTypeDef *device)
{
    if ((device == NULL) ||
        (device->osrs_temperature == BME280_OVERSAMPLING_SKIP))
    {
        return BME280_ERROR_ARGUMENT;
    }
    return BME280_WriteReg(device, BME280_REG_CTRL_MEAS,
                           (uint8_t)(device->ctrl_meas_sleep |
                                     BME280_MODE_FORCED));
}

BME280_Status BME280_ReadMeasurement(BME280_HandleTypeDef *device,
                                     BME280_Data *data)
{
    uint8_t status_register;
    uint8_t raw[8];
    int32_t adc_pressure;
    int32_t adc_temperature;
    int32_t adc_humidity;
    BME280_Status status;

    if ((device == NULL) || (data == NULL))
    {
        return BME280_ERROR_ARGUMENT;
    }

    status = BME280_ReadRegs(device, BME280_REG_STATUS,
                             &status_register, 1U);
    if (status != BME280_OK)
    {
        return status;
    }
    if ((status_register & BME280_STATUS_MEASURING) != 0U)
    {
        return BME280_ERROR_NOT_READY;
    }

    status = BME280_ReadRegs(device, BME280_REG_DATA, raw, sizeof(raw));
    if (status != BME280_OK)
    {
        return status;
    }

    adc_pressure = ((int32_t)raw[0] << 12) |
                   ((int32_t)raw[1] << 4) |
                   ((int32_t)raw[2] >> 4);
    adc_temperature = ((int32_t)raw[3] << 12) |
                      ((int32_t)raw[4] << 4) |
                      ((int32_t)raw[5] >> 4);
    adc_humidity = ((int32_t)raw[6] << 8) | (int32_t)raw[7];

    if (adc_temperature == 0x80000)
    {
        return BME280_ERROR_NOT_READY;
    }

    data->temperature_x100 =
        BME280_CompensateTemperature(device, adc_temperature);
    data->pressure_pa =
        (device->osrs_pressure == BME280_OVERSAMPLING_SKIP) ? 0U :
        BME280_CompensatePressure(device, adc_pressure);
    data->humidity_x1024 =
        (device->osrs_humidity == BME280_OVERSAMPLING_SKIP) ? 0U :
        BME280_CompensateHumidity(device, adc_humidity);
    return BME280_OK;
}

BME280_Status BME280_ReadAll(BME280_HandleTypeDef *device,
                             BME280_Data *data)
{
    BME280_Status status;

    status = BME280_TriggerMeasurement(device);
    if (status != BME280_OK)
    {
        return status;
    }
    HAL_Delay(BME280_GetMeasurementDelayMs(device));
    return BME280_ReadMeasurement(device, data);
}

uint32_t BME280_GetMeasurementDelayMs(const BME280_HandleTypeDef *device)
{
    static const uint8_t oversampling_values[6] = {0U, 1U, 2U, 4U, 8U, 16U};
    uint32_t duration_us;

    if ((device == NULL) ||
        (device->osrs_temperature > BME280_OVERSAMPLING_X16) ||
        (device->osrs_pressure > BME280_OVERSAMPLING_X16) ||
        (device->osrs_humidity > BME280_OVERSAMPLING_X16))
    {
        return 20U;
    }

    duration_us = 1250U;
    if (device->osrs_temperature != BME280_OVERSAMPLING_SKIP)
    {
        duration_us += 2300U *
            oversampling_values[device->osrs_temperature];
    }
    if (device->osrs_pressure != BME280_OVERSAMPLING_SKIP)
    {
        duration_us += 2300U *
            oversampling_values[device->osrs_pressure] + 575U;
    }
    if (device->osrs_humidity != BME280_OVERSAMPLING_SKIP)
    {
        duration_us += 2300U *
            oversampling_values[device->osrs_humidity] + 575U;
    }
    return (duration_us + 999U) / 1000U;
}
