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

static void TestStartRuleScansPastInvalid(void)
{
    AutoControlState state;
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];

    AutoControl_Init(&state);
    Fill(temperatures, 0);
    temperatures[35] = MASTER_DEFAULT_TARGET_TEMP_X10 + 1;
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 0U) ==
          AUTO_DECISION_RUN);
    CHECK(state.low_hold_active == 0U);

    Fill(temperatures, MASTER_DEFAULT_TARGET_TEMP_X10);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 0U) ==
          AUTO_DECISION_HOLD);
}

static void TestStopHoldAndReset(void)
{
    AutoControlState state;
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    int16_t low = MASTER_DEFAULT_TARGET_TEMP_X10 -
                  MASTER_AUTO_STOP_HYSTERESIS_X10;

    AutoControl_Init(&state);
    Fill(temperatures, low);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 1000U) ==
          AUTO_DECISION_HOLD);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10,
                           1000U + MASTER_AUTO_STOP_HOLD_MS - 1U) ==
          AUTO_DECISION_HOLD);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10,
                           1000U + MASTER_AUTO_STOP_HOLD_MS) ==
          AUTO_DECISION_STOP);

    temperatures[10] = (int16_t)(low + 1);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 70000U) ==
          AUTO_DECISION_HOLD);
    CHECK(state.low_hold_active == 0U);
    temperatures[10] = low;
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 70001U) ==
          AUTO_DECISION_HOLD);
    CHECK(state.low_since_ms == 70001U);

    temperatures[0] = 0;
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 80000U) ==
          AUTO_DECISION_HOLD);
    CHECK(state.low_hold_active == 0U);
}

static void TestFreshnessModesAndRecovery(void)
{
    AutoControlState state;
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    int16_t low = MASTER_DEFAULT_TARGET_TEMP_X10 -
                  MASTER_AUTO_STOP_HYSTERESIS_X10;

    AutoControl_Init(&state);
    Fill(temperatures, low);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 0U) ==
          AUTO_DECISION_HOLD);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 0U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 1000U) ==
          AUTO_DECISION_HOLD);
    CHECK(state.low_hold_active == 0U);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 2000U) ==
          AUTO_DECISION_HOLD);
    CHECK(state.low_since_ms == 2000U);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_MANUAL_RUN,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 3000U) ==
          AUTO_DECISION_HOLD);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_MANUAL_STOP,
                           temperatures, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           MASTER_DEFAULT_TARGET_TEMP_X10, 4000U) ==
          AUTO_DECISION_HOLD);
}

static void TestWrapNegativeAndGuards(void)
{
    struct
    {
        int16_t before;
        int16_t values[LORA_PROTOCOL_TEMP_COUNT];
        int16_t after;
    } guarded;
    AutoControlState state;
    uint32_t start = 0xFFFFFF00UL;

    guarded.before = 1234;
    guarded.after = 2345;
    Fill(guarded.values, -100);
    AutoControl_Init(&state);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           guarded.values, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           0, start) == AUTO_DECISION_HOLD);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           guarded.values, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           0, (uint32_t)(start + MASTER_AUTO_STOP_HOLD_MS)) ==
          AUTO_DECISION_STOP);
    CHECK(guarded.before == 1234);
    CHECK(guarded.after == 2345);

    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           guarded.values, LORA_PROTOCOL_TEMP_COUNT - 1U, 1U,
                           0, 0U) == AUTO_DECISION_INVALID);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           NULL, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           0, 0U) == AUTO_DECISION_INVALID);
    CHECK(AutoControl_Step(NULL, MASTER_CONTROL_MODE_AUTO,
                           guarded.values, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           0, 0U) == AUTO_DECISION_INVALID);
    CHECK(AutoControl_Step(&state, MASTER_CONTROL_MODE_AUTO,
                           guarded.values, LORA_PROTOCOL_TEMP_COUNT, 1U,
                           (int16_t)(MASTER_MAX_TARGET_TEMP_X10 + 1), 0U) ==
          AUTO_DECISION_INVALID);
    AutoControl_Init(NULL);
}

int main(void)
{
    TestStartRuleScansPastInvalid();
    TestStopHoldAndReset();
    TestFreshnessModesAndRecovery();
    TestWrapNegativeAndGuards();
    printf("auto_control: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
