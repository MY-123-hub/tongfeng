#ifndef __I2C_HAL_H
#define __I2C_HAL_H


#include "main.h"
#include "i2c.h"
#include "stdint.h"
#include "stdio.h"


/*********************************************************************
 * INCLUDES
 */
#include <stdint.h>

/*********************************************************************
 * DEFINITIONS
 */
#define GZP6859D_SLAVE_ADDR         0x6D

#define GZP6859D_WRITE_BIT          0x00
#define GZP6859D_READ_BIT           0x01

#define GZP6859D_DATA_MSB_ADDR      0x06
#define GZP6859D_DATA_CSB_ADDR      0x07
#define GZP6859D_DATA_LSB_ADDR      0x08
#define GZP6859D_TEMP_MSB_ADDR      0x09
#define GZP6859D_TEMP_LSB_ADDR      0x0A
#define GZP6859D_CMD_ADDR           0x30
#define GZP6859D_SYS_CONFIG_ADDR    0xA5
#define GZP6859D_P_CONFIG_ADDR      0xA6

#define GZP6859D_ONE_TEMP           0x08
#define GZP6859D_ONE_PRESS          0x09
#define GZP6859D_COM                0x0A
#define GZP6859D_DORMANT            0x0B

#define GZP6859D_K_VALUE            1024


void GZP6859D_ReadSingleModePressureData(uint32_t *pPressure);
void GZP6859D_ReadCombinedModeData(uint8_t *pTemperature, uint8_t *pPressure);


#endif
