#include "gateway_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t g_ports[32];
static uint8_t g_frames[32][109];
static uint16_t g_lengths[32];
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
    output[2] = 0x02U;
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
    assert(g_count < 32U);
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
    uint8_t temperatures[76] = {0};
    uint8_t environment[86];
    uint8_t frequency[2] = {0x88U, 0x13U};
    uint8_t host_list[2] = {2U, 4U};
    uint16_t length;

    memset(environment, 0xFF, sizeof(environment));
    GatewayRuntime_Init(Test_Send, NULL);
    GatewayRuntime_Process(0U);
    assert((g_count == 1U) && (g_ports[0] == GATEWAY_OUTPUT_LORA));
    assert((g_frames[0][3] == 0x01U) && (g_frames[0][4] == 0x01U));
    assert((g_frames[0][6] == 0x02U) && (g_frames[0][7] == 0x01U));
    assert(g_frames[0][2] == 0x02U);

    length = Test_BuildFrame(frame, 0x02U, 0x02U, 0x01U, 0x01U, 0x00U,
                             0x8000U, temperatures, sizeof(temperatures));
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(1U);
    assert((g_count == 2U) && (g_ports[1] == GATEWAY_OUTPUT_PC));
    assert((g_frames[1][3] == 0x02U) && (g_lengths[1] == 89U));

    /* 同组下一事务为独立环境帧。 */
    GatewayRuntime_Process(1000U);
    assert((g_count == 3U) && (g_ports[2] == GATEWAY_OUTPUT_LORA));
    assert((g_frames[2][3] == 0x03U) && (g_frames[2][7] == 0x01U));

    environment[84] = 0xFFU;
    environment[85] = 0xFFU;
    length = Test_BuildFrame(frame, 0x04U, 0x02U, 0x01U, 0x01U, 0x00U,
                             0x8001U, environment, sizeof(environment));
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(1001U);
    assert((g_count == 4U) && (g_ports[3] == GATEWAY_OUTPUT_PC));
    assert((g_frames[3][3] == 0x04U) && (g_lengths[3] == 99U));
    assert(g_frames[3][95] == 0xFFU && g_frames[3][96] == 0xFFU);

    /* 默认联调列表只有 M1；环境完成后重新进入 M1 温度事务。 */
    GatewayRuntime_Process(2000U);
    assert((g_count == 5U) && (g_ports[4] == GATEWAY_OUTPUT_LORA));
    assert((g_frames[4][3] == 0x01U) && (g_frames[4][7] == 0x01U));

    /* CRC错误不得转发，也不得完成当前事务。 */
    length = Test_BuildFrame(frame, 0x02U, 0x02U, 0x01U, 0x01U, 0x00U,
                             0x8002U, temperatures, sizeof(temperatures));
    frame[length - 1U] ^= 0x01U;
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(2001U);
    assert(g_count == 5U);

    /* M2命令仍属于合法通用组；待当前 M1 自动事务完成后进入空口。 */
    length = Test_BuildFrame(frame, 0x10U, 0x01U, 0x00U, 0x02U, 0x02U,
                             101U, frequency, sizeof(frequency));
    Test_FeedPc(frame, length);
    GatewayRuntime_Process(2002U);
    assert(g_count == 5U);
    length = Test_BuildFrame(frame, 0x02U, 0x02U, 0x01U, 0x01U, 0x00U,
                             0x8002U, temperatures, sizeof(temperatures));
    Test_FeedLoRa(frame, length);
    GatewayRuntime_Process(2003U);
    assert((g_count == 7U) && (g_ports[5] == GATEWAY_OUTPUT_PC));
    assert((g_ports[6] == GATEWAY_OUTPUT_LORA) && (g_frames[6][3] == 0x10U));
    assert(g_frames[6][7] == 0x02U);

    /* 上位机可把活动组改为[2,4]，下一轮从M2温度开始。 */
    g_count = 0U;
    GatewayRuntime_Init(Test_Send, NULL);
    length = Test_BuildFrame(frame, 0x30U, 0x04U, 0x00U, 0x01U, 0x00U,
                             10U, host_list, sizeof(host_list));
    Test_FeedPc(frame, length);
    GatewayRuntime_Process(0U);
    assert((g_count == 1U) && (g_ports[0] == GATEWAY_OUTPUT_LORA));
    assert((g_frames[0][3] == 0x01U) && (g_frames[0][7] == 0x02U));
    return 0;
}
