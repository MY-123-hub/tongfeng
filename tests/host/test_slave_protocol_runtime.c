#include "slave_protocol_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_tx_frame[109];
static uint16_t g_tx_length;
static uint32_t g_tx_count;

uint8_t LORA_SendData(const uint8_t *data, uint16_t len)
{
    assert(data != NULL);
    assert(len <= sizeof(g_tx_frame));
    memcpy(g_tx_frame, data, len);
    g_tx_length = len;
    g_tx_count++;
    return 1U;
}

static uint16_t crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++)
    {
        crc = (uint16_t)(crc ^ data[i]);
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) :
                                      (uint16_t)(crc >> 1U);
        }
    }
    return crc;
}

static void push_frame(const uint8_t *frame, uint8_t length, uint32_t now_ms)
{
    uint8_t index;

    for (index = 0U; index < length; index++)
    {
        SlaveRuntime_PushRxByteFromIsr(frame[index]);
    }
    SlaveRuntime_Process(now_ms);
}

static void send_read(uint8_t type, uint8_t source_role, uint8_t source_group,
                      uint8_t destination_group, uint16_t flow_id,
                      uint8_t read_mode, uint32_t now_ms)
{
    uint8_t frame[14] =
    {
        0xAAU, 0x55U, 0x02U, type, source_role, source_group,
        0x03U, destination_group, (uint8_t)(flow_id & 0xFFU),
        (uint8_t)(flow_id >> 8U), 0x01U, read_mode, 0U, 0U
    };
    uint16_t frame_crc = crc16(&frame[2], 10U);

    frame[12] = (uint8_t)(frame_crc & 0xFFU);
    frame[13] = (uint8_t)(frame_crc >> 8U);
    push_frame(frame, (uint8_t)sizeof(frame), now_ms);
}

static void test_temp36_encoding_and_duplicate(void)
{
    SlaveTelemetrySnapshot snapshot;
    uint16_t flow_id;
    uint8_t index;

    SlaveRuntime_Init(1U);
    memset(&snapshot, 0, sizeof(snapshot));
    for (index = 0U; index < 36U; index++)
    {
        snapshot.node_temperature_x10[index] = SLAVE_TEMPERATURE_INVALID_X10;
        snapshot.node_humidity_x10[index] = SLAVE_HUMIDITY_INVALID_X10;
    }
    snapshot.slave_bme_temperature_x10 = 201;
    snapshot.slave_bme_humidity_x10 = 553U;
    snapshot.slave_bme_pressure_pa = 100123UL;
    snapshot.rain_value = SLAVE_RAIN_VALUE_UNAVAILABLE;

    send_read(0x01U, 0x02U, 1U, 1U, 100U, 1U, 0U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 1U);
    assert(flow_id == 100U);

    snapshot.node_temperature_x10[0] = 250;
    snapshot.node_temperature_x10[1] = -55;
    SlaveRuntime_CompleteSample(flow_id, &snapshot);
    SlaveRuntime_Process(49U);
    assert(g_tx_count == 0U);
    SlaveRuntime_Process(50U);

    assert(g_tx_count == 1U);
    assert(g_tx_length == 89U);
    assert(memcmp(g_tx_frame, (const uint8_t[]){0xAAU, 0x55U, 0x02U, 0x02U,
                                                0x03U, 0x01U, 0x02U, 0x01U,
                                                0x64U, 0x00U, 0x4CU}, 11U) == 0);
    assert(g_tx_frame[11] == 0xFAU && g_tx_frame[12] == 0x00U);
    assert(g_tx_frame[13] == 0xC9U && g_tx_frame[14] == 0xFFU);
    assert(g_tx_frame[15] == 0x00U && g_tx_frame[16] == 0x80U);
    assert(g_tx_frame[83] == 0xC9U && g_tx_frame[84] == 0x00U);
    assert(g_tx_frame[85] == 0x00U && g_tx_frame[86] == 0x80U);
    assert(crc16(&g_tx_frame[2], 85U) ==
           (uint16_t)((uint16_t)g_tx_frame[87] | ((uint16_t)g_tx_frame[88] << 8U)));

    send_read(0x01U, 0x02U, 1U, 1U, 100U, 1U, 100U);
    SlaveRuntime_Process(149U);
    assert(g_tx_count == 1U);
    SlaveRuntime_Process(150U);
    assert(g_tx_count == 2U);
    assert(SlaveRuntimeDiag.duplicate_request_count == 1U);
}

static void test_wrong_group_is_silent(void)
{
    uint16_t flow_id = 0U;

    SlaveRuntime_Init(1U);
    send_read(0x01U, 0x02U, 2U, 1U, 101U, 1U, 0U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 1U);
}

static void test_invalid_frames_are_silent(void)
{
    uint8_t bad_crc[14] =
    {
        0xAAU, 0x55U, 0x02U, 0x01U, 0x02U, 0x01U,
        0x03U, 0x01U, 0x66U, 0x00U, 0x01U, 0x01U,
        0x00U, 0x00U
    };
    uint8_t bad_length[13] =
    {
        0xAAU, 0x55U, 0x02U, 0x01U, 0x02U, 0x01U,
        0x03U, 0x01U, 0x67U, 0x00U, 0x00U, 0x00U,
        0x00U
    };
    uint16_t frame_crc;
    uint16_t flow_id = 0U;

    SlaveRuntime_Init(1U);
    push_frame(bad_crc, (uint8_t)sizeof(bad_crc), 0U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.invalid_frame_count == 1U);

    frame_crc = crc16(&bad_length[2], 9U);
    bad_length[11] = (uint8_t)(frame_crc & 0xFFU);
    bad_length[12] = (uint8_t)(frame_crc >> 8U);
    push_frame(bad_length, (uint8_t)sizeof(bad_length), 1U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 1U);

    send_read(0x01U, 0x01U, 1U, 1U, 102U, 1U, 2U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 2U);
}

static void test_environment_encoding_and_placeholder(void)
{
    SlaveTelemetrySnapshot snapshot;
    uint16_t flow_id;
    uint8_t index;

    SlaveRuntime_Init(1U);
    memset(&snapshot, 0, sizeof(snapshot));
    for (index = 0U; index < 36U; index++)
    {
        snapshot.node_temperature_x10[index] = SLAVE_TEMPERATURE_INVALID_X10;
        snapshot.node_humidity_x10[index] = SLAVE_HUMIDITY_INVALID_X10;
    }
    snapshot.node_humidity_x10[0] = 456U;
    snapshot.slave_bme_temperature_x10 = SLAVE_TEMPERATURE_INVALID_X10;
    snapshot.slave_bme_humidity_x10 = 612U;
    snapshot.slave_bme_pressure_pa = 100123UL;
    snapshot.rain_value = SLAVE_RAIN_VALUE_UNAVAILABLE;

    send_read(0x03U, 0x02U, 1U, 1U, 103U, 1U, 0U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 1U);
    SlaveRuntime_CompleteSample(flow_id, &snapshot);
    SlaveRuntime_Process(50U);

    assert(g_tx_length == 99U);
    assert(g_tx_frame[2] == 0x02U && g_tx_frame[3] == 0x04U);
    assert(g_tx_frame[10] == 86U);
    assert(g_tx_frame[11] == 0xC8U && g_tx_frame[12] == 0x01U);
    assert(g_tx_frame[13] == 0xFFU && g_tx_frame[14] == 0xFFU);
    assert(g_tx_frame[83] == 0x64U && g_tx_frame[84] == 0x02U);
    assert(g_tx_frame[85] == 0xFFU && g_tx_frame[86] == 0xFFU);
    assert(memcmp(&g_tx_frame[87], (const uint8_t[]){0x1BU, 0x87U, 0x01U, 0x00U}, 4U) == 0);
    assert(memcmp(&g_tx_frame[91], (const uint8_t[]){0xFFU, 0xFFU, 0xFFU, 0xFFU}, 4U) == 0);
    assert(g_tx_frame[95] == 0xFFU && g_tx_frame[96] == 0xFFU);
    assert(crc16(&g_tx_frame[2], 95U) ==
           (uint16_t)((uint16_t)g_tx_frame[97] | ((uint16_t)g_tx_frame[98] << 8U)));
}

int main(void)
{
    test_temp36_encoding_and_duplicate();
    test_wrong_group_is_silent();
    test_invalid_frames_are_silent();
    test_environment_encoding_and_placeholder();
    puts("slave_protocol_runtime: PASS");
    return 0;
}
