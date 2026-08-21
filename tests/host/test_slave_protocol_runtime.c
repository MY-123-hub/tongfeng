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

static void push_frame(const uint8_t *frame, uint8_t length)
{
    uint8_t index;

    for (index = 0U; index < length; index++)
    {
        SlaveRuntime_PushRxByteFromIsr(frame[index]);
    }
    SlaveRuntime_Process(0U);
}

static void send_read_temp(uint8_t source_role, uint8_t source_group,
                           uint8_t destination_group, uint16_t flow_id)
{
    uint8_t frame[14] =
    {
        0xAAU, 0x55U, 0x01U, 0x01U, source_role, source_group,
        0x03U, destination_group, (uint8_t)(flow_id & 0xFFU),
        (uint8_t)(flow_id >> 8U), 0x01U, 0x01U, 0U, 0U
    };
    uint16_t frame_crc = crc16(&frame[2], 10U);

    frame[12] = (uint8_t)(frame_crc & 0xFFU);
    frame[13] = (uint8_t)(frame_crc >> 8U);
    push_frame(frame, (uint8_t)sizeof(frame));
}

static void test_temp36_encoding_and_duplicate(void)
{
    int16_t temperatures[36] = {0};
    uint16_t flow_id;

    SlaveRuntime_Init(1U);
    send_read_temp(0x02U, 1U, 1U, 100U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 1U);
    assert(flow_id == 100U);

    temperatures[0] = 250;
    temperatures[1] = -55;
    SlaveRuntime_CompleteSample(flow_id, temperatures);
    SlaveRuntime_Process(1U);

    assert(g_tx_count == 1U);
    assert(g_tx_length == 85U);
    assert(memcmp(g_tx_frame, (const uint8_t[]){0xAAU, 0x55U, 0x01U, 0x02U,
                                                0x03U, 0x01U, 0x02U, 0x01U,
                                                0x64U, 0x00U, 0x48U}, 11U) == 0);
    assert(g_tx_frame[11] == 0xFAU && g_tx_frame[12] == 0x00U);
    assert(g_tx_frame[13] == 0xC9U && g_tx_frame[14] == 0xFFU);
    assert(g_tx_frame[15] == 0x00U && g_tx_frame[82] == 0x00U);
    assert(crc16(&g_tx_frame[2], 81U) ==
           (uint16_t)((uint16_t)g_tx_frame[83] | ((uint16_t)g_tx_frame[84] << 8U)));

    send_read_temp(0x02U, 1U, 1U, 100U);
    SlaveRuntime_Process(2U);
    assert(g_tx_count == 2U);
    assert(SlaveRuntimeDiag.duplicate_request_count == 1U);
}

static void test_wrong_group_is_silent(void)
{
    uint16_t flow_id = 0U;

    SlaveRuntime_Init(1U);
    send_read_temp(0x02U, 2U, 1U, 101U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 1U);
}

static void test_invalid_frames_are_silent(void)
{
    uint8_t bad_crc[14] =
    {
        0xAAU, 0x55U, 0x01U, 0x01U, 0x02U, 0x01U,
        0x03U, 0x01U, 0x66U, 0x00U, 0x01U, 0x01U,
        0x00U, 0x00U
    };
    uint8_t bad_length[13] =
    {
        0xAAU, 0x55U, 0x01U, 0x01U, 0x02U, 0x01U,
        0x03U, 0x01U, 0x67U, 0x00U, 0x00U, 0x00U,
        0x00U
    };
    uint16_t frame_crc;
    uint16_t flow_id = 0U;

    SlaveRuntime_Init(1U);
    push_frame(bad_crc, (uint8_t)sizeof(bad_crc));
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.invalid_frame_count == 1U);

    frame_crc = crc16(&bad_length[2], 9U);
    bad_length[11] = (uint8_t)(frame_crc & 0xFFU);
    bad_length[12] = (uint8_t)(frame_crc >> 8U);
    push_frame(bad_length, (uint8_t)sizeof(bad_length));
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 1U);

    send_read_temp(0x01U, 1U, 1U, 102U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 0U);
    assert(SlaveRuntimeDiag.ignored_message_count == 2U);
}

static void test_all_invalid_snapshot(void)
{
    int16_t temperatures[36] = {0};
    uint16_t flow_id;
    uint8_t index;

    SlaveRuntime_Init(1U);
    send_read_temp(0x02U, 1U, 1U, 103U);
    assert(SlaveRuntime_TakeSampleRequest(&flow_id) == 1U);
    SlaveRuntime_CompleteSample(flow_id, temperatures);
    SlaveRuntime_Process(0U);

    assert(g_tx_length == 85U);
    for (index = 11U; index < 83U; index++)
    {
        assert(g_tx_frame[index] == 0U);
    }
}

int main(void)
{
    test_temp36_encoding_and_duplicate();
    test_wrong_group_is_silent();
    test_invalid_frames_are_silent();
    test_all_invalid_snapshot();
    puts("slave_protocol_runtime: PASS");
    return 0;
}
