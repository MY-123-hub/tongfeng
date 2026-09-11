#include "master_runtime.h"

#include <string.h>

#include "auto_control.h"
#include "command_service.h"
#include "master_config.h"
#include "master_identity.h"
#include "master_ingress.h"
#include "master_messages.h"
#include "master_protocol_values.h"
#include "master_queues.h"
#include "master_temperature.h"
#include "parameter_store.h"

MasterRuntimeDiagnostics MasterRuntimeDiag;

static MasterTemperatureService g_temperature_service;
static CommandService g_command_service;
static AutoControlState g_auto_control;
static MasterParameters g_parameters;
static MasterUiSnapshot g_ui_snapshot;
static MasterEvent g_runtime_event;
static MasterEnvironmentSample g_environment_sample;
static int16_t g_slave_bme_temperature_x10;

typedef enum
{
    MASTER_ENV_IDLE = 0,
    MASTER_ENV_WAIT_SLAVE,
    MASTER_ENV_REPLY_PENDING,
    MASTER_ENV_ERROR_PENDING
} MasterEnvironmentState;

typedef struct
{
    uint8_t cache[LORA_PROTOCOL_ENV_PAYLOAD_SIZE];
    uint32_t cache_tick;
    uint32_t request_tick;
    uint16_t pending_flow_id;
    uint8_t cache_valid;
    MasterEnvironmentState state;
    uint8_t pending_error;
} MasterEnvironmentService;

static MasterEnvironmentService g_environment_service;
static volatile uint32_t g_control_epoch;
static uint32_t g_auto_pending_epoch;
static uint8_t g_fan_state;
static uint8_t g_auto_vfd_pending;
static uint8_t g_safety_stop_required;
static uint8_t g_safety_stop_pending;
static uint32_t g_safety_stop_retry_tick;
static MasterParameters g_persist_parameters;
static uint32_t g_flash_save_tick;
static uint16_t g_flash_origin_flow_id;
static uint8_t g_flash_save_pending;
static uint8_t g_flash_failure_reported;

typedef struct
{
    MasterParameters candidate;
    uint16_t flow_id;
    uint8_t type;
    uint8_t active;
    uint8_t mode_locked;
} MasterPendingCommandContext;

static MasterPendingCommandContext g_pending_command;

static void MasterRuntime_SetAddress(LoRaMessage *message,
                                     uint8_t type,
                                     uint8_t destination_role,
                                     uint8_t destination_group,
                                     uint16_t flow_id)
{
    memset(message, 0, sizeof(*message));
    message->version = LORA_PROTOCOL_VERSION;
    message->type = type;
    message->source_role = LORA_ROLE_MASTER;
    message->source_group = MasterIdentity_GetGroup();
    message->destination_role = destination_role;
    message->destination_group = destination_group;
    message->flow_id = flow_id;
}

static uint16_t MasterRuntime_ReadU16(const uint8_t *payload)
{
    return (uint16_t)((uint16_t)payload[0] |
                      (uint16_t)((uint16_t)payload[1] << 8U));
}

static void MasterRuntime_WriteU16(uint8_t *payload, uint16_t value)
{
    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)(value >> 8U);
}

static void MasterRuntime_WriteU32(uint8_t *payload, uint32_t value)
{
    payload[0] = (uint8_t)(value & 0xFFUL);
    payload[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    payload[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    payload[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}

static uint8_t MasterRuntime_BmeIsFresh(uint32_t now_ms)
{
    return ((g_environment_sample.valid != 0U) &&
            ((uint32_t)(now_ms - g_environment_sample.sample_tick) <=
             MASTER_BME_CACHE_FRESH_MS)) ? 1U : 0U;
}

static void MasterRuntime_UpdateUi(void)
{
    g_ui_snapshot.target_temperature_x10 =
        g_parameters.target_temperature_x10;
    g_ui_snapshot.frequency_x100 = g_parameters.frequency_x100;
    g_ui_snapshot.control_mode = g_parameters.control_mode;
    g_ui_snapshot.fan_state = g_fan_state;
    (void)MasterQueues_OverwriteUi(&g_ui_snapshot);
}

static uint8_t MasterRuntime_QueueAck(uint16_t flow_id,
                                     uint8_t ack_status,
                                     uint8_t reason)
{
    LoRaMessage message;

    MasterRuntime_SetAddress(&message, LORA_MSG_ACK,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    message.payload_length = 2U;
    message.payload[0] = ack_status;
    message.payload[1] = reason;
    if (MasterQueues_SendLoRa(&message, 0U) != pdPASS)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static void MasterRuntime_BuildResultPayload(uint8_t result_code,
                                             uint8_t *payload)
{
    uint16_t target_raw = (uint16_t)g_parameters.target_temperature_x10;

    payload[0] = result_code;
    payload[1] = g_parameters.control_mode;
    payload[2] = g_fan_state;
    payload[3] = (uint8_t)(g_parameters.frequency_x100 & 0x00FFU);
    payload[4] = (uint8_t)(g_parameters.frequency_x100 >> 8U);
    payload[5] = (uint8_t)(target_raw & 0x00FFU);
    payload[6] = (uint8_t)(target_raw >> 8U);
}

static uint8_t MasterRuntime_QueueResult(uint16_t flow_id,
                                        const uint8_t *payload)
{
    LoRaMessage message;

    MasterRuntime_SetAddress(&message, LORA_MSG_RESULT,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    message.payload_length = 7U;
    memcpy(message.payload, payload, 7U);
    if (MasterQueues_SendLoRa(&message, 0U) != pdPASS)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static void MasterRuntime_CompleteCommand(uint8_t result_code)
{
    uint8_t result_payload[7];
    uint16_t flow_id = g_pending_command.flow_id;

    MasterRuntime_BuildResultPayload(result_code, result_payload);
    (void)CommandService_Complete(&g_command_service, result_payload);
    memset(&g_pending_command, 0, sizeof(g_pending_command));
    MasterRuntime_UpdateUi();
    (void)MasterRuntime_QueueResult(flow_id, result_payload);
}

static void MasterRuntime_AbortPendingCommand(void)
{
    uint8_t result_payload[7];
    uint16_t flow_id;

    if (g_pending_command.active == 0U)
    {
        return;
    }

    flow_id = g_pending_command.flow_id;
    MasterRuntime_BuildResultPayload(MASTER_ERROR_STATE_NOT_ALLOWED,
                                     result_payload);
    (void)CommandService_Complete(&g_command_service, result_payload);
    memset(&g_pending_command, 0, sizeof(g_pending_command));
    (void)MasterRuntime_QueueResult(flow_id, result_payload);
}

static uint8_t MasterRuntime_QueueError(uint16_t flow_id, uint8_t error_code)
{
    LoRaMessage message;

    MasterRuntime_SetAddress(&message, LORA_MSG_ERROR,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    message.payload_length = 1U;
    message.payload[0] = error_code;
    if (MasterQueues_SendLoRa(&message, 0U) != pdPASS)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static uint8_t MasterRuntime_QueueSlaveRead(uint8_t request_type,
                                            uint16_t flow_id,
                                            uint8_t read_mode)
{
    LoRaMessage message;
    uint8_t local_group = MasterIdentity_GetGroup();

    MasterRuntime_SetAddress(&message, request_type,
                             LORA_ROLE_SLAVE, local_group, flow_id);
    message.payload_length = 1U;
    message.payload[0] = read_mode;
    if (MasterQueues_SendLoRa(&message, 0U) != pdPASS)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static uint8_t MasterRuntime_QueueTemperature(uint16_t flow_id,
                                              const int16_t *temperatures,
                                              uint32_t now_ms)
{
    LoRaMessage message;
    uint32_t i;

    MasterRuntime_SetAddress(&message, LORA_MSG_TEMP_36,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    message.payload_length = LORA_PROTOCOL_TEMP_PAYLOAD_SIZE;
    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        uint16_t raw = (uint16_t)temperatures[i];
        message.payload[i * 2U] = (uint8_t)(raw & 0x00FFU);
        message.payload[(i * 2U) + 1U] = (uint8_t)((raw >> 8U) & 0x00FFU);
    }
    MasterRuntime_WriteU16(&message.payload[72],
                           (uint16_t)g_slave_bme_temperature_x10);
    MasterRuntime_WriteU16(
        &message.payload[74],
        (uint16_t)((MasterRuntime_BmeIsFresh(now_ms) != 0U) ?
                   g_environment_sample.temperature_x10 :
                   LORA_PROTOCOL_TEMPERATURE_INVALID));

    if (MasterQueues_SendLoRa(&message, 0U) != pdPASS)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static uint8_t MasterRuntime_QueueEnvironment(uint16_t flow_id,
                                              const uint8_t *slave_payload,
                                              uint32_t now_ms)
{
    LoRaMessage message;

    if (slave_payload == NULL)
    {
        return 0U;
    }
    MasterRuntime_SetAddress(&message, LORA_MSG_ENV_DATA,
                             LORA_ROLE_CONTROL_ROOM, 0U, flow_id);
    message.payload_length = LORA_PROTOCOL_ENV_PAYLOAD_SIZE;
    memcpy(message.payload, slave_payload, LORA_PROTOCOL_ENV_PAYLOAD_SIZE);

    if (MasterRuntime_BmeIsFresh(now_ms) != 0U)
    {
        MasterRuntime_WriteU16(&message.payload[74],
                               g_environment_sample.humidity_x10);
        MasterRuntime_WriteU32(&message.payload[80],
                               g_environment_sample.pressure_pa);
    }
    else
    {
        MasterRuntime_WriteU16(&message.payload[74],
                               LORA_PROTOCOL_HUMIDITY_INVALID);
        MasterRuntime_WriteU32(&message.payload[80],
                               LORA_PROTOCOL_PRESSURE_INVALID);
    }

    if (MasterQueues_SendLoRa(&message, 0U) != pdPASS)
    {
        MasterRuntimeDiag.lora_queue_failure_count++;
        return 0U;
    }
    return 1U;
}

static void MasterRuntime_DecodeTemperatures(const LoRaMessage *message,
                                             int16_t *temperatures)
{
    uint32_t i;

    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        uint16_t raw = (uint16_t)(
            (uint16_t)message->payload[i * 2U] |
            (uint16_t)((uint16_t)message->payload[(i * 2U) + 1U] << 8U));
        temperatures[i] = (int16_t)raw;
    }
}

static uint8_t MasterRuntime_SaveParameters(const MasterParameters *parameters)
{
    MasterParameters safe_parameters;
    ParameterStoreStatus status;

    safe_parameters = *parameters;
    /* 运行模式永不做掉电恢复；持久记录固定为安全停机。 */
    safe_parameters.control_mode = MASTER_CONTROL_MODE_MANUAL_STOP;
    status = ParameterStore_Save(&safe_parameters);

    if ((status == PARAMETER_STORE_SAVED) ||
        (status == PARAMETER_STORE_UNCHANGED))
    {
        return 1U;
    }
    MasterRuntimeDiag.flash_failure_count++;
    return 0U;
}

static void MasterRuntime_ScheduleParameterSave(const MasterParameters *parameters,
                                                uint16_t flow_id,
                                                uint32_t now_ms)
{
    g_persist_parameters = *parameters;
    g_persist_parameters.control_mode = MASTER_CONTROL_MODE_MANUAL_STOP;
    g_flash_origin_flow_id = flow_id;
    g_flash_save_tick = now_ms + MASTER_FLASH_DEBOUNCE_MS;
    g_flash_save_pending = 1U;
    g_flash_failure_reported = 0U;
    MasterRuntimeDiag.parameters_dirty = 1U;
}

static void MasterRuntime_ProcessParameterSave(uint32_t now_ms)
{
    if ((g_flash_save_pending == 0U) ||
        (((uint32_t)(now_ms - g_flash_save_tick) & 0x80000000UL) != 0U))
    {
        return;
    }

    if (MasterRuntime_SaveParameters(&g_persist_parameters) != 0U)
    {
        g_flash_save_pending = 0U;
        g_flash_failure_reported = 0U;
        MasterRuntimeDiag.parameters_dirty = 0U;
        return;
    }

    g_flash_save_tick = now_ms + MASTER_FLASH_RETRY_MS;
    if ((g_flash_origin_flow_id != 0U) && (g_flash_failure_reported == 0U) &&
        (MasterRuntime_QueueError(g_flash_origin_flow_id, MASTER_ERROR_FLASH) != 0U))
    {
        g_flash_failure_reported = 1U;
    }
}

static uint8_t MasterRuntime_QueueRemoteVfd(const LoRaMessage *message)
{
    VfdJob job;
    BaseType_t result;

    memset(&job, 0, sizeof(job));
    job.flow_id = message->flow_id;
    job.frequency_x100 = g_pending_command.candidate.frequency_x100;
    job.epoch = g_control_epoch;
    job.request_type = message->type;
    job.origin = VFD_JOB_ORIGIN_REMOTE;
    if (message->type == LORA_MSG_MANUAL_RUN)
    {
        job.action = VFD_ACTION_RUN_FORWARD;
    }
    else if (message->type == LORA_MSG_MANUAL_STOP)
    {
        job.action = VFD_ACTION_STOP_DECELERATE;
    }
    else if (g_pending_command.candidate.control_mode ==
             MASTER_CONTROL_MODE_MANUAL_STOP)
    {
        job.action = VFD_ACTION_STOP_DECELERATE;
    }
    else if (g_pending_command.candidate.control_mode ==
             MASTER_CONTROL_MODE_MANUAL_RUN)
    {
        job.action = VFD_ACTION_RUN_FORWARD;
    }
    else
    {
        job.action = (g_fan_state == MASTER_FAN_STATE_RUNNING) ?
                     VFD_ACTION_RUN_FORWARD : VFD_ACTION_STOP_DECELERATE;
    }
    
    result = (message->type == LORA_MSG_MANUAL_STOP) ?
             MasterQueues_SendEmergencyVfdJob(&job) :
             MasterQueues_SendVfdJob(&job, 0U);
    if ((result == pdPASS) && (message->type == LORA_MSG_MANUAL_STOP))
    {
        g_safety_stop_pending = 1U;
    }
    return (result == pdPASS) ? 1U : 0U;
}

static void MasterRuntime_StartNewCommand(const LoRaMessage *message)
{
    uint16_t raw;

    if (CommandService_ValidateParameters(message) == 0U)
    {
        MasterRuntimeDiag.command_reject_count++;
        (void)MasterRuntime_QueueAck(message->flow_id,
                                    MASTER_ACK_REJECTED,
                                    MASTER_ERROR_INVALID_PARAMETER);
        return;
    }
    if (MasterRuntime_QueueAck(message->flow_id,
                               MASTER_ACK_ACCEPTED,
                               MASTER_ERROR_NONE) == 0U)
    {
        return;
    }
    if (CommandService_Begin(&g_command_service, message) == 0U)
    {
        return;
    }

    MasterRuntimeDiag.command_accept_count++;
    memset(&g_pending_command, 0, sizeof(g_pending_command));
    g_pending_command.active = 1U;
    g_pending_command.flow_id = message->flow_id;
    g_pending_command.type = message->type;
    g_pending_command.candidate = g_parameters;

    if (message->type == LORA_MSG_SET_TARGET_TEMP)
    {
        raw = MasterRuntime_ReadU16(message->payload);
        g_pending_command.candidate.target_temperature_x10 = (int16_t)raw;
        if (MasterRuntime_SaveParameters(&g_pending_command.candidate) != 0U)
        {
            g_parameters = g_pending_command.candidate;
            g_flash_save_pending = 0U;
            g_flash_failure_reported = 0U;
            MasterRuntimeDiag.parameters_dirty = 0U;
            g_control_epoch++;
            AutoControl_Init(&g_auto_control);
            MasterRuntime_CompleteCommand(MASTER_ERROR_NONE);
        }
        else
        {
            MasterRuntime_CompleteCommand(MASTER_ERROR_FLASH);
        }
        return;
    }

    if (message->type == LORA_MSG_SET_AUTO)
    {
        g_pending_command.candidate.control_mode = MASTER_CONTROL_MODE_AUTO;
        g_parameters = g_pending_command.candidate;
        g_control_epoch++;
        g_safety_stop_required = 0U;
        g_safety_stop_pending = 0U;
        AutoControl_Init(&g_auto_control);
        MasterRuntime_CompleteCommand(MASTER_ERROR_NONE);
        return;
    }

    if (message->type == LORA_MSG_QUERY_STATUS)
    {
        MasterRuntime_CompleteCommand(MASTER_ERROR_NONE);
        return;
    }

    if (message->type == LORA_MSG_SET_FREQ)
    {
        g_pending_command.candidate.frequency_x100 =
            MasterRuntime_ReadU16(message->payload);
    }
    else if (message->type == LORA_MSG_MANUAL_RUN)
    {
        g_pending_command.candidate.control_mode =
            MASTER_CONTROL_MODE_MANUAL_RUN;
        g_safety_stop_required = 0U;
        g_safety_stop_pending = 0U;
    }
    else if (message->type == LORA_MSG_MANUAL_STOP)
    {
        g_pending_command.candidate.control_mode =
            MASTER_CONTROL_MODE_MANUAL_STOP;
        g_parameters.control_mode = MASTER_CONTROL_MODE_MANUAL_STOP;
        g_pending_command.mode_locked = 1U;
        g_safety_stop_required = 1U;
        MasterRuntime_UpdateUi();
    }

    g_control_epoch++;
    if (MasterRuntime_QueueRemoteVfd(message) == 0U)
    {
        /* 手动停机已经锁定模式；队列短暂不可用时由安全停机任务持续重试。 */
        if (message->type != LORA_MSG_MANUAL_STOP)
        {
            MasterRuntime_CompleteCommand(MASTER_ERROR_BUSY);
        }
    }
}

static void MasterRuntime_HandleCommand(const LoRaMessage *message)
{
    CommandClassification classification;
    uint8_t cached_result[7];

    classification = CommandService_Classify(&g_command_service, message);
    if ((message->type == LORA_MSG_MANUAL_STOP) &&
        (classification == COMMAND_CLASS_BUSY))
    {
        /* 停机安全级别高于正常命令：取消旧命令并立即锁停。 */
        MasterRuntime_AbortPendingCommand();
        MasterRuntime_StartNewCommand(message);
        return;
    }
    if (classification == COMMAND_CLASS_NEW)
    {
        MasterRuntime_StartNewCommand(message);
    }
    else if (classification == COMMAND_CLASS_DUPLICATE_PENDING)
    {
        MasterRuntimeDiag.command_duplicate_count++;
        (void)MasterRuntime_QueueAck(message->flow_id,
                                    MASTER_ACK_DUPLICATE,
                                    MASTER_ERROR_NONE);
    }
    else if (classification == COMMAND_CLASS_DUPLICATE_COMPLETED)
    {
        MasterRuntimeDiag.command_duplicate_count++;
        if ((MasterRuntime_QueueAck(message->flow_id,
                                    MASTER_ACK_DUPLICATE,
                                    MASTER_ERROR_NONE) != 0U) &&
            (CommandService_GetCompletedResult(&g_command_service,
                                               message,
                                               cached_result) != 0U))
        {
            (void)MasterRuntime_QueueResult(message->flow_id, cached_result);
        }
    }
    else if (classification == COMMAND_CLASS_FLOW_CONFLICT)
    {
        MasterRuntimeDiag.command_conflict_count++;
        (void)MasterRuntime_QueueAck(message->flow_id,
                                    MASTER_ACK_REJECTED,
                                    MASTER_ERROR_FLOW_CONFLICT);
    }
    else
    {
        MasterRuntimeDiag.command_reject_count++;
        (void)MasterRuntime_QueueAck(message->flow_id,
                                    MASTER_ACK_REJECTED,
                                    (classification == COMMAND_CLASS_BUSY) ?
                                    MASTER_ERROR_BUSY : MASTER_ERROR_UNSUPPORTED);
    }
}

static uint8_t MasterRuntime_MapVfdError(uint8_t vfd_code)
{
    return (vfd_code == VFD_RESULT_TIMEOUT) ? MASTER_ERROR_VFD_TIMEOUT :
                                             MASTER_ERROR_VFD_RESPONSE;
}

static void MasterRuntime_HandleVfdResult(const VfdResult *result,
                                          uint32_t now_ms)
{
    MasterRuntimeDiag.vfd_result_count++;
    if (result->origin == VFD_JOB_ORIGIN_SAFETY_STOP)
    {
        g_safety_stop_pending = 0U;
        if (result->epoch != g_control_epoch)
        {
            MasterRuntimeDiag.stale_vfd_result_count++;
            return;
        }
        if (result->code == VFD_RESULT_OK)
        {
            g_fan_state = MASTER_FAN_STATE_STOPPED;
            g_safety_stop_required = 0U;
            if ((g_pending_command.active != 0U) &&
                (g_pending_command.type == LORA_MSG_MANUAL_STOP))
            {
                MasterRuntime_CompleteCommand(MASTER_ERROR_NONE);
            }
        }
        else
        {
            g_fan_state = MASTER_FAN_STATE_UNKNOWN;
            g_safety_stop_retry_tick = now_ms + MASTER_SAFETY_STOP_RETRY_MS;
        }
        MasterRuntime_UpdateUi();
        return;
    }
    if (result->origin == VFD_JOB_ORIGIN_AUTOMATIC)
    {
        if ((g_auto_vfd_pending != 0U) &&
            (result->epoch == g_auto_pending_epoch))
        {
            g_auto_vfd_pending = 0U;
        }
        if (result->epoch != g_control_epoch)
        {
            MasterRuntimeDiag.stale_vfd_result_count++;
            return;
        }
        if (result->code == VFD_RESULT_OK)
        {
            g_fan_state = (result->action == VFD_ACTION_RUN_FORWARD) ?
                          MASTER_FAN_STATE_RUNNING : MASTER_FAN_STATE_STOPPED;
        }
        else if (result->code != VFD_RESULT_CANCELED)
        {
            g_fan_state = MASTER_FAN_STATE_UNKNOWN;
        }
        MasterRuntime_UpdateUi();
        return;
    }

    if (result->origin == VFD_JOB_ORIGIN_RESTORE)
    {
        if ((result->epoch == g_control_epoch) &&
            (result->code == VFD_RESULT_OK))
        {
            g_fan_state = (result->action == VFD_ACTION_RUN_FORWARD) ?
                          MASTER_FAN_STATE_RUNNING : MASTER_FAN_STATE_STOPPED;
        }
        else
        {
            g_fan_state = MASTER_FAN_STATE_UNKNOWN;
        }
        MasterRuntime_UpdateUi();
        return;
    }

    if ((g_pending_command.active == 0U) ||
        (result->epoch != g_control_epoch) ||
        (result->flow_id != g_pending_command.flow_id) ||
        (result->request_type != g_pending_command.type))
    {
        MasterRuntimeDiag.stale_vfd_result_count++;
        return;
    }

    if (result->code != VFD_RESULT_OK)
    {
        if (g_pending_command.mode_locked != 0U)
        {
            g_parameters.control_mode = MASTER_CONTROL_MODE_MANUAL_STOP;
        }
        if (g_pending_command.type == LORA_MSG_MANUAL_STOP)
        {
            g_safety_stop_pending = 0U;
            g_safety_stop_required = 1U;
            g_safety_stop_retry_tick = now_ms + MASTER_SAFETY_STOP_RETRY_MS;
        }
        g_fan_state = MASTER_FAN_STATE_UNKNOWN;
        MasterRuntime_CompleteCommand(MasterRuntime_MapVfdError(result->code));
        return;
    }

    g_fan_state = (result->action == VFD_ACTION_RUN_FORWARD) ?
                  MASTER_FAN_STATE_RUNNING : MASTER_FAN_STATE_STOPPED;
    if (g_pending_command.type == LORA_MSG_MANUAL_STOP)
    {
        g_safety_stop_pending = 0U;
        g_safety_stop_required = 0U;
    }
    g_parameters = g_pending_command.candidate;
    if (g_pending_command.type == LORA_MSG_SET_FREQ)
    {
        MasterRuntime_ScheduleParameterSave(&g_parameters,
                                            g_pending_command.flow_id,
                                            now_ms);
    }
    MasterRuntime_CompleteCommand(MASTER_ERROR_NONE);
}

static void MasterRuntime_ProcessSafetyStop(uint32_t now_ms)
{
    VfdJob job;

    if ((g_safety_stop_required == 0U) ||
        (g_safety_stop_pending != 0U) ||
        (g_fan_state == MASTER_FAN_STATE_STOPPED) ||
        ((uint32_t)(now_ms - g_safety_stop_retry_tick) & 0x80000000UL))
    {
        return;
    }

    memset(&job, 0, sizeof(job));
    job.frequency_x100 = g_parameters.frequency_x100;
    job.action = VFD_ACTION_STOP_DECELERATE;
    job.epoch = g_control_epoch;
    job.origin = VFD_JOB_ORIGIN_SAFETY_STOP;
    if (MasterQueues_SendEmergencyVfdJob(&job) == pdPASS)
    {
        g_safety_stop_pending = 1U;
    }
}

static void MasterRuntime_ProcessAutomaticControl(uint32_t now_ms)
{
    AutoDecision decision;
    VfdJob job;
    uint8_t fresh;

    fresh = MasterTemperature_IsCacheFresh(&g_temperature_service, now_ms);
    decision = AutoControl_Step(&g_auto_control,
                                g_parameters.control_mode,
                                g_temperature_service.cache,
                                LORA_PROTOCOL_TEMP_COUNT,
                                fresh,
                                g_parameters.target_temperature_x10,
                                now_ms);
    if ((g_command_service.pending.valid != 0U) ||
        (g_auto_vfd_pending != 0U) ||
        (g_safety_stop_required != 0U) ||
        (MasterIdentity_IsValid() == 0U))
    {
        return;
    }
    if (((decision == AUTO_DECISION_RUN) &&
         (g_fan_state == MASTER_FAN_STATE_RUNNING)) ||
        ((decision == AUTO_DECISION_STOP) &&
         (g_fan_state == MASTER_FAN_STATE_STOPPED)) ||
        ((decision != AUTO_DECISION_RUN) &&
         (decision != AUTO_DECISION_STOP)))
    {
        return;
    }

    memset(&job, 0, sizeof(job));
    job.frequency_x100 = g_parameters.frequency_x100;
    job.action = (decision == AUTO_DECISION_RUN) ?
                 VFD_ACTION_RUN_FORWARD : VFD_ACTION_STOP_DECELERATE;
    job.epoch = g_control_epoch;
    job.origin = VFD_JOB_ORIGIN_AUTOMATIC;
    if (MasterQueues_SendVfdJob(&job, 0U) == pdPASS)
    {
        g_auto_vfd_pending = 1U;
        g_auto_pending_epoch = g_control_epoch;
        if (decision == AUTO_DECISION_RUN)
        {
            MasterRuntimeDiag.auto_run_request_count++;
        }
        else
        {
            MasterRuntimeDiag.auto_stop_request_count++;
        }
    }
}

static void MasterRuntime_HandleRead(const LoRaMessage *message,
                                     uint32_t now_ms)
{
    MasterTemperatureReadDecision decision;
    uint8_t env_fresh;

    MasterRuntimeDiag.read_request_count++;
    if (message->type == LORA_MSG_READ_ENV)
    {
        if ((g_temperature_service.state != MASTER_TEMP_IDLE) ||
            (g_environment_service.state != MASTER_ENV_IDLE))
        {
            if (!((g_environment_service.state == MASTER_ENV_WAIT_SLAVE) &&
                  (g_environment_service.pending_flow_id == message->flow_id)))
            {
                if (MasterRuntime_QueueError(message->flow_id,
                                             MASTER_ERROR_BUSY) != 0U)
                {
                    MasterRuntimeDiag.busy_reject_count++;
                }
            }
            return;
        }

        env_fresh = ((g_environment_service.cache_valid != 0U) &&
                     ((uint32_t)(now_ms - g_environment_service.cache_tick) <=
                      MASTER_ENV_CACHE_FRESH_MS)) ? 1U : 0U;
        g_environment_service.pending_flow_id = message->flow_id;
        if ((message->payload[0] == 0U) && (env_fresh != 0U))
        {
            g_environment_service.state = MASTER_ENV_REPLY_PENDING;
        }
        else if (MasterRuntime_QueueSlaveRead(LORA_MSG_READ_ENV,
                                              message->flow_id,
                                              message->payload[0]) != 0U)
        {
            g_environment_service.request_tick = now_ms;
            g_environment_service.state = MASTER_ENV_WAIT_SLAVE;
        }
        return;
    }

    if (g_environment_service.state != MASTER_ENV_IDLE)
    {
        if (MasterRuntime_QueueError(message->flow_id, MASTER_ERROR_BUSY) != 0U)
        {
            MasterRuntimeDiag.busy_reject_count++;
        }
        return;
    }
    decision = MasterTemperature_EvaluateRead(&g_temperature_service,
                                              message->flow_id,
                                              message->payload[0],
                                              now_ms);
    if (decision == MASTER_TEMP_READ_USE_CACHE)
    {
        MasterTemperature_BeginCacheReply(&g_temperature_service,
                                          message->flow_id);
    }
    else if (decision == MASTER_TEMP_READ_REQUEST_SLAVE)
    {
        if (MasterRuntime_QueueSlaveRead(LORA_MSG_READ_TEMP,
                                         message->flow_id,
                                         message->payload[0]) != 0U)
        {
            MasterTemperature_BeginSlaveRequest(&g_temperature_service,
                                                message->flow_id,
                                                now_ms);
        }
    }
    else if (decision == MASTER_TEMP_READ_BUSY)
    {
        if (MasterRuntime_QueueError(message->flow_id, MASTER_ERROR_BUSY) != 0U)
        {
            MasterRuntimeDiag.busy_reject_count++;
        }
    }
    else
    {
        /* 相同流水号仍在处理，不重复请求从机。 */
    }
}

static void MasterRuntime_HandleSlave(const LoRaMessage *message,
                                      uint32_t now_ms)
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];

    if (message->type == LORA_MSG_ENV_DATA)
    {
        if ((g_environment_service.state != MASTER_ENV_WAIT_SLAVE) ||
            (g_environment_service.pending_flow_id != message->flow_id))
        {
            MasterRuntimeDiag.slave_environment_reject_count++;
            return;
        }
        memcpy(g_environment_service.cache, message->payload,
               LORA_PROTOCOL_ENV_PAYLOAD_SIZE);
        g_environment_service.cache_tick = now_ms;
        g_environment_service.cache_valid = 1U;
        g_environment_service.state = MASTER_ENV_REPLY_PENDING;
        MasterRuntimeDiag.slave_environment_accept_count++;
        return;
    }
    if (message->type != LORA_MSG_TEMP_36)
    {
        return;
    }

    MasterRuntime_DecodeTemperatures(message, temperatures);
    if (MasterTemperature_AcceptSlaveData(&g_temperature_service,
                                          message->flow_id,
                                          temperatures,
                                          now_ms) == 0U)
    {
        MasterRuntimeDiag.temperature_reject_count++;
        return;
    }

    g_slave_bme_temperature_x10 =
        (int16_t)MasterRuntime_ReadU16(&message->payload[72]);
    MasterRuntimeDiag.temperature_accept_count++;
    memcpy(g_ui_snapshot.temperatures, temperatures,
           sizeof(g_ui_snapshot.temperatures));
    g_ui_snapshot.temperature_valid = 1U;
    (void)MasterQueues_OverwriteUi(&g_ui_snapshot);
}

static void MasterRuntime_ProcessPending(uint32_t now_ms)
{
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT];
    uint16_t flow_id;
    uint8_t error_code;

    if (MasterTemperature_CheckTimeout(&g_temperature_service,
                                       now_ms,
                                       MASTER_ERROR_SLAVE_TIMEOUT) != 0U)
    {
        MasterRuntimeDiag.temperature_timeout_count++;
    }

    if (MasterTemperature_GetPendingReply(&g_temperature_service,
                                          &flow_id,
                                          temperatures) != 0U)
    {
        if (MasterRuntime_QueueTemperature(flow_id, temperatures, now_ms) != 0U)
        {
            MasterTemperature_CompletePending(&g_temperature_service);
        }
        return;
    }

    if (MasterTemperature_GetPendingError(&g_temperature_service,
                                          &flow_id,
                                          &error_code) != 0U)
    {
        if (MasterRuntime_QueueError(flow_id, error_code) != 0U)
        {
            MasterTemperature_CompletePending(&g_temperature_service);
        }
    }

    if ((g_environment_service.state == MASTER_ENV_WAIT_SLAVE) &&
        ((uint32_t)(now_ms - g_environment_service.request_tick) >=
         MASTER_SLAVE_RESPONSE_TIMEOUT_MS))
    {
        g_environment_service.pending_error = MASTER_ERROR_SLAVE_TIMEOUT;
        g_environment_service.state = MASTER_ENV_ERROR_PENDING;
        MasterRuntimeDiag.slave_environment_timeout_count++;
    }

    if (g_environment_service.state == MASTER_ENV_REPLY_PENDING)
    {
        if (MasterRuntime_QueueEnvironment(
                g_environment_service.pending_flow_id,
                g_environment_service.cache, now_ms) != 0U)
        {
            g_environment_service.state = MASTER_ENV_IDLE;
        }
    }
    else if (g_environment_service.state == MASTER_ENV_ERROR_PENDING)
    {
        if (MasterRuntime_QueueError(g_environment_service.pending_flow_id,
                                     g_environment_service.pending_error) != 0U)
        {
            g_environment_service.state = MASTER_ENV_IDLE;
        }
    }
}

void MasterRuntime_Init(void)
{
    ParameterStoreStatus parameter_status;
    uint8_t loaded_mode;

    memset(&MasterRuntimeDiag, 0, sizeof(MasterRuntimeDiag));
    memset(&g_ui_snapshot, 0, sizeof(g_ui_snapshot));
    memset(&g_pending_command, 0, sizeof(g_pending_command));
    memset(&g_environment_service, 0, sizeof(g_environment_service));
    memset(&g_environment_sample, 0, sizeof(g_environment_sample));
    g_slave_bme_temperature_x10 = LORA_PROTOCOL_TEMPERATURE_INVALID;
    MasterTemperature_Init(&g_temperature_service);
    CommandService_Init(&g_command_service);
    AutoControl_Init(&g_auto_control);
    g_control_epoch = 1U;
    g_auto_pending_epoch = 0U;
    g_auto_vfd_pending = 0U;
    g_safety_stop_required = 0U;
    g_safety_stop_pending = 0U;
    g_safety_stop_retry_tick = 0U;
    memset(&g_persist_parameters, 0, sizeof(g_persist_parameters));
    g_flash_save_tick = 0U;
    g_flash_origin_flow_id = 0U;
    g_flash_save_pending = 0U;
    g_flash_failure_reported = 0U;
    g_fan_state = MASTER_FAN_STATE_UNKNOWN;
    parameter_status = ParameterStore_Load(&g_parameters);
    loaded_mode = g_parameters.control_mode;
    if (parameter_status == PARAMETER_STORE_DEFAULTS)
    {
        MasterRuntimeDiag.parameters_from_defaults = 1U;
    }

    /* 无论掉电前处于何种模式，上电后都锁定停机，等待新的运行命令。 */
    g_parameters.control_mode = MASTER_CONTROL_MODE_MANUAL_STOP;
    g_safety_stop_required = 1U;
    if ((parameter_status == PARAMETER_STORE_DEFAULTS) ||
        (loaded_mode != MASTER_CONTROL_MODE_MANUAL_STOP))
    {
        MasterRuntime_ScheduleParameterSave(&g_parameters, 0U, 0U);
    }
    MasterRuntime_UpdateUi();

    if (MasterIdentity_IsValid() == 0U)
    {
        MasterRuntimeDiag.identity_invalid = 1U;
    }
}

void MasterRuntime_ProcessOne(uint32_t now_ms, TickType_t wait_ticks)
{
    MasterIngressRoute route;

    if (MasterQueues_ReceiveEnvironment(&g_environment_sample) == pdPASS)
    {
        if (g_environment_sample.valid != 0U)
        {
            g_ui_snapshot.environment_temperature_x10 =
                g_environment_sample.temperature_x10;
            g_ui_snapshot.environment_humidity_x10 =
                g_environment_sample.humidity_x10;
            g_ui_snapshot.environment_pressure_pa =
                g_environment_sample.pressure_pa;
            g_ui_snapshot.environment_valid = 1U;
            MasterRuntimeDiag.environment_accept_count++;
        }
        else
        {
            g_ui_snapshot.environment_valid = 0U;
            MasterRuntimeDiag.environment_error_count++;
        }
        MasterRuntime_UpdateUi();
    }

    if (MasterQueues_ReceiveEvent(&g_runtime_event, wait_ticks) == pdPASS)
    {
        if (g_runtime_event.type == MASTER_EVENT_LORA_MESSAGE)
        {
            route = MasterIngress_Route(&g_runtime_event.data.lora_message,
                                        MasterIdentity_GetGroup());
            if (route == MASTER_INGRESS_CONTROL_ROOM)
            {
                MasterRuntimeDiag.routed_control_count++;
                if ((g_runtime_event.data.lora_message.type == LORA_MSG_READ_TEMP) ||
                    (g_runtime_event.data.lora_message.type == LORA_MSG_READ_ENV))
                {
                    MasterRuntime_HandleRead(&g_runtime_event.data.lora_message,
                                             now_ms);
                }
                else if (CommandService_IsControlType(
                             g_runtime_event.data.lora_message.type) != 0U)
                {
                    MasterRuntime_HandleCommand(
                        &g_runtime_event.data.lora_message);
                }
            }
            else if (route == MASTER_INGRESS_SLAVE)
            {
                MasterRuntimeDiag.routed_slave_count++;
                MasterRuntime_HandleSlave(&g_runtime_event.data.lora_message,
                                          now_ms);
            }
            else
            {
                MasterRuntimeDiag.address_drop_count++;
            }
        }
        else if (g_runtime_event.type == MASTER_EVENT_VFD_RESULT)
        {
            MasterRuntime_HandleVfdResult(&g_runtime_event.data.vfd_result,
                                          now_ms);
        }
    }

    MasterRuntime_ProcessPending(now_ms);
    MasterRuntime_ProcessSafetyStop(now_ms);
    MasterRuntime_ProcessParameterSave(now_ms);
    MasterRuntime_ProcessAutomaticControl(now_ms);
}

uint32_t MasterRuntime_GetControlEpoch(void)
{
    return g_control_epoch;
}
