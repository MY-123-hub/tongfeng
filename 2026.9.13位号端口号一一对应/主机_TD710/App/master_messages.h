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
    MASTER_EVENT_VFD_RESULT,
    /* A validated 0x83 write reported by the DGUS receive task. */
    MASTER_EVENT_DGUS_WRITE
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
        struct
        {
            uint16_t address;
            uint16_t value;
        } dgus_write;
    } data;
} MasterEvent;

typedef struct
{
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint32_t pressure_pa;
    uint32_t sample_tick;
    uint8_t valid;
    uint8_t error_code;
} MasterEnvironmentSample;

typedef struct
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    int16_t target_temperature_x10;
    int16_t average_temperature_x10;
    uint16_t average_humidity_x10;
    uint16_t target_humidity_x10;
    int16_t environment_temperature_x10;
    uint16_t frequency_x100;
    uint16_t environment_humidity_x10;
    uint32_t environment_pressure_pa;
    uint32_t slave_environment_pressure_pa;
    uint8_t control_mode;
    uint8_t fan_state;
    uint8_t temperature_valid;
    uint8_t humidity_valid;
    uint8_t environment_valid;
    uint8_t slave_environment_pressure_valid;
    uint8_t target_humidity_configured;
    uint8_t schedule_enabled;
    /* Incremented only for a control-room setpoint update. */
    uint32_t target_temperature_screen_generation;
} MasterUiSnapshot;

#endif /* MASTER_MESSAGES_H */
