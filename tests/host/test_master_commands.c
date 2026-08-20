#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_parameter_store.h"
#include "lora_protocol.h"
#include "master_identity.h"
#include "master_protocol_values.h"
#include "master_queues.h"
#include "master_runtime.h"

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

static LoRaMessage MakeCommand(uint8_t type, uint16_t flow_id, uint16_t value)
{
    LoRaMessage message;

    memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION;
    message.type = type;
    message.source_role = LORA_ROLE_CONTROL_ROOM;
    message.source_group = 0U;
    message.destination_role = LORA_ROLE_MASTER;
    message.destination_group = 1U;
    message.flow_id = flow_id;
    if ((type == LORA_MSG_SET_FREQ) || (type == LORA_MSG_SET_TARGET_TEMP))
    {
        message.payload_length = 2U;
        message.payload[0] = (uint8_t)value;
        message.payload[1] = (uint8_t)(value >> 8U);
    }
    return message;
}

static void PushLoRa(const LoRaMessage *message)
{
    MasterEvent event;

    memset(&event, 0, sizeof(event));
    event.type = MASTER_EVENT_LORA_MESSAGE;
    event.data.lora_message = *message;
    CHECK(MasterQueues_SendEvent(&event, 0U) == pdPASS);
}

static void PushVfdResult(const VfdJob *job,
                          uint8_t code,
                          uint8_t exception_code)
{
    MasterEvent event;

    memset(&event, 0, sizeof(event));
    event.type = MASTER_EVENT_VFD_RESULT;
    event.data.vfd_result.flow_id = job->flow_id;
    event.data.vfd_result.frequency_x100 = job->frequency_x100;
    event.data.vfd_result.action = job->action;
    event.data.vfd_result.epoch = job->epoch;
    event.data.vfd_result.request_type = job->request_type;
    event.data.vfd_result.origin = job->origin;
    event.data.vfd_result.code = code;
    event.data.vfd_result.exception_code = exception_code;
    CHECK(MasterQueues_SendEvent(&event, 0U) == pdPASS);
}

static void ResetRuntime(void)
{
    CHECK(MasterQueues_Init() == 1U);
    FakeParameterStore_Reset();
    MasterIdentity_Init(1U);
    MasterRuntime_Init();
}

static LoRaMessage PopLoRa(uint8_t expected_type)
{
    LoRaMessage message;

    memset(&message, 0, sizeof(message));
    CHECK(MasterQueues_ReceiveLoRa(&message, 0U) == pdPASS);
    CHECK(message.type == expected_type);
    return message;
}

static void TestQueryDuplicateAndConflict(void)
{
    LoRaMessage command;
    LoRaMessage ack;
    LoRaMessage result;

    ResetRuntime();
    command = MakeCommand(LORA_MSG_QUERY_STATUS, 500U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(0U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(ack.payload[0] == MASTER_ACK_ACCEPTED);
    CHECK(result.payload[0] == MASTER_ERROR_NONE);
    CHECK(result.payload[1] == MASTER_CONTROL_MODE_AUTO);
    CHECK(result.payload[2] == MASTER_FAN_STATE_UNKNOWN);
    CHECK((uint16_t)((uint16_t)result.payload[3] |
                     (uint16_t)((uint16_t)result.payload[4] << 8U)) == 3000U);
    CHECK(FakeParameterStore_GetSaveCount() == 0U);

    PushLoRa(&command);
    MasterRuntime_ProcessOne(1U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(ack.payload[0] == MASTER_ACK_DUPLICATE);
    CHECK(result.payload[0] == MASTER_ERROR_NONE);

    command = MakeCommand(LORA_MSG_MANUAL_RUN, 500U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(2U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    CHECK(ack.payload[0] == MASTER_ACK_REJECTED);
    CHECK(ack.payload[1] == MASTER_ERROR_FLOW_CONFLICT);
}

static void TestTargetAndFlashFailure(void)
{
    LoRaMessage command;
    LoRaMessage ack;
    LoRaMessage result;

    ResetRuntime();
    command = MakeCommand(LORA_MSG_SET_TARGET_TEMP, 510U, 270U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(0U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(ack.payload[0] == MASTER_ACK_ACCEPTED);
    CHECK(result.payload[0] == MASTER_ERROR_NONE);
    CHECK((int16_t)((uint16_t)result.payload[5] |
                    (uint16_t)((uint16_t)result.payload[6] << 8U)) == 270);
    CHECK(FakeParameterStore_GetSaveCount() == 1U);
    CHECK(FakeParameterStore_GetLastSaved()->target_temperature_x10 == 270);

    PushLoRa(&command);
    MasterRuntime_ProcessOne(1U, 0U);
    (void)PopLoRa(LORA_MSG_ACK);
    (void)PopLoRa(LORA_MSG_RESULT);
    CHECK(FakeParameterStore_GetSaveCount() == 1U);

    FakeParameterStore_SetSaveStatus(PARAMETER_STORE_FLASH_ERROR);
    command = MakeCommand(LORA_MSG_SET_TARGET_TEMP, 511U, 280U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(2U, 0U);
    (void)PopLoRa(LORA_MSG_ACK);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(result.payload[0] == MASTER_ERROR_FLASH);
    CHECK((int16_t)((uint16_t)result.payload[5] |
                    (uint16_t)((uint16_t)result.payload[6] << 8U)) == 270);
    CHECK(MasterRuntimeDiag.parameters_dirty == 1U);
}

static void TestManualStopAndReturnAuto(void)
{
    LoRaMessage command;
    LoRaMessage ack;
    LoRaMessage result;
    VfdJob job;

    ResetRuntime();
    command = MakeCommand(LORA_MSG_MANUAL_STOP, 600U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(0U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    CHECK(ack.payload[0] == MASTER_ACK_ACCEPTED);
    CHECK(MasterQueues_ReceiveVfdJob(&job, 0U) == pdPASS);
    CHECK(job.action == VFD_ACTION_STOP_DECELERATE);
    CHECK(job.origin == VFD_JOB_ORIGIN_REMOTE);
    CHECK(FakeParameterStore_GetSaveCount() == 1U);
    CHECK(FakeParameterStore_GetLastSaved()->control_mode ==
          MASTER_CONTROL_MODE_MANUAL_STOP);

    PushLoRa(&command);
    MasterRuntime_ProcessOne(1U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    CHECK(ack.payload[0] == MASTER_ACK_DUPLICATE);
    CHECK(MasterQueues_ReceiveVfdJob(&job, 0U) == pdFAIL);

    PushVfdResult(&job, VFD_RESULT_OK, 0U);
    MasterRuntime_ProcessOne(2U, 0U);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(result.payload[0] == MASTER_ERROR_NONE);
    CHECK(result.payload[1] == MASTER_CONTROL_MODE_MANUAL_STOP);
    CHECK(result.payload[2] == MASTER_FAN_STATE_STOPPED);

    command = MakeCommand(LORA_MSG_SET_AUTO, 601U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(3U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(ack.payload[0] == MASTER_ACK_ACCEPTED);
    CHECK(result.payload[0] == MASTER_ERROR_NONE);
    CHECK(result.payload[1] == MASTER_CONTROL_MODE_AUTO);
}

static void TestVfdSuccessTimeoutAndBusy(void)
{
    LoRaMessage command;
    LoRaMessage ack;
    LoRaMessage result;
    VfdJob old_job;
    VfdJob stop_job;

    ResetRuntime();
    command = MakeCommand(LORA_MSG_SET_FREQ, 700U, 5000U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(0U, 0U);
    (void)PopLoRa(LORA_MSG_ACK);
    CHECK(MasterQueues_ReceiveVfdJob(&old_job, 0U) == pdPASS);

    command = MakeCommand(LORA_MSG_MANUAL_STOP, 700U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(1U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    CHECK(ack.payload[1] == MASTER_ERROR_FLOW_CONFLICT);
    command = MakeCommand(LORA_MSG_MANUAL_STOP, 701U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(2U, 0U);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(result.flow_id == 700U);
    CHECK(result.payload[0] == MASTER_ERROR_STATE_NOT_ALLOWED);
    ack = PopLoRa(LORA_MSG_ACK);
    CHECK(ack.flow_id == 701U);
    CHECK(ack.payload[0] == MASTER_ACK_ACCEPTED);
    CHECK(MasterQueues_ReceiveVfdJob(&stop_job, 0U) == pdPASS);
    CHECK(stop_job.action == VFD_ACTION_STOP_DECELERATE);

    /* 旧频率命令的迟到成功回包不能改写手动停机状态。 */
    PushVfdResult(&old_job, VFD_RESULT_OK, 0U);
    MasterRuntime_ProcessOne(3U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&result, 0U) == pdFAIL);

    PushVfdResult(&stop_job, VFD_RESULT_OK, 0U);
    MasterRuntime_ProcessOne(4U, 0U);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(result.payload[0] == MASTER_ERROR_NONE);
    CHECK(result.payload[1] == MASTER_CONTROL_MODE_MANUAL_STOP);
    CHECK(result.payload[2] == MASTER_FAN_STATE_STOPPED);

    command = MakeCommand(LORA_MSG_MANUAL_RUN, 702U, 0U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(5U, 0U);
    (void)PopLoRa(LORA_MSG_ACK);
    CHECK(MasterQueues_ReceiveVfdJob(&old_job, 0U) == pdPASS);
    PushVfdResult(&old_job, VFD_RESULT_TIMEOUT, 0U);
    MasterRuntime_ProcessOne(6U, 0U);
    result = PopLoRa(LORA_MSG_RESULT);
    CHECK(result.payload[0] == MASTER_ERROR_VFD_TIMEOUT);
    CHECK(result.payload[2] == MASTER_FAN_STATE_UNKNOWN);
}

static void TestInvalidParameters(void)
{
    LoRaMessage command;
    LoRaMessage ack;
    LoRaMessage output;
    VfdJob output_job;

    ResetRuntime();
    command = MakeCommand(LORA_MSG_SET_FREQ, 800U, 5001U);
    PushLoRa(&command);
    MasterRuntime_ProcessOne(0U, 0U);
    ack = PopLoRa(LORA_MSG_ACK);
    CHECK(ack.payload[0] == MASTER_ACK_REJECTED);
    CHECK(ack.payload[1] == MASTER_ERROR_INVALID_PARAMETER);
    CHECK(MasterQueues_ReceiveVfdJob(&output_job, 0U) == pdFAIL);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);
}

int main(void)
{
    TestQueryDuplicateAndConflict();
    TestTargetAndFlashFailure();
    TestManualStopAndReturnAuto();
    TestVfdSuccessTimeoutAndBusy();
    TestInvalidParameters();
    printf("master_commands: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
