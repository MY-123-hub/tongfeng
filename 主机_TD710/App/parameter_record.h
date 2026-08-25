#ifndef PARAMETER_RECORD_H
#define PARAMETER_RECORD_H

#include <stdint.h>

#define PARAMETER_RECORD_SIZE            (32U)
#define PARAMETER_RECORD_COMMIT_OFFSET   (28U)

typedef struct
{
    uint16_t frequency_x100;
    int16_t target_temperature_x10;
    uint8_t control_mode;
} MasterParameters;

typedef enum
{
    PARAMETER_RECORD_OK = 0,
    PARAMETER_RECORD_NULL_POINTER,
    PARAMETER_RECORD_INVALID_PARAMETER,
    PARAMETER_RECORD_INVALID_FORMAT,
    PARAMETER_RECORD_CRC_ERROR,
    PARAMETER_RECORD_NOT_COMMITTED
} ParameterRecordStatus;

uint32_t ParameterRecord_Crc32(const uint8_t *data, uint16_t length);
uint8_t ParameterRecord_ParametersValid(const MasterParameters *parameters);
ParameterRecordStatus ParameterRecord_Encode(const MasterParameters *parameters,
                                             uint32_t generation,
                                             uint8_t *record,
                                             uint16_t capacity);
ParameterRecordStatus ParameterRecord_Decode(const uint8_t *record,
                                             uint16_t length,
                                             MasterParameters *parameters,
                                             uint32_t *generation);
uint8_t ParameterRecord_SelectNewest(const uint8_t *slot_a,
                                     const uint8_t *slot_b,
                                     MasterParameters *parameters,
                                     uint32_t *generation,
                                     uint8_t *selected_slot);

#endif /* PARAMETER_RECORD_H */
