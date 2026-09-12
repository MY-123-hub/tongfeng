#ifndef BME280_H
#define BME280_H

#include "stm32f1xx_hal.h"

#include <stdint.h>

/* BME280 uses a 7-bit I2C address. The driver probes both valid addresses. */
#define BME280_I2C_ADDR_PRIMARY        (0x76U)
#define BME280_I2C_ADDR_SECONDARY      (0x77U)
#define BME280_CHIP_ID                 (0x60U)

typedef enum
{
    BME280_OK = 0,
    BME280_ERROR_ARGUMENT,
    BME280_ERROR_BUS,
    BME280_ERROR_NO_ACK,
    BME280_ERROR_TIMEOUT,
    BME280_ERROR_CHIP_ID,
    BME280_ERROR_CALIBRATION,
    BME280_ERROR_NOT_READY
} BME280_Status;

typedef enum
{
    BME280_OVERSAMPLING_SKIP = 0x00,
    BME280_OVERSAMPLING_X1   = 0x01,
    BME280_OVERSAMPLING_X2   = 0x02,
    BME280_OVERSAMPLING_X4   = 0x03,
    BME280_OVERSAMPLING_X8   = 0x04,
    BME280_OVERSAMPLING_X16  = 0x05
} BME280_Oversampling;

typedef enum
{
    BME280_FILTER_OFF = 0x00,
    BME280_FILTER_X2  = 0x01,
    BME280_FILTER_X4  = 0x02,
    BME280_FILTER_X8  = 0x03,
    BME280_FILTER_X16 = 0x04
} BME280_Filter;

typedef struct
{
    uint8_t i2c_address;
    uint8_t ctrl_meas_sleep;
    uint8_t osrs_temperature;
    uint8_t osrs_pressure;
    uint8_t osrs_humidity;

    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
    int32_t t_fine;
} BME280_HandleTypeDef;

typedef struct
{
    int32_t temperature_x100;
    uint32_t pressure_pa;
    uint32_t humidity_x1024;
} BME280_Data;

/** Configure PB14 as SCL and PB15 as SDA for open-drain software I2C. */
void BME280_BusInit(void);

/** Probe 0x76/0x77, verify chip ID, reset the device and read calibration. */
BME280_Status BME280_Init(BME280_HandleTypeDef *device);

/** Configure oversampling and filtering while keeping the sensor in sleep. */
BME280_Status BME280_Config(BME280_HandleTypeDef *device,
                            BME280_Oversampling osrs_temperature,
                            BME280_Oversampling osrs_pressure,
                            BME280_Oversampling osrs_humidity,
                            BME280_Filter filter);

/** Start one forced-mode conversion. */
BME280_Status BME280_TriggerMeasurement(BME280_HandleTypeDef *device);

/** Read and compensate the latest completed conversion. */
BME280_Status BME280_ReadMeasurement(BME280_HandleTypeDef *device,
                                     BME280_Data *data);

/** Blocking convenience API. The FreeRTOS task uses the split APIs above. */
BME280_Status BME280_ReadAll(BME280_HandleTypeDef *device,
                             BME280_Data *data);

/** Return the worst-case conversion time for the current configuration. */
uint32_t BME280_GetMeasurementDelayMs(const BME280_HandleTypeDef *device);

#endif /* BME280_H */
