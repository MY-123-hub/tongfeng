#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "auto_control.h"
#include "master_config.h"
#include "master_messages.h"

static uint32_t g_checks;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        g_checks++;                                                             \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

static void Fill(int16_t *temperatures, int16_t value)
{
    uint32_t i;

    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        temperatures[i] = value;
    }
}

static void TestAnyValidHighStarts(void)
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];

    Fill(temperatures, LORA_PROTOCOL_TEMPERATURE_INVALID);
    temperatures[35] = MASTER_DEFAULT_TARGET_TEMP_X10 + 1;
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_RUN);
}

static void TestStopThresholdAndBoundaryHold(void)
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    int16_t stop_threshold = MASTER_DEFAULT_TARGET_TEMP_X10 -
                             MASTER_AUTO_STOP_HYSTERESIS_X10;

    Fill(temperatures, stop_threshold);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_STOP);

    temperatures[10] = (int16_t)(stop_threshold + 1);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_HOLD);

    Fill(temperatures, MASTER_DEFAULT_TARGET_TEMP_X10);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_HOLD);
}

static void TestInvalidAndFreshnessRules(void)
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    int16_t stop_threshold = MASTER_DEFAULT_TARGET_TEMP_X10 -
                             MASTER_AUTO_STOP_HYSTERESIS_X10;

    Fill(temperatures, LORA_PROTOCOL_TEMPERATURE_INVALID);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_HOLD);

    temperatures[0] = stop_threshold;
    temperatures[1] = MASTER_DEFAULT_TARGET_TEMP_X10 + 1;
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_RUN);

    temperatures[1] = stop_threshold;
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_STOP);

    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 0U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_HOLD);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_MANUAL_STOP,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10) == AUTO_DECISION_HOLD);
}

static void TestGuards(void)
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];

    Fill(temperatures, 0);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT - 1U, 1U,
                           0) == AUTO_DECISION_INVALID);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           NULL, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           0) == AUTO_DECISION_INVALID);
    CHECK(AutoControl_Step(MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           (int16_t)(MASTER_MAX_TARGET_TEMP_X10 + 1)) ==
          AUTO_DECISION_INVALID);
}

int main(void)
{
    TestAnyValidHighStarts();
    TestStopThresholdAndBoundaryHold();
    TestInvalidAndFreshnessRules();
    TestGuards();
    printf("auto_control: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
