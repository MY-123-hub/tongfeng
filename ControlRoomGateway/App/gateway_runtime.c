#include "gateway_runtime.h"

#include <string.h>

#define GW_HEAD_1                  (0xAAU)
#define GW_HEAD_2                  (0x55U)
#define GW_VERSION                 (0x01U)
#define GW_MIN_FRAME_SIZE          (13U)
#define GW_MAX_FRAME_SIZE          (109U)
#define GW_MAX_PAYLOAD_SIZE        (96U)
#define GW_RX_RING_SIZE            (256U)
#define GW_RX_RING_MASK            (GW_RX_RING_SIZE - 1U)
#define GW_PC_QUEUE_DEPTH          (4U)
#define GW_ROLE_CONTROL_ROOM       (0x01U)
#define GW_ROLE_MASTER             (0x02U)
#define GW_TYPE_READ_TEMP          (0x01U)
#define GW_TYPE_TEMP_36            (0x02U)
#define GW_TYPE_SET_FREQ           (0x10U)
#define GW_TYPE_SET_TARGET_TEMP    (0x11U)
#define GW_TYPE_MANUAL_RUN         (0x12U)
#define GW_TYPE_MANUAL_STOP        (0x13U)
#define GW_TYPE_SET_AUTO           (0x14U)
#define GW_TYPE_QUERY_STATUS       (0x15U)
#define GW_TYPE_ACK                (0x20U)
#define GW_TYPE_RESULT             (0x21U)
#define GW_TYPE_ERROR              (0x7EU)
#define GW_POLL_INTERVAL_MS        (1000U)
#define GW_TEMP_TIMEOUT_MS         (4000U)
#define GW_COMMAND_TIMEOUT_MS      (6000U)

typedef struct
{
    volatile uint8_t data[GW_RX_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} GatewayRxRing;

typedef struct
{
    uint8_t frame[GW_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t expected_length;
} GatewayParser;

typedef struct
{
    uint8_t type;
    uint8_t source_role;
    uint8_t source_group;
    uint8_t destination_role;
    uint8_t destination_group;
    uint16_t flow_id;
    uint8_t payload_length;
    uint8_t payload[GW_MAX_PAYLOAD_SIZE];
} GatewayMessage;

typedef struct
{
    GatewayMessage message;
    uint8_t automatic_poll;
} GatewayQueuedMessage;

typedef struct
{
    uint8_t active;
    uint8_t automatic_poll;
    uint8_t group;
    uint8_t request_type;
    uint16_t flow_id;
    uint32_t deadline;
} GatewayPending;

static GatewayRxRing g_pc_rx;
static GatewayRxRing g_lora_rx;
static GatewayParser g_pc_parser;
static GatewayParser g_lora_parser;
static GatewayQueuedMessage g_pc_queue[GW_PC_QUEUE_DEPTH];
static uint8_t g_pc_queue_head;
static uint8_t g_pc_queue_tail;
static uint8_t g_pc_queue_count;
static GatewayPending g_pending;
static uint8_t g_next_poll_group;
static uint16_t g_next_auto_flow;
static uint32_t g_next_poll_tick;
static GatewaySendCallback g_send_callback;
static void *g_send_context;

static uint16_t Gateway_Crc16(const uint8_t *data, uint16_t length)
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

static void Gateway_ResetParser(GatewayParser *parser)
{
    parser->length = 0U;
    parser->expected_length = 0U;
}

static uint8_t Gateway_RingPop(GatewayRxRing *ring, uint8_t *byte)
{
    uint16_t tail;
    if (byte == NULL)
    {
        return 0U;
    }
    tail = ring->tail;
    if (tail == ring->head)
    {
        return 0U;
    }
    *byte = ring->data[tail];
    ring->tail = (uint16_t)((tail + 1U) & GW_RX_RING_MASK);
    return 1U;
}

static void Gateway_RingPushFromIsr(GatewayRxRing *ring, uint8_t byte)
{
    uint16_t head = ring->head;
    uint16_t next = (uint16_t)((head + 1U) & GW_RX_RING_MASK);
    if (next != ring->tail)
    {
        ring->data[head] = byte;
        ring->head = next;
    }
}

static uint8_t Gateway_Decode(const uint8_t *frame, uint16_t length,
                              GatewayMessage *message)
{
    uint8_t payload_length;
    uint16_t crc;
    if ((frame == NULL) || (message == NULL) || (length < GW_MIN_FRAME_SIZE) ||
        (frame[0] != GW_HEAD_1) || (frame[1] != GW_HEAD_2) ||
        (frame[2] != GW_VERSION))
    {
        return 0U;
    }
    payload_length = frame[10];
    if ((payload_length > GW_MAX_PAYLOAD_SIZE) ||
        (length != (uint16_t)(GW_MIN_FRAME_SIZE + payload_length)))
    {
        return 0U;
    }
    crc = Gateway_Crc16(&frame[2], (uint16_t)(9U + payload_length));
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

static uint8_t Gateway_ParseByte(GatewayParser *parser, uint8_t byte,
                                 GatewayMessage *message)
{
    if (parser->length == 0U)
    {
        if (byte == GW_HEAD_1)
        {
            parser->frame[0] = byte;
            parser->length = 1U;
        }
        return 0U;
    }
    if (parser->length == 1U)
    {
        if (byte == GW_HEAD_2)
        {
            parser->frame[1] = byte;
            parser->length = 2U;
        }
        else if (byte == GW_HEAD_1)
        {
            parser->frame[0] = byte;
        }
        else
        {
            Gateway_ResetParser(parser);
        }
        return 0U;
    }
    if (parser->length >= GW_MAX_FRAME_SIZE)
    {
        Gateway_ResetParser(parser);
        return 0U;
    }
    parser->frame[parser->length++] = byte;
    if (parser->length == 11U)
    {
        if (parser->frame[10] > GW_MAX_PAYLOAD_SIZE)
        {
            Gateway_ResetParser(parser);
            return 0U;
        }
        parser->expected_length = (uint16_t)(GW_MIN_FRAME_SIZE + parser->frame[10]);
    }
    if ((parser->expected_length != 0U) && (parser->length == parser->expected_length))
    {
        uint8_t ready = Gateway_Decode(parser->frame, parser->length, message);
        Gateway_ResetParser(parser);
        return ready;
    }
    return 0U;
}

static uint8_t Gateway_IsPcCommand(const GatewayMessage *message)
{
    if ((message->source_role != GW_ROLE_CONTROL_ROOM) ||
        (message->source_group != 0U) ||
        (message->destination_role != GW_ROLE_MASTER) ||
        (message->destination_group < 1U) || (message->destination_group > 4U))
    {
        return 0U;
    }
    switch (message->type)
    {
        case GW_TYPE_READ_TEMP:
            return (message->payload_length == 1U) && (message->payload[0] <= 1U);
        case GW_TYPE_SET_FREQ:
        case GW_TYPE_SET_TARGET_TEMP:
            return message->payload_length == 2U;
        case GW_TYPE_MANUAL_RUN:
        case GW_TYPE_MANUAL_STOP:
        case GW_TYPE_SET_AUTO:
        case GW_TYPE_QUERY_STATUS:
            return message->payload_length == 0U;
        default:
            return 0U;
    }
}

static void Gateway_EncodeAndSend(const GatewayMessage *message, GatewayOutputPort port)
{
    uint8_t frame[GW_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t crc;
    if ((message == NULL) || (message->payload_length > GW_MAX_PAYLOAD_SIZE) ||
        (g_send_callback == NULL))
    {
        return;
    }
    frame[0] = GW_HEAD_1;
    frame[1] = GW_HEAD_2;
    frame[2] = GW_VERSION;
    frame[3] = message->type;
    frame[4] = message->source_role;
    frame[5] = message->source_group;
    frame[6] = message->destination_role;
    frame[7] = message->destination_group;
    frame[8] = (uint8_t)(message->flow_id & 0xFFU);
    frame[9] = (uint8_t)(message->flow_id >> 8U);
    frame[10] = message->payload_length;
    if (message->payload_length != 0U)
    {
        memcpy(&frame[11], message->payload, message->payload_length);
    }
    length = (uint16_t)(GW_MIN_FRAME_SIZE + message->payload_length);
    crc = Gateway_Crc16(&frame[2], (uint16_t)(9U + message->payload_length));
    frame[length - 2U] = (uint8_t)(crc & 0xFFU);
    frame[length - 1U] = (uint8_t)(crc >> 8U);
    g_send_callback(port, frame, length, g_send_context);
}

static void Gateway_Start(const GatewayMessage *message, uint8_t automatic_poll,
                          uint32_t now_ms)
{
    Gateway_EncodeAndSend(message, GATEWAY_OUTPUT_LORA);
    g_pending.active = 1U;
    g_pending.automatic_poll = automatic_poll;
    g_pending.group = message->destination_group;
    g_pending.request_type = message->type;
    g_pending.flow_id = message->flow_id;
    g_pending.deadline = now_ms + ((message->type == GW_TYPE_READ_TEMP) ?
                                   GW_TEMP_TIMEOUT_MS : GW_COMMAND_TIMEOUT_MS);
}

static void Gateway_QueuePcCommand(const GatewayMessage *message)
{
    if (g_pc_queue_count >= GW_PC_QUEUE_DEPTH)
    {
        return;
    }
    g_pc_queue[g_pc_queue_head].message = *message;
    g_pc_queue[g_pc_queue_head].automatic_poll = 0U;
    g_pc_queue_head = (uint8_t)((g_pc_queue_head + 1U) % GW_PC_QUEUE_DEPTH);
    g_pc_queue_count++;
}

static uint8_t Gateway_PendingComplete(const GatewayMessage *message)
{
    if ((g_pending.active == 0U) || (message->source_role != GW_ROLE_MASTER) ||
        (message->source_group != g_pending.group) ||
        (message->flow_id != g_pending.flow_id))
    {
        return 0U;
    }
    if (message->type == GW_TYPE_ERROR)
    {
        return 1U;
    }
    if (g_pending.request_type == GW_TYPE_READ_TEMP)
    {
        return message->type == GW_TYPE_TEMP_36;
    }
    return message->type == GW_TYPE_RESULT;
}

static void Gateway_HandleLoRa(const GatewayMessage *message)
{
    if ((message->source_role != GW_ROLE_MASTER) ||
        (message->source_group < 1U) || (message->source_group > 4U) ||
        (message->destination_role != GW_ROLE_CONTROL_ROOM) ||
        (message->destination_group != 0U))
    {
        return;
    }
    Gateway_EncodeAndSend(message, GATEWAY_OUTPUT_PC);
    if (Gateway_PendingComplete(message) != 0U)
    {
        g_pending.active = 0U;
    }
}

void GatewayRuntime_Init(GatewaySendCallback send_callback, void *context)
{
    (void)memset(&g_pc_rx, 0, sizeof(g_pc_rx));
    (void)memset(&g_lora_rx, 0, sizeof(g_lora_rx));
    (void)memset(&g_pc_parser, 0, sizeof(g_pc_parser));
    (void)memset(&g_lora_parser, 0, sizeof(g_lora_parser));
    (void)memset(&g_pc_queue, 0, sizeof(g_pc_queue));
    (void)memset(&g_pending, 0, sizeof(g_pending));
    g_pc_queue_head = 0U;
    g_pc_queue_tail = 0U;
    g_pc_queue_count = 0U;
    g_next_poll_group = 1U;
    g_next_auto_flow = 0x8000U;
    g_next_poll_tick = 0U;
    g_send_callback = send_callback;
    g_send_context = context;
}

void GatewayRuntime_PushPcByteFromIsr(uint8_t byte)
{
    Gateway_RingPushFromIsr(&g_pc_rx, byte);
}

void GatewayRuntime_PushLoRaByteFromIsr(uint8_t byte)
{
    Gateway_RingPushFromIsr(&g_lora_rx, byte);
}

void GatewayRuntime_Process(uint32_t now_ms)
{
    uint8_t byte;
    uint16_t count;
    GatewayMessage message;

    count = 0U;
    while ((count < 128U) && (Gateway_RingPop(&g_pc_rx, &byte) != 0U))
    {
        if ((Gateway_ParseByte(&g_pc_parser, byte, &message) != 0U) &&
            (Gateway_IsPcCommand(&message) != 0U))
        {
            Gateway_QueuePcCommand(&message);
        }
        count++;
    }
    count = 0U;
    while ((count < 128U) && (Gateway_RingPop(&g_lora_rx, &byte) != 0U))
    {
        if (Gateway_ParseByte(&g_lora_parser, byte, &message) != 0U)
        {
            Gateway_HandleLoRa(&message);
        }
        count++;
    }
    if ((g_pending.active != 0U) && ((uint32_t)(now_ms - g_pending.deadline) < 0x80000000UL))
    {
        g_pending.active = 0U;
    }
    if (g_pending.active != 0U)
    {
        return;
    }
    if (g_pc_queue_count != 0U)
    {
        GatewayQueuedMessage queued = g_pc_queue[g_pc_queue_tail];
        g_pc_queue_tail = (uint8_t)((g_pc_queue_tail + 1U) % GW_PC_QUEUE_DEPTH);
        g_pc_queue_count--;
        Gateway_Start(&queued.message, queued.automatic_poll, now_ms);
        return;
    }
    if ((uint32_t)(now_ms - g_next_poll_tick) < 0x80000000UL)
    {
        GatewayMessage poll;
        (void)memset(&poll, 0, sizeof(poll));
        poll.type = GW_TYPE_READ_TEMP;
        poll.source_role = GW_ROLE_CONTROL_ROOM;
        poll.destination_role = GW_ROLE_MASTER;
        poll.destination_group = g_next_poll_group;
        poll.flow_id = g_next_auto_flow++;
        poll.payload_length = 1U;
        poll.payload[0] = 0U;
        Gateway_Start(&poll, 1U, now_ms);
        g_next_poll_group = (g_next_poll_group >= 4U) ? 1U :
                            (uint8_t)(g_next_poll_group + 1U);
        g_next_poll_tick = now_ms + GW_POLL_INTERVAL_MS;
    }
}
