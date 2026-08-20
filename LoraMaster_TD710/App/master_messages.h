#ifndef MASTER_MESSAGES_H
#define MASTER_MESSAGES_H

#include <stdint.h>

#include "lora_protocol.h"

#define MASTER_CONTROL_MODE_AUTO          (0U)
#define MASTER_CONTROL_MODE_MANUAL_RUN    (1U)
#define MASTER_CONTROL_MODE_MANUAL_STOP   (2U)

#define MASTER_FAN_STATE_STOPPED          (0U)
#define MASTER_FAN_STATE_RUNNING          (1U)
#define MASTER_FAN_STATE_UNKNOWN          (2U)

#define VFD_ACTION_RUN_FORWARD            (0x0001U)
#define VFD_ACTION_STOP_DECELERATE         (0x0003U)

typedef enum
{
    MASTER_EVENT_NONE = 0,
    MASTER_EVENT_LORA_MESSAGE,
    MASTER_EVENT_VFD_RESULT
} MasterEventType;

typedef enum
{
    VFD_JOB_ORIGIN_REMOTE = 0,
    VFD_JOB_ORIGIN_AUTOMATIC,
    VFD_JOB_ORIGIN_RESTORE,
    VFD_JOB_ORIGIN_SAFETY_STOP
} VfdJobOrigin;

typedef enum
{
    VFD_RESULT_OK = 0,
    VFD_RESULT_TIMEOUT,
    VFD_RESULT_EXCEPTION,
    VFD_RESULT_CRC_ERROR,
    VFD_RESULT_WRONG_REPLY,
    VFD_RESULT_UART_ERROR,
    VFD_RESULT_TX_ERROR,
    VFD_RESULT_CANCELED
} VfdResultCode;

typedef struct
{
    uint16_t flow_id;
    uint16_t frequency_x100;
    uint16_t action;
    uint32_t epoch;
    uint8_t request_type;
    uint8_t origin;
} VfdJob;

typedef struct
{
    uint16_t flow_id;
    uint16_t frequency_x100;
    uint16_t action;
    uint32_t epoch;
    uint8_t request_type;
    uint8_t origin;
    uint8_t code;
    uint8_t exception_code;
    uint8_t attempts;
} VfdResult;

typedef struct
{
    MasterEventType type;
    union
    {
        LoRaMessage lora_message;
        VfdResult vfd_result;
    } data;
} MasterEvent;

typedef struct
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    int16_t target_temperature_x10;
    uint16_t frequency_x100;
    uint8_t control_mode;
    uint8_t fan_state;
    uint8_t temperature_valid;
} MasterUiSnapshot;

#endif /* MASTER_MESSAGES_H */
