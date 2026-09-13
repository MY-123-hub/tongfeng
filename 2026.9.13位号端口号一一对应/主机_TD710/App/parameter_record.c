#include "parameter_record.h"

#include <stddef.h>
#include <string.h>

#include "master_config.h"
#include "master_calendar.h"
#include "master_messages.h"

#define PARAMETER_RECORD_MAGIC           (0x31504656UL)
#define PARAMETER_RECORD_VERSION_V1      (0x0001U)
#define PARAMETER_RECORD_VERSION         (0x0002U)
#define PARAMETER_RECORD_V1_SIZE         (32U)
#define PARAMETER_RECORD_V1_COMMIT_OFFSET (28U)
#define PARAMETER_RECORD_COMMIT_1        (0x5AA5U)
#define PARAMETER_RECORD_COMMIT_2        (0xA55AU)

static uint16_t ParameterRecord_ReadU16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] |
                      (uint16_t)((uint16_t)data[1] << 8U));
}

static uint32_t ParameterRecord_ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void ParameterRecord_WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0x00FFU);
    data[1] = (uint8_t)(value >> 8U);
}

static void ParameterRecord_WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0x000000FFUL);
    data[1] = (uint8_t)((value >> 8U) & 0x000000FFUL);
    data[2] = (uint8_t)((value >> 16U) & 0x000000FFUL);
    data[3] = (uint8_t)((value >> 24U) & 0x000000FFUL);
}

static uint8_t ParameterRecord_PlanTimeValid(const MasterPlanTime *time)
{
    MasterDateTime calendar_time;

    calendar_time.year = time->year;
    calendar_time.month = time->month;
    calendar_time.day = time->day;
    calendar_time.hour = time->hour;
    calendar_time.minute = time->minute;
    return MasterCalendar_IsValid(&calendar_time);
}

uint32_t ParameterRecord_Crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint32_t)data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1UL) != 0UL) ?
                  ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

uint8_t ParameterRecord_ParametersValid(const MasterParameters *parameters)
{
    MasterDateTime start_time;
    MasterDateTime end_time;
    if (parameters == NULL)
    {
        return 0U;
    }
    if ((parameters->frequency_x100 > MASTER_MAX_FREQUENCY_X100) ||
        (parameters->target_temperature_x10 < MASTER_MIN_TARGET_TEMP_X10) ||
        (parameters->target_temperature_x10 > MASTER_MAX_TARGET_TEMP_X10) ||
        (parameters->control_mode > MASTER_CONTROL_MODE_MANUAL_STOP))
    {
        return 0U;
    }
    if (((parameters->target_humidity_configured > 1U) ||
         (parameters->schedule_enabled > 1U)) ||
        (parameters->target_humidity_x10 > 1000U))
    {
        return 0U;
    }
    if ((parameters->schedule_enabled != 0U) &&
        ((ParameterRecord_PlanTimeValid(&parameters->plan_start) == 0U) ||
         (ParameterRecord_PlanTimeValid(&parameters->plan_end) == 0U)))
    {
        return 0U;
    }
    start_time.year = parameters->plan_start.year;
    start_time.month = parameters->plan_start.month;
    start_time.day = parameters->plan_start.day;
    start_time.hour = parameters->plan_start.hour;
    start_time.minute = parameters->plan_start.minute;
    end_time.year = parameters->plan_end.year;
    end_time.month = parameters->plan_end.month;
    end_time.day = parameters->plan_end.day;
    end_time.hour = parameters->plan_end.hour;
    end_time.minute = parameters->plan_end.minute;
    if ((parameters->schedule_enabled != 0U) &&
        (MasterCalendar_Compare(&start_time, &end_time) >= 0))
    {
        return 0U;
    }
    return 1U;
}

ParameterRecordStatus ParameterRecord_Encode(const MasterParameters *parameters,
                                             uint32_t generation,
                                             uint8_t *record,
                                             uint16_t capacity)
{
    uint32_t crc;

    if ((parameters == NULL) || (record == NULL))
    {
        return PARAMETER_RECORD_NULL_POINTER;
    }
    if (capacity < PARAMETER_RECORD_SIZE)
    {
        return PARAMETER_RECORD_INVALID_FORMAT;
    }
    if (ParameterRecord_ParametersValid(parameters) == 0U)
    {
        return PARAMETER_RECORD_INVALID_PARAMETER;
    }

    memset(record, 0, PARAMETER_RECORD_SIZE);
    ParameterRecord_WriteU32(&record[0], PARAMETER_RECORD_MAGIC);
    ParameterRecord_WriteU16(&record[4], PARAMETER_RECORD_VERSION);
    ParameterRecord_WriteU16(&record[6], PARAMETER_RECORD_SIZE);
    ParameterRecord_WriteU32(&record[8], generation);
    ParameterRecord_WriteU32(&record[12], ~generation);
    ParameterRecord_WriteU16(&record[16], parameters->frequency_x100);
    ParameterRecord_WriteU16(&record[18],
                             (uint16_t)parameters->target_temperature_x10);
    record[20] = parameters->control_mode;
    record[21] = parameters->target_humidity_configured;
    record[22] = parameters->schedule_enabled;
    ParameterRecord_WriteU16(&record[24], parameters->target_humidity_x10);
    ParameterRecord_WriteU16(&record[26], parameters->plan_start.year);
    record[28] = parameters->plan_start.month;
    record[29] = parameters->plan_start.day;
    record[30] = parameters->plan_start.hour;
    record[31] = parameters->plan_start.minute;
    ParameterRecord_WriteU16(&record[32], parameters->plan_end.year);
    record[34] = parameters->plan_end.month;
    record[35] = parameters->plan_end.day;
    record[36] = parameters->plan_end.hour;
    record[37] = parameters->plan_end.minute;
    crc = ParameterRecord_Crc32(record, 40U);
    ParameterRecord_WriteU32(&record[40], crc);
    ParameterRecord_WriteU16(&record[44], PARAMETER_RECORD_COMMIT_1);
    ParameterRecord_WriteU16(&record[46], PARAMETER_RECORD_COMMIT_2);
    return PARAMETER_RECORD_OK;
}

ParameterRecordStatus ParameterRecord_Decode(const uint8_t *record,
                                             uint16_t length,
                                             MasterParameters *parameters,
                                             uint32_t *generation)
{
    MasterParameters decoded;
    uint32_t decoded_generation;
    uint32_t stored_crc;

    if ((record == NULL) || (parameters == NULL) || (generation == NULL))
    {
        return PARAMETER_RECORD_NULL_POINTER;
    }
    if (length < PARAMETER_RECORD_SIZE)
    {
        return PARAMETER_RECORD_INVALID_FORMAT;
    }
    if (ParameterRecord_ReadU32(&record[0]) != PARAMETER_RECORD_MAGIC)
    {
        return PARAMETER_RECORD_INVALID_FORMAT;
    }

    if ((ParameterRecord_ReadU16(&record[4]) == PARAMETER_RECORD_VERSION_V1) &&
        (ParameterRecord_ReadU16(&record[6]) == PARAMETER_RECORD_V1_SIZE))
    {
        if ((ParameterRecord_ReadU16(&record[28]) != PARAMETER_RECORD_COMMIT_1) ||
            (ParameterRecord_ReadU16(&record[30]) != PARAMETER_RECORD_COMMIT_2))
        {
            return PARAMETER_RECORD_NOT_COMMITTED;
        }
        decoded_generation = ParameterRecord_ReadU32(&record[8]);
        if ((decoded_generation ^ ParameterRecord_ReadU32(&record[12])) != 0xFFFFFFFFUL ||
            (ParameterRecord_ReadU32(&record[24]) != ParameterRecord_Crc32(record, 24U)))
        {
            return PARAMETER_RECORD_CRC_ERROR;
        }
        memset(&decoded, 0, sizeof(decoded));
        decoded.frequency_x100 = ParameterRecord_ReadU16(&record[16]);
        decoded.target_temperature_x10 = (int16_t)ParameterRecord_ReadU16(&record[18]);
        decoded.target_humidity_x10 = MASTER_DEFAULT_TARGET_HUMIDITY_X10;
        decoded.control_mode = record[20];
        if ((record[21] != 0U) || (record[22] != 0U) || (record[23] != 0U) ||
            (ParameterRecord_ParametersValid(&decoded) == 0U))
        {
            return PARAMETER_RECORD_INVALID_PARAMETER;
        }
        *parameters = decoded;
        *generation = decoded_generation;
        return PARAMETER_RECORD_OK;
    }
    if ((ParameterRecord_ReadU16(&record[4]) != PARAMETER_RECORD_VERSION) ||
        (ParameterRecord_ReadU16(&record[6]) != PARAMETER_RECORD_SIZE) ||
        (ParameterRecord_ReadU16(&record[44]) != PARAMETER_RECORD_COMMIT_1) ||
        (ParameterRecord_ReadU16(&record[46]) != PARAMETER_RECORD_COMMIT_2))
    {
        return PARAMETER_RECORD_INVALID_FORMAT;
    }

    decoded_generation = ParameterRecord_ReadU32(&record[8]);
    if ((decoded_generation ^ ParameterRecord_ReadU32(&record[12])) !=
        0xFFFFFFFFUL)
    {
        return PARAMETER_RECORD_INVALID_FORMAT;
    }
    stored_crc = ParameterRecord_ReadU32(&record[40]);
    if (stored_crc != ParameterRecord_Crc32(record, 40U))
    {
        return PARAMETER_RECORD_CRC_ERROR;
    }

    decoded.frequency_x100 = ParameterRecord_ReadU16(&record[16]);
    decoded.target_temperature_x10 =
        (int16_t)ParameterRecord_ReadU16(&record[18]);
    decoded.control_mode = record[20];
    decoded.target_humidity_configured = record[21];
    decoded.schedule_enabled = record[22];
    decoded.target_humidity_x10 = ParameterRecord_ReadU16(&record[24]);
    decoded.plan_start.year = ParameterRecord_ReadU16(&record[26]);
    decoded.plan_start.month = record[28];
    decoded.plan_start.day = record[29];
    decoded.plan_start.hour = record[30];
    decoded.plan_start.minute = record[31];
    decoded.plan_end.year = ParameterRecord_ReadU16(&record[32]);
    decoded.plan_end.month = record[34];
    decoded.plan_end.day = record[35];
    decoded.plan_end.hour = record[36];
    decoded.plan_end.minute = record[37];
    if ((record[23] != 0U) || (record[38] != 0U) || (record[39] != 0U) ||
        (ParameterRecord_ParametersValid(&decoded) == 0U))
    {
        return PARAMETER_RECORD_INVALID_PARAMETER;
    }

    *parameters = decoded;
    *generation = decoded_generation;
    return PARAMETER_RECORD_OK;
}

uint8_t ParameterRecord_SelectNewest(const uint8_t *slot_a,
                                     const uint8_t *slot_b,
                                     MasterParameters *parameters,
                                     uint32_t *generation,
                                     uint8_t *selected_slot)
{
    MasterParameters parameters_a;
    MasterParameters parameters_b;
    uint32_t generation_a = 0U;
    uint32_t generation_b = 0U;
    uint8_t valid_a;
    uint8_t valid_b;

    if ((slot_a == NULL) || (slot_b == NULL) || (parameters == NULL) ||
        (generation == NULL) || (selected_slot == NULL))
    {
        return 0U;
    }

    valid_a = (ParameterRecord_Decode(slot_a, PARAMETER_RECORD_SIZE,
                                      &parameters_a, &generation_a) ==
               PARAMETER_RECORD_OK) ? 1U : 0U;
    valid_b = (ParameterRecord_Decode(slot_b, PARAMETER_RECORD_SIZE,
                                      &parameters_b, &generation_b) ==
               PARAMETER_RECORD_OK) ? 1U : 0U;
    if ((valid_a == 0U) && (valid_b == 0U))
    {
        return 0U;
    }
    if ((valid_b != 0U) &&
        ((valid_a == 0U) || ((int32_t)(generation_b - generation_a) > 0)))
    {
        *parameters = parameters_b;
        *generation = generation_b;
        *selected_slot = 1U;
    }
    else
    {
        *parameters = parameters_a;
        *generation = generation_a;
        *selected_slot = 0U;
    }
    return 1U;
}
