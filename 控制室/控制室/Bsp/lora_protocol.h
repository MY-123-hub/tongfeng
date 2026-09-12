#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include <stdint.h>

#define LORA_PROTOCOL_HEAD_0               0xAAU
#define LORA_PROTOCOL_HEAD_1               0x55U
#define LORA_PROTOCOL_VERSION              0x02U
#define LORA_PROTOCOL_HEADER_SIZE          11U
#define LORA_PROTOCOL_CRC_SIZE             2U
#define LORA_PROTOCOL_MAX_PAYLOAD          96U
#define LORA_PROTOCOL_FRAME_MAX_SIZE       (LORA_PROTOCOL_HEADER_SIZE + LORA_PROTOCOL_MAX_PAYLOAD + LORA_PROTOCOL_CRC_SIZE)
#define LORA_PROTOCOL_TEMPERATURE_COUNT    36U
#define LORA_PROTOCOL_TEMPERATURE_BYTES    76U
#define LORA_PROTOCOL_ENVIRONMENT_BYTES    86U
#define LORA_PROTOCOL_TEMPERATURE_INVALID  (-32767 - 1)
#define LORA_PROTOCOL_HUMIDITY_INVALID     0xFFFFU
#define LORA_PROTOCOL_PRESSURE_INVALID     0xFFFFFFFFUL
#define LORA_PROTOCOL_RAIN_UNAVAILABLE     0xFFFFU

typedef enum
{
    LORA_ROLE_CONTROL = 0x01U,
    LORA_ROLE_MASTER  = 0x02U,
    LORA_ROLE_SLAVE   = 0x03U,
    LORA_ROLE_PC      = 0x04U
} LoraProtocolRole;

typedef enum
{
    LORA_TYPE_READ_TEMP = 0x01U,
    LORA_TYPE_TEMP_36   = 0x02U,
    LORA_TYPE_READ_ENV  = 0x03U,
    LORA_TYPE_ENV_DATA  = 0x04U,
    LORA_TYPE_ACK       = 0x20U,
    LORA_TYPE_RESULT    = 0x21U,
    LORA_TYPE_HOST_LIST = 0x30U,
    LORA_TYPE_ERROR     = 0x7EU
} LoraProtocolType;

typedef struct
{
    uint8_t type;
    uint8_t src_role;
    uint8_t src_group;
    uint8_t dst_role;
    uint8_t dst_group;
    uint16_t flow_id;
    uint8_t data_len;
    uint8_t data[LORA_PROTOCOL_MAX_PAYLOAD];
} LoraProtocolFrame;

#endif /* LORA_PROTOCOL_H */
