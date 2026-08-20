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

typedef struct
{
    uint32_t low_since_ms;
    uint8_t low_hold_active;
} AutoControlState;

void AutoControl_Init(AutoControlState *state);
AutoDecision AutoControl_Step(AutoControlState *state,
                              uint8_t control_mode,
                              const int16_t *temperatures_x10,
                              uint8_t point_count,
                              uint8_t snapshot_fresh,
                              int16_t target_x10,
                              uint32_t now_ms);

#endif /* AUTO_CONTROL_H */
