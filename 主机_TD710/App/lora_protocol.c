#include "lora_protocol.h"

#include <string.h>

#define LORA_PROTOCOL_FIXED_CRC_INPUT_SIZE    (9U)

static LoRaProtocolStatus LoRaProtocol_ValidateAddress(uint8_t role, uint8_t group)
{
    if ((role < (uint8_t)LORA_ROLE_CONTROL_ROOM) ||
        (role > (uint8_t)LORA_ROLE_SLAVE))
    {
        return LORA_PROTOCOL_INVALID_ROLE;
    }

    if (role == (uint8_t)LORA_ROLE_CONTROL_ROOM)
    {
        return (group == 0U) ? LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_GROUP;
    }

    return ((group >= 1U) && (group <= 4U)) ?
           LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_GROUP;
}

static LoRaProtocolStatus LoRaProtocol_ValidatePayloadLength(uint8_t type,
                                                              uint8_t payload_length)
{
    switch (type)
    {
        case (uint8_t)LORA_MSG_READ_TEMP:
            return (payload_length == 1U) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        case (uint8_t)LORA_MSG_TEMP_36:
            return (payload_length == LORA_PROTOCOL_TEMP_PAYLOAD_SIZE) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        case (uint8_t)LORA_MSG_SET_FREQ:
        case (uint8_t)LORA_MSG_SET_TARGET_TEMP:
        case (uint8_t)LORA_MSG_ACK:
            return (payload_length == 2U) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        case (uint8_t)LORA_MSG_MANUAL_RUN:
        case (uint8_t)LORA_MSG_MANUAL_STOP:
        case (uint8_t)LORA_MSG_SET_AUTO:
        case (uint8_t)LORA_MSG_QUERY_STATUS:
            return (payload_length == 0U) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        case (uint8_t)LORA_MSG_RESULT:
            return (payload_length == 7U) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        case (uint8_t)LORA_MSG_ERROR:
            return ((payload_length >= 1U) && (payload_length <= 16U)) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;

        default:
            return LORA_PROTOCOL_UNSUPPORTED_TYPE;
    }
}

static LoRaProtocolStatus LoRaProtocol_ValidateDirection(const LoRaMessage *message)
{
    uint8_t source_role = message->source_role;
    uint8_t destination_role = message->destination_role;

    switch (message->type)
    {
        case (uint8_t)LORA_MSG_READ_TEMP:
            if (((source_role == (uint8_t)LORA_ROLE_CONTROL_ROOM) &&
                 (destination_role == (uint8_t)LORA_ROLE_MASTER)) ||
                ((source_role == (uint8_t)LORA_ROLE_MASTER) &&
                 (destination_role == (uint8_t)LORA_ROLE_SLAVE)))
            {
                return LORA_PROTOCOL_OK;
            }
            break;

        case (uint8_t)LORA_MSG_TEMP_36:
            if (((source_role == (uint8_t)LORA_ROLE_SLAVE) &&
                 (destination_role == (uint8_t)LORA_ROLE_MASTER)) ||
                ((source_role == (uint8_t)LORA_ROLE_MASTER) &&
                 (destination_role == (uint8_t)LORA_ROLE_CONTROL_ROOM)))
            {
                return LORA_PROTOCOL_OK;
            }
            break;

        case (uint8_t)LORA_MSG_SET_FREQ:
        case (uint8_t)LORA_MSG_SET_TARGET_TEMP:
        case (uint8_t)LORA_MSG_MANUAL_RUN:
        case (uint8_t)LORA_MSG_MANUAL_STOP:
        case (uint8_t)LORA_MSG_SET_AUTO:
        case (uint8_t)LORA_MSG_QUERY_STATUS:
            if ((source_role == (uint8_t)LORA_ROLE_CONTROL_ROOM) &&
                (destination_role == (uint8_t)LORA_ROLE_MASTER))
            {
                return LORA_PROTOCOL_OK;
            }
            break;

        case (uint8_t)LORA_MSG_ACK:
        case (uint8_t)LORA_MSG_RESULT:
            if ((source_role == (uint8_t)LORA_ROLE_MASTER) &&
                (destination_role == (uint8_t)LORA_ROLE_CONTROL_ROOM))
            {
                return LORA_PROTOCOL_OK;
            }
            break;

        case (uint8_t)LORA_MSG_ERROR:
            return LORA_PROTOCOL_OK;

        default:
            return LORA_PROTOCOL_UNSUPPORTED_TYPE;
    }

    return LORA_PROTOCOL_INVALID_DIRECTION;
}

static LoRaProtocolStatus LoRaProtocol_ValidatePayloadValue(const LoRaMessage *message)
{
    switch (message->type)
    {
        case (uint8_t)LORA_MSG_READ_TEMP:
            return (message->payload[0] <= 1U) ?
                   LORA_PROTOCOL_OK : LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;

        case (uint8_t)LORA_MSG_ACK:
            if ((message->payload[0] <= 2U) && (message->payload[1] <= 9U))
            {
                return LORA_PROTOCOL_OK;
            }
            return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;

        case (uint8_t)LORA_MSG_RESULT:
            if ((message->payload[0] <= 9U) &&
                (message->payload[1] <= 2U) &&
                (message->payload[2] <= 2U))
            {
                return LORA_PROTOCOL_OK;
            }
            return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;

        case (uint8_t)LORA_MSG_ERROR:
            if ((message->payload[0] >= 1U) && (message->payload[0] <= 9U))
            {
                return LORA_PROTOCOL_OK;
            }
            return LORA_PROTOCOL_INVALID_PAYLOAD_VALUE;

        default:
            return LORA_PROTOCOL_OK;
    }
}

uint16_t LoRaProtocol_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        crc = (uint16_t)(crc ^ (uint16_t)data[index]);
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

LoRaProtocolStatus LoRaProtocol_ValidateMessage(const LoRaMessage *message)
{
    LoRaProtocolStatus status;

    if (message == NULL)
    {
        return LORA_PROTOCOL_NULL_POINTER;
    }

    if (message->version != LORA_PROTOCOL_VERSION)
    {
        return LORA_PROTOCOL_UNSUPPORTED_VERSION;
    }

    status = LoRaProtocol_ValidateAddress(message->source_role,
                                          message->source_group);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }

    status = LoRaProtocol_ValidateAddress(message->destination_role,
                                          message->destination_group);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }

    if (message->payload_length > LORA_PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
    }

    status = LoRaProtocol_ValidatePayloadLength(message->type,
                                                message->payload_length);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }

    status = LoRaProtocol_ValidatePayloadValue(message);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }

    return LoRaProtocol_ValidateDirection(message);
}

LoRaProtocolStatus LoRaProtocol_Encode(const LoRaMessage *message,
                                       uint8_t *output,
                                       uint16_t output_size,
                                       uint16_t *output_length)
{
    LoRaProtocolStatus status;
    uint16_t frame_length;
    uint16_t crc;

    if ((message == NULL) || (output == NULL) || (output_length == NULL))
    {
        return LORA_PROTOCOL_NULL_POINTER;
    }

    *output_length = 0U;
    status = LoRaProtocol_ValidateMessage(message);
    if (status != LORA_PROTOCOL_OK)
    {
        return status;
    }

    frame_length = (uint16_t)(LORA_PROTOCOL_MIN_FRAME_SIZE +
                              message->payload_length);
    if (output_size < frame_length)
    {
        return LORA_PROTOCOL_OUTPUT_TOO_SMALL;
    }

    output[0] = LORA_PROTOCOL_HEADER_1;
    output[1] = LORA_PROTOCOL_HEADER_2;
    output[2] = message->version;
    output[3] = message->type;
    output[4] = message->source_role;
    output[5] = message->source_group;
    output[6] = message->destination_role;
    output[7] = message->destination_group;
    output[8] = (uint8_t)(message->flow_id & 0x00FFU);
    output[9] = (uint8_t)((message->flow_id >> 8U) & 0x00FFU);
    output[10] = message->payload_length;

    if (message->payload_length != 0U)
    {
        memcpy(&output[11], message->payload, message->payload_length);
    }

    crc = LoRaProtocol_Crc16(&output[2],
                             (uint16_t)(LORA_PROTOCOL_FIXED_CRC_INPUT_SIZE +
                                        message->payload_length));
    output[frame_length - 2U] = (uint8_t)(crc & 0x00FFU);
    output[frame_length - 1U] = (uint8_t)((crc >> 8U) & 0x00FFU);
    *output_length = frame_length;

    return LORA_PROTOCOL_OK;
}

LoRaProtocolStatus LoRaProtocol_Decode(const uint8_t *frame,
                                       uint16_t frame_length,
                                       LoRaMessage *message)
{
    uint8_t payload_length;
    uint16_t expected_length;
    uint16_t received_crc;
    uint16_t calculated_crc;

    if ((frame == NULL) || (message == NULL))
    {
        return LORA_PROTOCOL_NULL_POINTER;
    }

    if (frame_length < LORA_PROTOCOL_MIN_FRAME_SIZE)
    {
        return LORA_PROTOCOL_FRAME_TOO_SHORT;
    }

    if ((frame[0] != LORA_PROTOCOL_HEADER_1) ||
        (frame[1] != LORA_PROTOCOL_HEADER_2))
    {
        return LORA_PROTOCOL_INVALID_HEADER;
    }

    payload_length = frame[10];
    if (payload_length > LORA_PROTOCOL_MAX_PAYLOAD_SIZE)
    {
        return LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH;
    }

    expected_length = (uint16_t)(LORA_PROTOCOL_MIN_FRAME_SIZE + payload_length);
    if (frame_length != expected_length)
    {
        return LORA_PROTOCOL_FRAME_LENGTH_MISMATCH;
    }

    received_crc = (uint16_t)((uint16_t)frame[frame_length - 2U] |
                              (uint16_t)((uint16_t)frame[frame_length - 1U] << 8U));
    calculated_crc = LoRaProtocol_Crc16(&frame[2],
                                        (uint16_t)(LORA_PROTOCOL_FIXED_CRC_INPUT_SIZE +
                                                   payload_length));
    if (received_crc != calculated_crc)
    {
        return LORA_PROTOCOL_CRC_MISMATCH;
    }

    memset(message, 0, sizeof(*message));
    message->version = frame[2];
    message->type = frame[3];
    message->source_role = frame[4];
    message->source_group = frame[5];
    message->destination_role = frame[6];
    message->destination_group = frame[7];
    message->flow_id = (uint16_t)((uint16_t)frame[8] |
                                  (uint16_t)((uint16_t)frame[9] << 8U));
    message->payload_length = payload_length;

    if (payload_length != 0U)
    {
        memcpy(message->payload, &frame[11], payload_length);
    }

    return LoRaProtocol_ValidateMessage(message);
}
