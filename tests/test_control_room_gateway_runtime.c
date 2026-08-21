#include "gateway_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t g_ports[8];
static uint8_t g_frames[8][109];
static uint16_t g_lengths[8];
static uint8_t g_count;

static uint16_t Test_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc = (uint16_t)(crc ^ data[index]);
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) :
                                      (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static uint16_t Test_BuildFrame(uint8_t *output, uint8_t type,
                                uint8_t source_role, uint8_t source_group,
                                uint8_t destination_role, uint8_t destination_group,
                                uint16_t flow_id, const uint8_t *payload,
                                uint8_t payload_length)
{
    uint16_t crc;

    output[0] = 0xAAU;
    output[1] = 0x55U;
    output[2] = 0x01U;
    output[3] = type;
    output[4] = source_role;
    output[5] = source_group;
    output[6] = destination_role;
    output[7] = destination_group;
    output[8] = (uint8_t)(flow_id & 0x00FFU);
    output[9] = (uint8_t)(flow_id >> 8U);
    output[10] = payload_length;
    if (payload_length != 0U)
    {
        memcpy(&output[11], payload, payload_length);
    }
    crc = Test_Crc16(&output[2], (uint16_t)(9U + payload_length));
    output[11U + payload_length] = (uint8_t)(crc & 0x00FFU);
    output[12U + payload_length] = (uint8_t)(crc >> 8U);
    return (uint16_t)(13U + payload_length);
}

static uint8_t Test_Send(GatewayOutputPort port, const uint8_t *frame,
                         uint16_t frame_length, void *context)
{
    (void)context;
    assert(g_count < 8U);
    g_ports[g_count] = (uint8_t)port;
    g_lengths[g_count] = frame_length;
    memcpy(g_frames[g_count], frame, frame_length);
    g_count++;
    return 1U;
}

static void Test_FeedPc(const uint8_t *frame, uint16_t frame_length)
{
    uint16_t index;
    for (index = 0U; index < frame_length; index++)
    {
        GatewayRuntime_PushPcByteFromIsr(frame[index]);
    }
}

static void Test_FeedLoRa(const uint8_t *frame, uint16_t frame_length)
{
    uint16_t index;
    for (index = 0U; index < frame_length; index++)
    {
        GatewayRuntime_PushLoRaByteFromIsr(frame[index]);
    }
}

int main(void)
{
    uint8_t frame[109];
    uint8_t temperatures[72] = {0};
    uint8_t frequency[2] = {0x88U, 0x13U};
    uint8_t ack[2] = {0x00U, 0x00U};
    uint8_t result[7] = {0};
    uint16_t length;

    GatewayRuntime_Init(Test_Send, NULL);
    GatewayRuntime_Process(0U);
    assert((g_count == 1U) && (g_ports[0] == GATEWAY_OUTPUT_LORA));
    assert((g_frames[0][3] == 0x01U) && (g_frames[0][4] == 0x01U));
    assert((g_frames[0][6] == 0x02U) && (g_frames[0][7] == 0x01U));

    length = Test_BuildFrame(frame, 0x02U, 0x02U, 0x01U, 0x01U, 0x00U,
                             0x8000U, temperatures, sizeof(temperatures));
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(1U);
    assert((g_count == 2U) && (g_ports[1] == GATEWAY_OUTPUT_PC));
    assert((g_frames[1][3] == 0x02U) && (g_lengths[1] == 85U));

    length = Test_BuildFrame(frame, 0x10U, 0x01U, 0x00U, 0x02U, 0x01U,
                             100U, frequency, sizeof(frequency));
    Test_FeedPc(frame, length);
    GatewayRuntime_Process(2U);
    assert((g_count == 3U) && (g_ports[2] == GATEWAY_OUTPUT_LORA));
    assert(g_lengths[2] == 15U);
    assert(memcmp(g_frames[2], frame, length) == 0);

    length = Test_BuildFrame(frame, 0x20U, 0x02U, 0x01U, 0x01U, 0x00U,
                             100U, ack, sizeof(ack));
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(3U);
    assert((g_count == 4U) && (g_ports[3] == GATEWAY_OUTPUT_PC));
    assert(g_frames[3][3] == 0x20U);

    length = Test_BuildFrame(frame, 0x21U, 0x02U, 0x01U, 0x01U, 0x00U,
                             100U, result, sizeof(result));
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(4U);
    assert((g_count == 5U) && (g_ports[4] == GATEWAY_OUTPUT_PC));
    assert(g_frames[4][3] == 0x21U);

    frame[length - 1U] ^= 0x01U;
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(5U);
    assert(g_count == 5U);
    return 0;
}
