#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lora_protocol.h"
#include "fake_parameter_store.h"
#include "master_identity.h"
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

static LoRaMessage MakeRead(uint8_t destination_group,
                            uint16_t flow_id,
                            uint8_t mode)
{
    LoRaMessage message;

    memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION;
    message.type = LORA_MSG_READ_TEMP;
    message.source_role = LORA_ROLE_CONTROL_ROOM;
    message.source_group = 0U;
    message.destination_role = LORA_ROLE_MASTER;
    message.destination_group = destination_group;
    message.flow_id = flow_id;
    message.payload_length = 1U;
    message.payload[0] = mode;
    return message;
}

static LoRaMessage MakeTemperature(uint8_t group, uint16_t flow_id)
{
    LoRaMessage message;
    uint32_t i;

    memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION;
    message.type = LORA_MSG_TEMP_36;
    message.source_role = LORA_ROLE_SLAVE;
    message.source_group = group;
    message.destination_role = LORA_ROLE_MASTER;
    message.destination_group = group;
    message.flow_id = flow_id;
    message.payload_length = LORA_PROTOCOL_TEMP_PAYLOAD_SIZE;
    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        uint16_t value = (uint16_t)(250U + i);
        message.payload[i * 2U] = (uint8_t)(value & 0x00FFU);
        message.payload[(i * 2U) + 1U] = (uint8_t)(value >> 8U);
    }
    message.payload[14] = 0U;
    message.payload[15] = 0U;
    return message;
}

static void SetAllTemperatures(LoRaMessage *message, int16_t value)
{
    uint16_t raw = (uint16_t)value;
    uint32_t i;

    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        message->payload[i * 2U] = (uint8_t)(raw & 0x00FFU);
        message->payload[(i * 2U) + 1U] = (uint8_t)(raw >> 8U);
    }
}

static void PushVfdResult(const VfdJob *job, uint8_t code)
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
    CHECK(MasterQueues_SendEvent(&event, 0U) == pdPASS);
}

static void PushMessage(const LoRaMessage *message)
{
    MasterEvent event;

    memset(&event, 0, sizeof(event));
    event.type = MASTER_EVENT_LORA_MESSAGE;
    event.data.lora_message = *message;
    CHECK(MasterQueues_SendEvent(&event, 0U) == pdPASS);
}

static void ResetRuntime(uint8_t group)
{
    CHECK(MasterQueues_Init() == 1U);
    FakeParameterStore_Reset();
    MasterIdentity_Init(group);
    MasterRuntime_Init();
}

static void TestStandardFlowAndCache(void)
{
    static const uint8_t expected_read[] = {
        0xAAU, 0x55U, 0x01U, 0x01U, 0x02U, 0x01U, 0x03U,
        0x01U, 0x64U, 0x00U, 0x01U, 0x01U, 0x5FU, 0xD8U
    };
    LoRaMessage input;
    LoRaMessage output;
    uint8_t frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t frame_length;

    ResetRuntime(1U);
    input = MakeRead(1U, 100U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(1000U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_READ_TEMP);
    CHECK(output.source_role == LORA_ROLE_MASTER);
    CHECK(output.destination_role == LORA_ROLE_SLAVE);
    CHECK(output.flow_id == 100U);
    CHECK(output.payload[0] == 1U);
    CHECK(LoRaProtocol_Encode(&output, frame, sizeof(frame), &frame_length) ==
          LORA_PROTOCOL_OK);
    CHECK(frame_length == sizeof(expected_read));
    CHECK(memcmp(frame, expected_read, sizeof(expected_read)) == 0);

    input = MakeTemperature(1U, 100U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(1100U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_TEMP_36);
    CHECK(output.source_role == LORA_ROLE_MASTER);
    CHECK(output.destination_role == LORA_ROLE_CONTROL_ROOM);
    CHECK(output.flow_id == 100U);
    CHECK(output.payload_length == LORA_PROTOCOL_TEMP_PAYLOAD_SIZE);
    CHECK(memcmp(output.payload, input.payload,
                 LORA_PROTOCOL_TEMP_PAYLOAD_SIZE) == 0);

    input = MakeRead(1U, 101U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(2000U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_TEMP_36);
    CHECK(output.flow_id == 101U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);
}

static void TestAddressFlowAndTimeout(void)
{
    LoRaMessage input;
    LoRaMessage output;

    ResetRuntime(1U);
    input = MakeRead(2U, 10U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(0U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);
    CHECK(MasterRuntimeDiag.address_drop_count == 1U);

    input = MakeRead(1U, 200U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(100U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_READ_TEMP);

    input = MakeRead(1U, 200U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(101U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);

    input = MakeRead(1U, 201U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(102U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_ERROR);
    CHECK(output.flow_id == 201U);
    CHECK(output.payload[0] == 3U);

    MasterRuntime_ProcessOne(3099U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);
    MasterRuntime_ProcessOne(3100U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_ERROR);
    CHECK(output.flow_id == 200U);
    CHECK(output.payload[0] == 4U);
    MasterRuntime_ProcessOne(4000U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);

    input = MakeTemperature(1U, 200U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(4001U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);
    CHECK(MasterRuntimeDiag.temperature_reject_count == 1U);
}

static void TestFullTxQueueDoesNotStartRequest(void)
{
    LoRaMessage filler;
    LoRaMessage input;
    LoRaMessage output;
    uint32_t i;

    ResetRuntime(1U);
    memset(&filler, 0, sizeof(filler));
    for (i = 0U; i < MASTER_LORA_TX_QUEUE_DEPTH; i++)
    {
        CHECK(MasterQueues_SendLoRa(&filler, 0U) == pdPASS);
    }

    input = MakeRead(1U, 300U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(0U, 0U);
    CHECK(MasterRuntimeDiag.lora_queue_failure_count == 1U);
    for (i = 0U; i < MASTER_LORA_TX_QUEUE_DEPTH; i++)
    {
        CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    }

    PushMessage(&input);
    MasterRuntime_ProcessOne(1U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_READ_TEMP);
    CHECK(output.flow_id == 300U);
}

static void TestInvalidIdentity(void)
{
    LoRaMessage input;
    LoRaMessage output;

    ResetRuntime(0U);
    CHECK(MasterRuntimeDiag.identity_invalid == 1U);
    input = MakeRead(1U, 1U, 0U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(0U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdFAIL);
}

static void TestAutomaticControlIntegration(void)
{
    LoRaMessage input;
    LoRaMessage output;
    VfdJob job;
    MasterUiSnapshot snapshot;
    uint16_t flow_id;
    uint32_t now_ms;

    ResetRuntime(1U);

    input = MakeRead(1U, 900U, 1U);
    PushMessage(&input);
    MasterRuntime_ProcessOne(100U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_READ_TEMP);

    input = MakeTemperature(1U, 900U);
    SetAllTemperatures(&input, 0);
    input.payload[70] = 0x05U; /* 第36点=26.1℃，验证前35点为0仍会启动。 */
    input.payload[71] = 0x01U;
    PushMessage(&input);
    MasterRuntime_ProcessOne(200U, 0U);
    CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
    CHECK(output.type == LORA_MSG_TEMP_36);
    CHECK(MasterQueues_ReceiveVfdJob(&job, 0U) == pdPASS);
    CHECK(job.origin == VFD_JOB_ORIGIN_AUTOMATIC);
    CHECK(job.action == VFD_ACTION_RUN_FORWARD);

    PushVfdResult(&job, VFD_RESULT_OK);
    MasterRuntime_ProcessOne(201U, 0U);
    CHECK(MasterQueues_PeekUi(&snapshot) == pdPASS);
    CHECK(snapshot.fan_state == MASTER_FAN_STATE_RUNNING);

    /* 每4秒给一套完整低温快照，使缓存持续新鲜；满60秒后才停机。 */
    for (now_ms = 1000U, flow_id = 901U;
         now_ms <= 61000U;
         now_ms += 4000U, flow_id++)
    {
        input = MakeRead(1U, flow_id, 1U);
        PushMessage(&input);
        MasterRuntime_ProcessOne(now_ms, 0U);
        CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
        CHECK(output.type == LORA_MSG_READ_TEMP);

        input = MakeTemperature(1U, flow_id);
        SetAllTemperatures(&input, 255);
        PushMessage(&input);
        MasterRuntime_ProcessOne(now_ms + 1U, 0U);
        CHECK(MasterQueues_ReceiveLoRa(&output, 0U) == pdPASS);
        CHECK(output.type == LORA_MSG_TEMP_36);

        if (now_ms < 61000U)
        {
            CHECK(MasterQueues_ReceiveVfdJob(&job, 0U) == pdFAIL);
        }
    }

    CHECK(MasterQueues_ReceiveVfdJob(&job, 0U) == pdPASS);
    CHECK(job.origin == VFD_JOB_ORIGIN_AUTOMATIC);
    CHECK(job.action == VFD_ACTION_STOP_DECELERATE);
}

static void TestPowerOnManualStopRestore(void)
{
    MasterParameters parameters;
    MasterUiSnapshot snapshot;
    VfdJob job;

    CHECK(MasterQueues_Init() == 1U);
    FakeParameterStore_Reset();
    parameters.frequency_x100 = 4500U;
    parameters.target_temperature_x10 = 275;
    parameters.control_mode = MASTER_CONTROL_MODE_MANUAL_STOP;
    FakeParameterStore_SetLoad(PARAMETER_STORE_LOADED, &parameters);
    MasterIdentity_Init(1U);
    MasterRuntime_Init();
    MasterRuntime_ProcessOne(0U, 0U);

    CHECK(MasterQueues_ReceiveVfdJob(&job, 0U) == pdPASS);
    CHECK(job.origin == VFD_JOB_ORIGIN_SAFETY_STOP);
    CHECK(job.action == VFD_ACTION_STOP_DECELERATE);
    CHECK(job.frequency_x100 == 4500U);
    CHECK(MasterQueues_PeekUi(&snapshot) == pdPASS);
    CHECK(snapshot.control_mode == MASTER_CONTROL_MODE_MANUAL_STOP);
    CHECK(snapshot.target_temperature_x10 == 275);
}

int main(void)
{
    TestStandardFlowAndCache();
    TestAddressFlowAndTimeout();
    TestFullTxQueueDoesNotStartRequest();
    TestInvalidIdentity();
    TestAutomaticControlIntegration();
    TestPowerOnManualStopRestore();
    printf("master_runtime: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
