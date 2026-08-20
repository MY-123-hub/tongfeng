#include "lora_stream_parser.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static const uint8_t TEMP_FRAME[] = {
    0xAA, 0x55, 0x01, 0x02, 0x03, 0x01, 0x02, 0x01, 0x64, 0x00, 0x48,
    0xFA, 0x00, 0xFB, 0x00, 0xFC, 0x00, 0xFD, 0x00, 0xFE, 0x00, 0xFF, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x02, 0x01, 0x03, 0x01, 0x04, 0x01, 0x05, 0x01,
    0x06, 0x01, 0x07, 0x01, 0x08, 0x01, 0x09, 0x01, 0x0A, 0x01, 0x0B, 0x01,
    0x0C, 0x01, 0x0D, 0x01, 0x0E, 0x01, 0x0F, 0x01, 0x10, 0x01, 0x11, 0x01,
    0x12, 0x01, 0x13, 0x01, 0x14, 0x01, 0x15, 0x01, 0x16, 0x01, 0x17, 0x01,
    0x18, 0x01, 0x19, 0x01, 0x1A, 0x01, 0x1B, 0x01, 0x1C, 0x01, 0x1D, 0x01,
    0xE3, 0x4F
};

static unsigned int FeedFrame(LoRaStreamParser *parser,
                              const uint8_t *data,
                              uint16_t length,
                              LoRaMessage *message)
{
    uint16_t index;
    unsigned int ready_count = 0U;

    for (index = 0U; index < length; index++)
    {
        LoRaStreamResult result = LoRaStreamParser_PushByte(parser,
                                                            data[index],
                                                            message);
        CHECK(result != LORA_STREAM_INVALID_ARGUMENT);
        if (result == LORA_STREAM_FRAME_READY)
        {
            ready_count++;
        }
    }

    return ready_count;
}

static void TestByteByByteFrames(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;

    LoRaStreamParser_Init(&parser);
    CHECK(FeedFrame(&parser,
                    READ_FRAME,
                    (uint16_t)sizeof(READ_FRAME),
                    &message) == 1U);
    CHECK(message.type == (uint8_t)LORA_MSG_READ_TEMP);
    CHECK(message.flow_id == 100U);
    CHECK(parser.accepted_frame_count == 1U);
    CHECK(parser.rejected_frame_count == 0U);
    CHECK(parser.length == 0U);

    CHECK(FeedFrame(&parser,
                    TEMP_FRAME,
                    (uint16_t)sizeof(TEMP_FRAME),
                    &message) == 1U);
    CHECK(message.type == (uint8_t)LORA_MSG_TEMP_36);
    CHECK(message.payload_length == LORA_PROTOCOL_TEMP_PAYLOAD_SIZE);
    CHECK(parser.accepted_frame_count == 2U);
}

static void TestNoiseAndRepeatedHeader(void)
{
    static const uint8_t NOISE[] = {
        0x00, 0x11, 0x55, 0xFE, 0xAA, 0x00, 0xAA, 0xAA
    };
    LoRaStreamParser parser;
    LoRaMessage message;

    LoRaStreamParser_Init(&parser);
    CHECK(FeedFrame(&parser,
                    NOISE,
                    (uint16_t)sizeof(NOISE),
                    &message) == 0U);
    CHECK(parser.length == 1U);
    CHECK(FeedFrame(&parser,
                    &READ_FRAME[1],
                    (uint16_t)(sizeof(READ_FRAME) - 1U),
                    &message) == 1U);
    CHECK(parser.accepted_frame_count == 1U);
    CHECK(parser.discarded_byte_count == 7U);
}

static void TestConcatenatedFrames(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;
    unsigned int ready_count = 0U;

    LoRaStreamParser_Init(&parser);
    ready_count += FeedFrame(&parser,
                             READ_FRAME,
                             (uint16_t)sizeof(READ_FRAME),
                             &message);
    ready_count += FeedFrame(&parser,
                             READ_FRAME,
                             (uint16_t)sizeof(READ_FRAME),
                             &message);
    ready_count += FeedFrame(&parser,
                             TEMP_FRAME,
                             (uint16_t)sizeof(TEMP_FRAME),
                             &message);

    CHECK(ready_count == 3U);
    CHECK(parser.accepted_frame_count == 3U);
    CHECK(parser.length == 0U);
}

static void TestRejectedFrameRecovery(void)
{
    uint8_t corrupted[sizeof(READ_FRAME)];
    uint8_t invalid_length[11] = {
        0xAA, 0x55, 0x01, 0x01, 0x01, 0x00,
        0x02, 0x01, 0x64, 0x00, 0xFF
    };
    LoRaStreamParser parser;
    LoRaMessage message;

    LoRaStreamParser_Init(&parser);
    (void)memcpy(corrupted, READ_FRAME, sizeof(corrupted));
    corrupted[11] ^= 1U;

    CHECK(FeedFrame(&parser,
                    corrupted,
                    (uint16_t)sizeof(corrupted),
                    &message) == 0U);
    CHECK(parser.rejected_frame_count == 1U);
    CHECK(parser.length == 0U);

    CHECK(FeedFrame(&parser,
                    READ_FRAME,
                    (uint16_t)sizeof(READ_FRAME),
                    &message) == 1U);
    CHECK(parser.accepted_frame_count == 1U);

    CHECK(FeedFrame(&parser,
                    invalid_length,
                    (uint16_t)sizeof(invalid_length),
                    &message) == 0U);
    CHECK(parser.rejected_frame_count == 2U);
    CHECK(parser.length == 0U);

    CHECK(FeedFrame(&parser,
                    READ_FRAME,
                    (uint16_t)sizeof(READ_FRAME),
                    &message) == 1U);
    CHECK(parser.accepted_frame_count == 2U);
}

static void TestAbortPartialFrame(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;

    LoRaStreamParser_Init(&parser);
    CHECK(FeedFrame(&parser, READ_FRAME, 7U, &message) == 0U);
    CHECK(parser.length == 7U);
    LoRaStreamParser_AbortPartialFrame(&parser);
    CHECK(parser.length == 0U);
    CHECK(parser.aborted_frame_count == 1U);
    LoRaStreamParser_AbortPartialFrame(&parser);
    CHECK(parser.aborted_frame_count == 1U);
}

static void TestLongNoiseRecovery(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;
    uint8_t noise;
    unsigned int block;
    unsigned int index;
    unsigned int ready_count = 0U;

    LoRaStreamParser_Init(&parser);
    for (block = 0U; block < 100U; block++)
    {
        for (index = 0U; index < 37U; index++)
        {
            noise = (uint8_t)((block + index) % 0xAAU);
            CHECK(LoRaStreamParser_PushByte(&parser, noise, &message) ==
                  LORA_STREAM_WAITING);
        }
        ready_count += FeedFrame(&parser,
                                 READ_FRAME,
                                 (uint16_t)sizeof(READ_FRAME),
                                 &message);
    }

    CHECK(ready_count == 100U);
    CHECK(parser.accepted_frame_count == 100U);
    CHECK(parser.rejected_frame_count == 0U);
    CHECK(parser.discarded_byte_count == 3700U);
}

static void TestInvalidArguments(void)
{
    LoRaStreamParser parser;
    LoRaMessage message;

    LoRaStreamParser_Init(NULL);
    LoRaStreamParser_Init(&parser);
    CHECK(LoRaStreamParser_PushByte(NULL, 0U, &message) ==
          LORA_STREAM_INVALID_ARGUMENT);
    CHECK(LoRaStreamParser_PushByte(&parser, 0U, NULL) ==
          LORA_STREAM_INVALID_ARGUMENT);
    LoRaStreamParser_AbortPartialFrame(NULL);
}

int main(void)
{
    TestByteByByteFrames();
    TestNoiseAndRepeatedHeader();
    TestConcatenatedFrames();
    TestRejectedFrameRecovery();
    TestAbortPartialFrame();
    TestLongNoiseRecovery();
    TestInvalidArguments();

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

