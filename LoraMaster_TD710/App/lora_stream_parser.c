#include "lora_stream_parser.h"

#include <string.h>

static void LoRaStreamParser_ResetFrame(LoRaStreamParser *parser)
{
    parser->length = 0U;
    parser->expected_length = 0U;
}

void LoRaStreamParser_Init(LoRaStreamParser *parser)
{
    if (parser != NULL)
    {
        (void)memset(parser, 0, sizeof(*parser));
    }
}

LoRaStreamResult LoRaStreamParser_PushByte(LoRaStreamParser *parser,
                                           uint8_t byte,
                                           LoRaMessage *message)
{
    LoRaProtocolStatus decode_status;

    if ((parser == NULL) || (message == NULL))
    {
        return LORA_STREAM_INVALID_ARGUMENT;
    }

    if (parser->length == 0U)
    {
        if (byte == LORA_PROTOCOL_HEADER_1)
        {
            parser->frame[0] = byte;
            parser->length = 1U;
        }
        else
        {
            parser->discarded_byte_count++;
        }
        return LORA_STREAM_WAITING;
    }

    if (parser->length == 1U)
    {
        if (byte == LORA_PROTOCOL_HEADER_2)
        {
            parser->frame[1] = byte;
            parser->length = 2U;
        }
        else if (byte == LORA_PROTOCOL_HEADER_1)
        {
            /* 保留当前 AA 作为新候选帧头，丢弃前一个 AA。 */
            parser->frame[0] = byte;
            parser->discarded_byte_count++;
        }
        else
        {
            parser->discarded_byte_count += 2U;
            LoRaStreamParser_ResetFrame(parser);
        }
        return LORA_STREAM_WAITING;
    }

    if (parser->length >= LORA_PROTOCOL_MAX_FRAME_SIZE)
    {
        parser->rejected_frame_count++;
        LoRaStreamParser_ResetFrame(parser);
        return LORA_STREAM_FRAME_REJECTED;
    }

    parser->frame[parser->length] = byte;
    parser->length++;

    if (parser->length == 11U)
    {
        uint8_t payload_length = parser->frame[10];

        if (payload_length > LORA_PROTOCOL_MAX_PAYLOAD_SIZE)
        {
            parser->rejected_frame_count++;
            LoRaStreamParser_ResetFrame(parser);
            return LORA_STREAM_FRAME_REJECTED;
        }

        parser->expected_length = (uint16_t)(LORA_PROTOCOL_MIN_FRAME_SIZE +
                                              payload_length);
    }

    if ((parser->expected_length != 0U) &&
        (parser->length == parser->expected_length))
    {
        decode_status = LoRaProtocol_Decode(parser->frame,
                                            parser->length,
                                            message);
        LoRaStreamParser_ResetFrame(parser);

        if (decode_status == LORA_PROTOCOL_OK)
        {
            parser->accepted_frame_count++;
            return LORA_STREAM_FRAME_READY;
        }

        parser->rejected_frame_count++;
        return LORA_STREAM_FRAME_REJECTED;
    }

    return LORA_STREAM_WAITING;
}

void LoRaStreamParser_AbortPartialFrame(LoRaStreamParser *parser)
{
    if ((parser != NULL) && (parser->length != 0U))
    {
        parser->aborted_frame_count++;
        LoRaStreamParser_ResetFrame(parser);
    }
}

