#include "slave_protocol_runtime.h"

#include "lora.h"

#include <string.h>

#define FRAME_HEAD_1                 (0xAAU)
#define FRAME_HEAD_2                 (0x55U)
#define FRAME_VERSION                (0x01U)
#define FRAME_MAX_PAYLOAD            (96U)
#define FRAME_MIN_SIZE               (13U)
#define FRAME_MAX_SIZE               (109U)
#define FRAME_TEMP_PAYLOAD_SIZE      (72U)
#define FRAME_ROLE_MASTER            (0x02U)
#define FRAME_ROLE_SLAVE             (0x03U)
#define FRAME_TYPE_READ_TEMP         (0x01U)
#define FRAME_TYPE_TEMP_36           (0x02U)
#define RX_RING_SIZE                 (256U)
#define RX_RING_MASK                 (RX_RING_SIZE - 1U)

typedef struct
{
    volatile uint8_t data[RX_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count;
} SlaveRxRing;

typedef struct
{
    uint8_t type;
    uint8_t source_role;
    uint8_t source_group;
    uint8_t destination_role;
    uint8_t destination_group;
    uint16_t flow_id;
    uint8_t payload_length;
    uint8_t payload[FRAME_MAX_PAYLOAD];
} SlaveMessage;

static SlaveRxRing g_rx_ring;
static uint8_t g_frame[FRAME_MAX_SIZE];
static uint16_t g_frame_length;
static uint16_t g_expected_length;
static uint8_t g_local_group;
static uint8_t g_application_mode;
static uint8_t g_sample_requested;
static uint8_t g_sample_in_progress;
static uint16_t g_sample_flow_id;
static uint8_t g_tx_pending;
static uint8_t g_tx_frame[FRAME_MAX_SIZE];
static uint16_t g_tx_frame_length;

static uint16_t SlaveRuntime_Crc16(const uint8_t *data, uint16_t length)
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

static uint8_t SlaveRuntime_PopRx(uint8_t *byte)
{
    uint16_t tail;

    if (byte == NULL)
    {
        return 0U;
    }
    tail = g_rx_ring.tail;
    if (tail == g_rx_ring.head)
    {
        return 0U;
    }
    *byte = g_rx_ring.data[tail];
    g_rx_ring.tail = (uint16_t)((tail + 1U) & RX_RING_MASK);
    return 1U;
}

static void SlaveRuntime_ResetParser(void)
{
    g_frame_length = 0U;
    g_expected_length = 0U;
}

static uint8_t SlaveRuntime_Decode(const uint8_t *frame, uint16_t length,
                                   SlaveMessage *message)
{
    uint8_t payload_length;
    uint16_t crc;

    if ((frame == NULL) || (message == NULL) || (length < FRAME_MIN_SIZE) ||
        (frame[0] != FRAME_HEAD_1) || (frame[1] != FRAME_HEAD_2) ||
        (frame[2] != FRAME_VERSION))
    {
        return 0U;
    }
    payload_length = frame[10];
    if ((payload_length > FRAME_MAX_PAYLOAD) ||
        (length != (uint16_t)(FRAME_MIN_SIZE + payload_length)))
    {
        return 0U;
    }
    crc = SlaveRuntime_Crc16(&frame[2], (uint16_t)(9U + payload_length));
    if ((frame[length - 2U] != (uint8_t)(crc & 0xFFU)) ||
        (frame[length - 1U] != (uint8_t)(crc >> 8U)))
    {
        return 0U;
    }

    message->type = frame[3];
    message->source_role = frame[4];
    message->source_group = frame[5];
    message->destination_role = frame[6];
    message->destination_group = frame[7];
    message->flow_id = (uint16_t)((uint16_t)frame[8] | ((uint16_t)frame[9] << 8U));
    message->payload_length = payload_length;
    if (payload_length != 0U)
    {
        memcpy(message->payload, &frame[11], payload_length);
    }
    return 1U;
}

static void SlaveRuntime_HandleMessage(const SlaveMessage *message)
{
    if ((message->type != FRAME_TYPE_READ_TEMP) ||
        (message->source_role != FRAME_ROLE_MASTER) ||
        (message->destination_role != FRAME_ROLE_SLAVE) ||
        (message->source_group != g_local_group) ||
        (message->destination_group != g_local_group) ||
        (message->payload_length != 1U) || (message->payload[0] > 1U))
    {
        return;
    }

    /* 同一个流水号的重发不会再次启动转换；主机等待当前结果。 */
    if (g_sample_in_progress != 0U)
    {
        return;
    }
    g_sample_flow_id = message->flow_id;
    g_sample_requested = 1U;
    g_sample_in_progress = 1U;
}

static void SlaveRuntime_PushByte(uint8_t byte)
{
    SlaveMessage message;

    if (g_frame_length == 0U)
    {
        if (byte == FRAME_HEAD_1)
        {
            g_frame[0] = byte;
            g_frame_length = 1U;
        }
        return;
    }
    if (g_frame_length == 1U)
    {
        if (byte == FRAME_HEAD_2)
        {
            g_frame[1] = byte;
            g_frame_length = 2U;
        }
        else if (byte == FRAME_HEAD_1)
        {
            g_frame[0] = byte;
        }
        else
        {
            SlaveRuntime_ResetParser();
        }
        return;
    }
    if (g_frame_length >= FRAME_MAX_SIZE)
    {
        SlaveRuntime_ResetParser();
        return;
    }
    g_frame[g_frame_length++] = byte;
    if (g_frame_length == 11U)
    {
        if (g_frame[10] > FRAME_MAX_PAYLOAD)
        {
            SlaveRuntime_ResetParser();
            return;
        }
        g_expected_length = (uint16_t)(FRAME_MIN_SIZE + g_frame[10]);
    }
    if ((g_expected_length != 0U) && (g_frame_length == g_expected_length))
    {
        if (SlaveRuntime_Decode(g_frame, g_frame_length, &message) != 0U)
        {
            SlaveRuntime_HandleMessage(&message);
        }
        SlaveRuntime_ResetParser();
    }
}

void SlaveRuntime_Init(uint8_t local_group)
{
    (void)memset(&g_rx_ring, 0, sizeof(g_rx_ring));
    SlaveRuntime_ResetParser();
    g_local_group = local_group;
    g_application_mode = ((local_group >= 1U) && (local_group <= 4U)) ? 1U : 0U;
    g_sample_requested = 0U;
    g_sample_in_progress = 0U;
    g_tx_pending = 0U;
    g_tx_frame_length = 0U;
}

uint8_t SlaveRuntime_IsApplicationMode(void)
{
    return g_application_mode;
}

void SlaveRuntime_PushRxByteFromIsr(uint8_t byte)
{
    uint16_t head;
    uint16_t next_head;

    if (g_application_mode == 0U)
    {
        return;
    }
    head = g_rx_ring.head;
    next_head = (uint16_t)((head + 1U) & RX_RING_MASK);
    if (next_head == g_rx_ring.tail)
    {
        g_rx_ring.overflow_count++;
        return;
    }
    g_rx_ring.data[head] = byte;
    g_rx_ring.head = next_head;
}

void SlaveRuntime_Process(uint32_t now_ms)
{
    uint8_t byte;
    uint16_t count = 0U;

    (void)now_ms;
    while ((count < 128U) && (SlaveRuntime_PopRx(&byte) != 0U))
    {
        SlaveRuntime_PushByte(byte);
        count++;
    }
    if (g_tx_pending != 0U)
    {
        LORA_SendData(g_tx_frame, g_tx_frame_length);
        g_tx_pending = 0U;
    }
}

uint8_t SlaveRuntime_TakeSampleRequest(uint16_t *flow_id)
{
    if ((flow_id == NULL) || (g_sample_requested == 0U))
    {
        return 0U;
    }
    *flow_id = g_sample_flow_id;
    g_sample_requested = 0U;
    return 1U;
}

void SlaveRuntime_CompleteSample(uint16_t flow_id, const int16_t temperatures[36])
{
    uint16_t i;
    uint16_t crc;

    if ((temperatures == NULL) || (g_sample_in_progress == 0U) ||
        (flow_id != g_sample_flow_id) || (g_tx_pending != 0U))
    {
        return;
    }
    g_tx_frame[0] = FRAME_HEAD_1;
    g_tx_frame[1] = FRAME_HEAD_2;
    g_tx_frame[2] = FRAME_VERSION;
    g_tx_frame[3] = FRAME_TYPE_TEMP_36;
    g_tx_frame[4] = FRAME_ROLE_SLAVE;
    g_tx_frame[5] = g_local_group;
    g_tx_frame[6] = FRAME_ROLE_MASTER;
    g_tx_frame[7] = g_local_group;
    g_tx_frame[8] = (uint8_t)(flow_id & 0xFFU);
    g_tx_frame[9] = (uint8_t)(flow_id >> 8U);
    g_tx_frame[10] = FRAME_TEMP_PAYLOAD_SIZE;
    for (i = 0U; i < 36U; i++)
    {
        uint16_t raw = (uint16_t)temperatures[i];
        g_tx_frame[11U + 2U * i] = (uint8_t)(raw & 0xFFU);
        g_tx_frame[12U + 2U * i] = (uint8_t)(raw >> 8U);
    }
    g_tx_frame_length = (uint16_t)(FRAME_MIN_SIZE + FRAME_TEMP_PAYLOAD_SIZE);
    crc = SlaveRuntime_Crc16(&g_tx_frame[2], 9U + FRAME_TEMP_PAYLOAD_SIZE);
    g_tx_frame[g_tx_frame_length - 2U] = (uint8_t)(crc & 0xFFU);
    g_tx_frame[g_tx_frame_length - 1U] = (uint8_t)(crc >> 8U);
    g_tx_pending = 1U;
    g_sample_in_progress = 0U;
}
