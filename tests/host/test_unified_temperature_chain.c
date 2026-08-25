#include "gateway_runtime.h"
#include "lora_protocol.h"
#include "master_identity.h"
#include "master_messages.h"
#include "master_queues.h"
#include "master_runtime.h"
#include "slave_protocol_runtime.h"

#include "fake_parameter_store.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_gateway_lora_frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
static uint16_t g_gateway_lora_length;
static uint32_t g_gateway_lora_count;
static uint8_t g_gateway_pc_frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
static uint16_t g_gateway_pc_length;
static uint32_t g_gateway_pc_count;
static uint8_t g_slave_frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
static uint16_t g_slave_frame_length;
static uint32_t g_slave_frame_count;

uint8_t LORA_SendData(const uint8_t *data, uint16_t len)
{
    assert(data != NULL);
    assert(len <= sizeof(g_slave_frame));
    memcpy(g_slave_frame, data, len);
    g_slave_frame_length = len;
    g_slave_frame_count++;
    return 1U;
}

static uint8_t Test_GatewaySend(GatewayOutputPort port,
                                const uint8_t *frame,
                                uint16_t frame_length,
                                void *context)
{
    (void)context;
    assert(frame != NULL);
    assert(frame_length <= LORA_PROTOCOL_MAX_FRAME_SIZE);

    if (port == GATEWAY_OUTPUT_LORA)
    {
        memcpy(g_gateway_lora_frame, frame, frame_length);
        g_gateway_lora_length = frame_length;
        g_gateway_lora_count++;
    }
    else
    {
        memcpy(g_gateway_pc_frame, frame, frame_length);
        g_gateway_pc_length = frame_length;
        g_gateway_pc_count++;
    }
    return 1U;
}

static void Test_PushMasterMessage(const LoRaMessage *message)
{
    MasterEvent event;

    memset(&event, 0, sizeof(event));
    event.type = MASTER_EVENT_LORA_MESSAGE;
    event.data.lora_message = *message;
    assert(MasterQueues_SendEvent(&event, 0U) == pdPASS);
}

static void Test_PushSlaveFrame(const uint8_t *frame, uint16_t frame_length)
{
    uint16_t index;

    for (index = 0U; index < frame_length; index++)
    {
        SlaveRuntime_PushRxByteFromIsr(frame[index]);
    }
}

static void Test_PushGatewayLoRaFrame(const uint8_t *frame,
                                     uint16_t frame_length)
{
    uint16_t index;

    for (index = 0U; index < frame_length; index++)
    {
        GatewayRuntime_PushLoRaByteFromIsr(frame[index]);
    }
}

int main(void)
{
    LoRaMessage message;
    LoRaMessage master_output;
    LoRaMessage pc_output;
    uint8_t master_frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t master_frame_length;
    uint16_t sample_flow_id;
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT] = {0};

    assert(MasterQueues_Init() == 1U);
    FakeParameterStore_Reset();
    MasterIdentity_Init(1U);
    MasterRuntime_Init();
    SlaveRuntime_Init(1U);
    GatewayRuntime_Init(Test_GatewaySend, NULL);

    /* 控制室发起M1温度轮询，flow由控制室创建。 */
    GatewayRuntime_Process(0U);
    assert(g_gateway_lora_count == 1U);
    assert(LoRaProtocol_Decode(g_gateway_lora_frame,
                               g_gateway_lora_length,
                               &message) == LORA_PROTOCOL_OK);
    assert(message.type == LORA_MSG_READ_TEMP);
    assert(message.source_role == LORA_ROLE_CONTROL_ROOM);
    assert(message.destination_role == LORA_ROLE_MASTER);
    assert(message.destination_group == 1U);
    assert(message.flow_id == 0x8000U);

    /* 主机保留同一flow，把请求改址后转给同组从机。 */
    Test_PushMasterMessage(&message);
    MasterRuntime_ProcessOne(10U, 0U);
    assert(MasterQueues_ReceiveLoRa(&master_output, 0U) == pdPASS);
    assert(master_output.type == LORA_MSG_READ_TEMP);
    assert(master_output.source_role == LORA_ROLE_MASTER);
    assert(master_output.destination_role == LORA_ROLE_SLAVE);
    assert(master_output.destination_group == 1U);
    assert(master_output.flow_id == message.flow_id);
    assert(master_output.payload[0] == 1U);
    assert(LoRaProtocol_Encode(&master_output,
                               master_frame,
                               sizeof(master_frame),
                               &master_frame_length) == LORA_PROTOCOL_OK);

    /* 从机完整解帧、采样，并且不会早于50ms收发保护间隔回包。 */
    Test_PushSlaveFrame(master_frame, master_frame_length);
    SlaveRuntime_Process(100U);
    assert(SlaveRuntime_TakeSampleRequest(&sample_flow_id) == 1U);
    assert(sample_flow_id == message.flow_id);
    temperatures[0] = 250;
    temperatures[35] = -55;
    SlaveRuntime_CompleteSample(sample_flow_id, temperatures);
    SlaveRuntime_Process(149U);
    assert(g_slave_frame_count == 0U);
    SlaveRuntime_Process(150U);
    assert(g_slave_frame_count == 1U);
    assert(LoRaProtocol_Decode(g_slave_frame,
                               g_slave_frame_length,
                               &message) == LORA_PROTOCOL_OK);
    assert(message.type == LORA_MSG_TEMP_36);
    assert(message.source_role == LORA_ROLE_SLAVE);
    assert(message.destination_role == LORA_ROLE_MASTER);
    assert(message.flow_id == 0x8000U);

    /* 主机接收从机36点数据，再以原flow转发给控制室。 */
    Test_PushMasterMessage(&message);
    MasterRuntime_ProcessOne(200U, 0U);
    assert(MasterQueues_ReceiveLoRa(&master_output, 0U) == pdPASS);
    assert(master_output.type == LORA_MSG_TEMP_36);
    assert(master_output.source_role == LORA_ROLE_MASTER);
    assert(master_output.destination_role == LORA_ROLE_CONTROL_ROOM);
    assert(master_output.flow_id == 0x8000U);
    assert(master_output.payload[0] == 0xFAU);
    assert(master_output.payload[1] == 0x00U);
    assert(master_output.payload[70] == 0xC9U);
    assert(master_output.payload[71] == 0xFFU);
    assert(LoRaProtocol_Encode(&master_output,
                               master_frame,
                               sizeof(master_frame),
                               &master_frame_length) == LORA_PROTOCOL_OK);

    /* 控制室只做协议透明转发，上位机收到的仍是Git二进制TEMP_36。 */
    Test_PushGatewayLoRaFrame(master_frame, master_frame_length);
    GatewayRuntime_Process(250U);
    assert(g_gateway_pc_count == 1U);
    assert(LoRaProtocol_Decode(g_gateway_pc_frame,
                               g_gateway_pc_length,
                               &pc_output) == LORA_PROTOCOL_OK);
    assert(pc_output.type == LORA_MSG_TEMP_36);
    assert(pc_output.source_group == 1U);
    assert(pc_output.destination_group == 0U);
    assert(pc_output.flow_id == 0x8000U);
    assert(memcmp(pc_output.payload, master_output.payload,
                  LORA_PROTOCOL_TEMP_PAYLOAD_SIZE) == 0);

    puts("unified_temperature_chain: PASS");
    return 0;
}
