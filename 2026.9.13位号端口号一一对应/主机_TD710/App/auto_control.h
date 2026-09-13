#ifndef AUTO_CONTROL_H
#define AUTO_CONTROL_H

#include <stdint.h>

#include "lora_protocol.h"

typedef enum
{
    AUTO_DECISION_INVALID = 0,
    AUTO_DECISION_HOLD,
    AUTO_DECISION_RUN,
    AUTO_DECISION_STOP
} AutoDecision;

AutoDecision AutoControl_Step(uint8_t control_mode,
                              const int16_t *temperatures_x10,
                              uint8_t point_count,
                              uint8_t snapshot_fresh,
                              int16_t target_x10);

#endif /* AUTO_CONTROL_H */
