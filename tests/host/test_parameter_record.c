#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "master_config.h"
#include "master_messages.h"
#include "parameter_record.h"

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

static MasterParameters MakeParameters(uint16_t frequency,
                                       int16_t target,
                                       uint8_t mode)
{
    MasterParameters parameters;

    parameters.frequency_x100 = frequency;
    parameters.target_temperature_x10 = target;
    parameters.control_mode = mode;
    return parameters;
}

static void TestRoundTripAndCorruption(void)
{
    MasterParameters input = MakeParameters(5000U, 260,
                                             MASTER_CONTROL_MODE_MANUAL_STOP);
    MasterParameters output;
    uint8_t record[PARAMETER_RECORD_SIZE];
    uint8_t damaged[PARAMETER_RECORD_SIZE];
    uint32_t generation;
    uint16_t bit;

    CHECK(ParameterRecord_Crc32((const uint8_t *)"123456789", 9U) ==
          0xCBF43926UL);
    CHECK(ParameterRecord_Encode(&input, 123456U, record, sizeof(record)) ==
          PARAMETER_RECORD_OK);
    CHECK(ParameterRecord_Decode(record, sizeof(record), &output, &generation) ==
          PARAMETER_RECORD_OK);
    CHECK(output.frequency_x100 == input.frequency_x100);
    CHECK(output.target_temperature_x10 == input.target_temperature_x10);
    CHECK(output.control_mode == input.control_mode);
    CHECK(generation == 123456U);

    for (bit = 0U; bit < (PARAMETER_RECORD_SIZE * 8U); bit++)
    {
        memcpy(damaged, record, sizeof(damaged));
        damaged[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
        CHECK(ParameterRecord_Decode(damaged, sizeof(damaged),
                                     &output, &generation) != PARAMETER_RECORD_OK);
    }
}

static void TestSelectionAndGenerationWrap(void)
{
    MasterParameters old_parameters = MakeParameters(3000U, 260,
                                                       MASTER_CONTROL_MODE_AUTO);
    MasterParameters new_parameters = MakeParameters(4000U, 270,
                                                       MASTER_CONTROL_MODE_MANUAL_RUN);
    MasterParameters selected;
    uint8_t slot_a[PARAMETER_RECORD_SIZE];
    uint8_t slot_b[PARAMETER_RECORD_SIZE];
    uint32_t generation;
    uint8_t selected_slot;

    memset(slot_a, 0xFF, sizeof(slot_a));
    memset(slot_b, 0xFF, sizeof(slot_b));
    CHECK(ParameterRecord_SelectNewest(slot_a, slot_b, &selected,
                                       &generation, &selected_slot) == 0U);

    CHECK(ParameterRecord_Encode(&old_parameters, 10U, slot_a, sizeof(slot_a)) ==
          PARAMETER_RECORD_OK);
    CHECK(ParameterRecord_SelectNewest(slot_a, slot_b, &selected,
                                       &generation, &selected_slot) == 1U);
    CHECK(selected_slot == 0U);
    CHECK(generation == 10U);

    CHECK(ParameterRecord_Encode(&new_parameters, 11U, slot_b, sizeof(slot_b)) ==
          PARAMETER_RECORD_OK);
    CHECK(ParameterRecord_SelectNewest(slot_a, slot_b, &selected,
                                       &generation, &selected_slot) == 1U);
    CHECK(selected_slot == 1U);
    CHECK(selected.frequency_x100 == 4000U);

    CHECK(ParameterRecord_Encode(&old_parameters, 0xFFFFFFFEUL,
                                 slot_a, sizeof(slot_a)) == PARAMETER_RECORD_OK);
    CHECK(ParameterRecord_Encode(&new_parameters, 1U,
                                 slot_b, sizeof(slot_b)) == PARAMETER_RECORD_OK);
    CHECK(ParameterRecord_SelectNewest(slot_a, slot_b, &selected,
                                       &generation, &selected_slot) == 1U);
    CHECK(selected_slot == 1U);
    CHECK(generation == 1U);
}

static void TestPowerLossAtEveryHalfword(void)
{
    MasterParameters old_parameters = MakeParameters(3000U, 260,
                                                       MASTER_CONTROL_MODE_AUTO);
    MasterParameters new_parameters = MakeParameters(5000U, 280,
                                                       MASTER_CONTROL_MODE_MANUAL_STOP);
    MasterParameters selected;
    uint8_t old_slot[PARAMETER_RECORD_SIZE];
    uint8_t complete_new[PARAMETER_RECORD_SIZE];
    uint8_t partial_new[PARAMETER_RECORD_SIZE];
    uint32_t generation;
    uint8_t selected_slot;
    uint16_t written_halfwords;

    CHECK(ParameterRecord_Encode(&old_parameters, 1U,
                                 old_slot, sizeof(old_slot)) == PARAMETER_RECORD_OK);
    CHECK(ParameterRecord_Encode(&new_parameters, 2U,
                                 complete_new, sizeof(complete_new)) ==
          PARAMETER_RECORD_OK);

    for (written_halfwords = 0U;
         written_halfwords <= (PARAMETER_RECORD_SIZE / 2U);
         written_halfwords++)
    {
        memset(partial_new, 0xFF, sizeof(partial_new));
        memcpy(partial_new, complete_new, (size_t)written_halfwords * 2U);
        CHECK(ParameterRecord_SelectNewest(old_slot, partial_new, &selected,
                                           &generation, &selected_slot) == 1U);
        if (written_halfwords < (PARAMETER_RECORD_SIZE / 2U))
        {
            CHECK(selected_slot == 0U);
            CHECK(generation == 1U);
        }
        else
        {
            CHECK(selected_slot == 1U);
            CHECK(generation == 2U);
        }
    }
}

static void TestParameterAndPointerGuards(void)
{
    MasterParameters parameters;
    MasterParameters output;
    uint8_t record[PARAMETER_RECORD_SIZE];
    uint32_t generation;
    uint8_t selected;

    parameters = MakeParameters((uint16_t)(MASTER_MAX_FREQUENCY_X100 + 1U),
                                260, MASTER_CONTROL_MODE_AUTO);
    CHECK(ParameterRecord_ParametersValid(&parameters) == 0U);
    parameters = MakeParameters(3000U,
                                (int16_t)(MASTER_MIN_TARGET_TEMP_X10 - 1),
                                MASTER_CONTROL_MODE_AUTO);
    CHECK(ParameterRecord_ParametersValid(&parameters) == 0U);
    parameters = MakeParameters(3000U, 260, 3U);
    CHECK(ParameterRecord_ParametersValid(&parameters) == 0U);
    parameters = MakeParameters(3000U, -100, MASTER_CONTROL_MODE_AUTO);
    CHECK(ParameterRecord_ParametersValid(&parameters) == 1U);

    CHECK(ParameterRecord_Encode(NULL, 0U, record, sizeof(record)) ==
          PARAMETER_RECORD_NULL_POINTER);
    CHECK(ParameterRecord_Encode(&parameters, 0U, NULL, sizeof(record)) ==
          PARAMETER_RECORD_NULL_POINTER);
    CHECK(ParameterRecord_Encode(&parameters, 0U, record,
                                 PARAMETER_RECORD_SIZE - 1U) ==
          PARAMETER_RECORD_INVALID_FORMAT);
    CHECK(ParameterRecord_Decode(NULL, sizeof(record), &output, &generation) ==
          PARAMETER_RECORD_NULL_POINTER);
    CHECK(ParameterRecord_Decode(record, PARAMETER_RECORD_SIZE - 1U,
                                 &output, &generation) ==
          PARAMETER_RECORD_INVALID_FORMAT);
    CHECK(ParameterRecord_SelectNewest(NULL, record, &output,
                                       &generation, &selected) == 0U);
    CHECK(ParameterRecord_Crc32(NULL, 1U) == 0U);
}

int main(void)
{
    TestRoundTripAndCorruption();
    TestSelectionAndGenerationWrap();
    TestPowerLossAtEveryHalfword();
    TestParameterAndPointerGuards();
    printf("parameter_record: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
