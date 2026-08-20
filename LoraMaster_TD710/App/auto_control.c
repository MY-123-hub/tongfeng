#include "auto_control.h"

#include <stddef.h>

#include "master_config.h"
#include "master_messages.h"

static void AutoControl_ResetLowHold(AutoControlState *state)
{
    state->low_hold_active = 0U;
    state->low_since_ms = 0U;
}

void AutoControl_Init(AutoControlState *state)
{
    if (state != NULL)
    {
        AutoControl_ResetLowHold(state);
    }
}

AutoDecision AutoControl_Step(AutoControlState *state,
                              uint8_t control_mode,
                              const int16_t *temperatures_x10,
                              uint8_t point_count,
                              uint8_t snapshot_fresh,
                              int16_t target_x10,
                              uint32_t now_ms)
{
    uint8_t any_high = 0U;
    uint8_t all_low = 1U;
    int16_t stop_threshold;
    uint32_t index;

    if ((state == NULL) || (temperatures_x10 == NULL) ||
        (point_count != LORA_PROTOCOL_TEMP_COUNT) ||
        (target_x10 < MASTER_MIN_TARGET_TEMP_X10) ||
        (target_x10 > MASTER_MAX_TARGET_TEMP_X10))
    {
        if (state != NULL)
        {
            AutoControl_ResetLowHold(state);
        }
        return AUTO_DECISION_INVALID;
    }

    if ((control_mode != MASTER_CONTROL_MODE_AUTO) ||
        (snapshot_fresh == 0U))
    {
        AutoControl_ResetLowHold(state);
        return AUTO_DECISION_HOLD;
    }

    stop_threshold = (int16_t)(target_x10 - MASTER_AUTO_STOP_HYSTERESIS_X10);
    for (index = 0U; index < LORA_PROTOCOL_TEMP_COUNT; index++)
    {
        int16_t temperature = temperatures_x10[index];

        if ((temperature != 0) && (temperature > target_x10))
        {
            any_high = 1U;
        }
        if ((temperature == 0) || (temperature > stop_threshold))
        {
            all_low = 0U;
        }
    }

    if (any_high != 0U)
    {
        AutoControl_ResetLowHold(state);
        return AUTO_DECISION_RUN;
    }
    if (all_low == 0U)
    {
        AutoControl_ResetLowHold(state);
        return AUTO_DECISION_HOLD;
    }

    if (state->low_hold_active == 0U)
    {
        state->low_hold_active = 1U;
        state->low_since_ms = now_ms;
        return AUTO_DECISION_HOLD;
    }
    if ((uint32_t)(now_ms - state->low_since_ms) >= MASTER_AUTO_STOP_HOLD_MS)
    {
        return AUTO_DECISION_STOP;
    }
    return AUTO_DECISION_HOLD;
}
