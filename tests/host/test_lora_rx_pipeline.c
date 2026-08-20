#include "lora_stream_parser.h"
#include "lora_transport.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int g_check_count;
static unsigned int g_failure_count;

#define CHECK(condition)                                                         \
    do                                                                           \
    {                                                                            \
        g_check_count++;                                                         \
        if (!(condition))                                                        \
        {                                                                        \
            g_failure_count++;                                                   \
            (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                        \
    } while (0)

static const uint8_t READ_FRAME[] = {
    0xAA, 0x55, 0x01, 0x01, 0x01, 0x00, 0x02,
    0x01, 0x64, 0x00, 0x01, 0x00, 0xCF, 0x1C
};

static void PushBufferFromIsr(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        CHECK(LoraTransport_PushRxFromIsr(data[index]) == 1U);
    }
}

static unsigned int DrainToParser(LoRaStreamParser *parser,
                                  LoRaMessage *message)
{
    uint8_t byte;
    unsigned int ready_count = 0U;

    while (LoraTransport_PopRx(&byte) != 0U)
    {
        LoRaStreamResult result = LoRaStreamParser_PushByte(parser, byte, message);
        CHECK(result != LORA_STREAM_INVALID_ARGUMENT);
        if (result == LORA_STREAM_FRAME_READY)
        {
            ready_count++;
        }
    }

    return ready_count;
}

static void TestApplicationModeGate(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;

    LoraTransport_Init();
    LoRaStreamParser_Init(&parser);
    CHECK(LoraTransport_PushRxFromIsr(0xAAU) == 0U);
    CHECK(DrainToParser(&parser, &message) == 0U);

    LoraTransport_EnableApplicationMode();
    PushBufferFromIsr(READ_FRAME, (uint16_t)sizeof(READ_FRAME));
    CHECK(DrainToParser(&parser, &message) == 1U);
    CHECK(message.type == (uint8_t)LORA_MSG_READ_TEMP);
    CHECK(message.flow_id == 100U);
}

static void TestBackToBackFrames(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;

    LoraTransport_Init();
    LoraTransport_EnableApplicationMode();
    LoRaStreamParser_Init(&parser);

    PushBufferFromIsr(READ_FRAME, (uint16_t)sizeof(READ_FRAME));
    PushBufferFromIsr(READ_FRAME, (uint16_t)sizeof(READ_FRAME));
    CHECK(DrainToParser(&parser, &message) == 2U);
    CHECK(parser.accepted_frame_count == 2U);
    CHECK(parser.rejected_frame_count == 0U);
}

static void TestOverflowRecovery(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;
    uint16_t index;
    uint8_t byte;
    uint32_t previous_loss_count;

    LoraTransport_Init();
    LoraTransport_EnableApplicationMode();
    LoRaStreamParser_Init(&parser);

    PushBufferFromIsr(READ_FRAME, 7U);
    CHECK(DrainToParser(&parser, &message) == 0U);
    CHECK(parser.length == 7U);

    previous_loss_count = LoraTransport_GetDataLossCount();
    for (index = 0U; index < 255U; index++)
    {
        CHECK(LoraTransport_PushRxFromIsr(0x11U) == 1U);
    }
    CHECK(LoraTransport_PushRxFromIsr(0x22U) == 0U);
    CHECK(LoraTransport_GetDataLossCount() == (previous_loss_count + 1U));

    LoRaStreamParser_AbortPartialFrame(&parser);
    CHECK(parser.length == 0U);
    CHECK(parser.aborted_frame_count == 1U);
    while (LoraTransport_PopRx(&byte) != 0U)
    {
        CHECK(LoRaStreamParser_PushByte(&parser, byte, &message) ==
              LORA_STREAM_WAITING);
    }

    PushBufferFromIsr(READ_FRAME, (uint16_t)sizeof(READ_FRAME));
    CHECK(DrainToParser(&parser, &message) == 1U);
    CHECK(message.flow_id == 100U);
}

static void TestUartErrorRecovery(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;
    uint32_t previous_loss_count;

    LoraTransport_Init();
    LoraTransport_EnableApplicationMode();
    LoRaStreamParser_Init(&parser);

    PushBufferFromIsr(READ_FRAME, 5U);
    CHECK(DrainToParser(&parser, &message) == 0U);
    CHECK(parser.length == 5U);

    previous_loss_count = LoraTransport_GetDataLossCount();
    LoraTransport_ReportRxErrorFromIsr();
    CHECK(LoraTransport_GetDataLossCount() == (previous_loss_count + 1U));
    LoRaStreamParser_AbortPartialFrame(&parser);

    PushBufferFromIsr(READ_FRAME, (uint16_t)sizeof(READ_FRAME));
    CHECK(DrainToParser(&parser, &message) == 1U);
    CHECK(parser.aborted_frame_count == 1U);
}

int main(void)
{
    TestApplicationModeGate();
    TestBackToBackFrames();
    TestOverflowRecovery();
    TestUartErrorRecovery();

    if (g_failure_count == 0U)
    {
        (void)printf("PASS: %u checks\n", g_check_count);
        return 0;
    }

    (void)printf("FAIL: %u of %u checks failed\n",
                 g_failure_count,
                 g_check_count);
    return 1;
}

