#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

#include "main.h"

typedef enum {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_OLD = 1,
    SENSOR_TYPE_NEW = 2
} SensorType;

SensorType Sensor_Detect(GPIO_TypeDef *GPIOx, uint16_t PINx);

extern uint16_t time_10s,time_100ms;
extern uint8_t Rx2Buffer[100],rx2_pointer,rx2_data;

#endif /*__INTERRUPT_H__*/
