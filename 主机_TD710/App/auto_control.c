#include "auto_control.h"

#include <stddef.h>

#include "master_config.h"
#include "master_messages.h"

AutoDecision AutoControl_Step(uint8_t control_mode,
                              const int16_t *temperatures_x10,
                              uint8_t point_count,
                              uint8_t snapshot_fresh,
                              int16_t target_x10)
{
    uint8_t any_valid = 0U;
    uint8_t any_high = 0U;
    uint8_t all_at_or_below_stop = 1U;
    int16_t stop_threshold;
    uint32_t index;

    if ((temperatures_x10 == NULL) ||
        (point_count != LORA_PROTOCOL_TEMP_COUNT) ||
        (target_x10 < MASTER_MIN_TARGET_TEMP_X10) ||
        (target_x10 > MASTER_MAX_TARGET_TEMP_X10))
    {
        return AUTO_DECISION_INVALID;
    }

    /* 过期数据、非自动模式都不得产生新的变频器命令。 */
    if ((control_mode != MASTER_CONTROL_MODE_AUTO) || (snapshot_fresh == 0U))
    {
        return AUTO_DECISION_HOLD;
    }

    stop_threshold = (int16_t)(target_x10 - MASTER_AUTO_STOP_HYSTERESIS_X10);
    for (index = 0U; index < LORA_PROTOCOL_TEMP_COUNT; index++)
    {
        int16_t temperature = temperatures_x10[index];

        /* 无效点不参与启动或停机判断。 */
        if (temperature == LORA_PROTOCOL_TEMPERATURE_INVALID)
        {
            continue;
        }
        any_valid = 1U;
        if (temperature > target_x10)
        {
            any_high = 1U;
        }
        if (temperature > stop_threshold)
        {
            all_at_or_below_stop = 0U;
        }
    }

    if (any_high != 0U)
    {
        return AUTO_DECISION_RUN;
    }
    if ((any_valid != 0U) && (all_at_or_below_stop != 0U))
    {
        return AUTO_DECISION_STOP;
    }
    return AUTO_DECISION_HOLD;
}
