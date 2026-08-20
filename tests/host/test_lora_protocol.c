#include "lora_protocol.h"

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

static const uint8_t G_TO_M1_READ[] = {
    0xAA, 0x55, 0x01, 0x01, 0x01, 0x00, 0x02,
    0x01, 0x64, 0x00, 0x01, 0x00, 0xCF, 0x1C
};

static const uint8_t M1_TO_S1_READ[] = {
    0xAA, 0x55, 0x01, 0x01, 0x02, 0x01, 0x03,
    0x01, 0x64, 0x00, 0x01, 0x01, 0x5F, 0xD8
};

static const uint8_t S1_TO_M1_TEMP[] = {
    0xAA, 0x55, 0x01, 0x02, 0x03, 0x01, 0x02, 0x01, 0x64, 0x00, 0x48,
    0xFA, 0x00, 0xFB, 0x00, 0xFC, 0x00, 0xFD, 0x00, 0xFE, 0x00, 0xFF, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x02, 0x01, 0x03, 0x01, 0x04, 0x01, 0x05, 0x01,
    0x06, 0x01, 0x07, 0x01, 0x08, 0x01, 0x09, 0x01, 0x0A, 0x01, 0x0B, 0x01,
    0x0C, 0x01, 0x0D, 0x01, 0x0E, 0x01, 0x0F, 0x01, 0x10, 0x01, 0x11, 0x01,
    0x12, 0x01, 0x13, 0x01, 0x14, 0x01, 0x15, 0x01, 0x16, 0x01, 0x17, 0x01,
    0x18, 0x01, 0x19, 0x01, 0x1A, 0x01, 0x1B, 0x01, 0x1C, 0x01, 0x1D, 0x01,
    0xE3, 0x4F
};

static const uint8_t M1_TO_G_TEMP[] = {
    0xAA, 0x55, 0x01, 0x02, 0x02, 0x01, 0x01, 0x00, 0x64, 0x00, 0x48,
    0xFA, 0x00, 0xFB, 0x00, 0xFC, 0x00, 0xFD, 0x00, 0xFE, 0x00, 0xFF, 0x00,
    0x00, 0x01, 0x01, 0x01, 0x02, 0x01, 0x03, 0x01, 0x04, 0x01, 0x05, 0x01,
    0x06, 0x01, 0x07, 0x01, 0x08, 0x01, 0x09, 0x01, 0x0A, 0x01, 0x0B, 0x01,
    0x0C, 0x01, 0x0D, 0x01, 0x0E, 0x01, 0x0F, 0x01, 0x10, 0x01, 0x11, 0x01,
    0x12, 0x01, 0x13, 0x01, 0x14, 0x01, 0x15, 0x01, 0x16, 0x01, 0x17, 0x01,
    0x18, 0x01, 0x19, 0x01, 0x1A, 0x01, 0x1B, 0x01, 0x1C, 0x01, 0x1D, 0x01,
    0x2C, 0xAD
};

static void UpdateFrameCrc(uint8_t *frame, uint16_t frame_length)
{
    uint16_t crc = LoRaProtocol_Crc16(&frame[2], (uint16_t)(frame_length - 4U));

    frame[frame_length - 2U] = (uint8_t)(crc & 0x00FFU);
    frame[frame_length - 1U] = (uint8_t)((crc >> 8U) & 0x00FFU);
}

static void CheckVector(const uint8_t *frame,
                        uint16_t frame_length,
                        uint8_t expected_type,
                        uint8_t expected_source_role,
                        uint8_t expected_source_group,
                        uint8_t expected_destination_role,
                        uint8_t expected_destination_group,
                        uint8_t expected_payload_length)
{
    LoRaMessage message;
    uint8_t encoded[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t encoded_length = 0U;

    CHECK(LoRaProtocol_Decode(frame, frame_length, &message) == LORA_PROTOCOL_OK);
    CHECK(message.version == LORA_PROTOCOL_VERSION);
    CHECK(message.type == expected_type);
    CHECK(message.source_role == expected_source_role);
    CHECK(message.source_group == expected_source_group);
    CHECK(message.destination_role == expected_destination_role);
    CHECK(message.destination_group == expected_destination_group);
    CHECK(message.flow_id == 100U);
    CHECK(message.payload_length == expected_payload_length);
    CHECK(LoRaProtocol_Encode(&message, encoded, sizeof(encoded), &encoded_length) ==
          LORA_PROTOCOL_OK);
    CHECK(encoded_length == frame_length);
    CHECK(memcmp(encoded, frame, frame_length) == 0);
}

static void TestStandardVectors(void)
{
    CheckVector(G_TO_M1_READ,
                (uint16_t)sizeof(G_TO_M1_READ),
                (uint8_t)LORA_MSG_READ_TEMP,
                (uint8_t)LORA_ROLE_CONTROL_ROOM,
                0U,
                (uint8_t)LORA_ROLE_MASTER,
                1U,
                1U);

    CheckVector(M1_TO_S1_READ,
                (uint16_t)sizeof(M1_TO_S1_READ),
                (uint8_t)LORA_MSG_READ_TEMP,
                (uint8_t)LORA_ROLE_MASTER,
                1U,
                (uint8_t)LORA_ROLE_SLAVE,
                1U,
                1U);

    CheckVector(S1_TO_M1_TEMP,
                (uint16_t)sizeof(S1_TO_M1_TEMP),
                (uint8_t)LORA_MSG_TEMP_36,
                (uint8_t)LORA_ROLE_SLAVE,
                1U,
                (uint8_t)LORA_ROLE_MASTER,
                1U,
                LORA_PROTOCOL_TEMP_PAYLOAD_SIZE);

    CheckVector(M1_TO_G_TEMP,
                (uint16_t)sizeof(M1_TO_G_TEMP),
                (uint8_t)LORA_MSG_TEMP_36,
                (uint8_t)LORA_ROLE_MASTER,
                1U,
                (uint8_t)LORA_ROLE_CONTROL_ROOM,
                0U,
                LORA_PROTOCOL_TEMP_PAYLOAD_SIZE);
}

static void TestKnownCrcValue(void)
{
    static const uint8_t MODBUS_TEXT[] = {
        (uint8_t)'1', (uint8_t)'2', (uint8_t)'3',
        (uint8_t)'4', (uint8_t)'5', (uint8_t)'6',
        (uint8_t)'7', (uint8_t)'8', (uint8_t)'9'
    };

    CHECK(LoRaProtocol_Crc16(MODBUS_TEXT, (uint16_t)sizeof(MODBUS_TEXT)) == 0x4B37U);
    CHECK(LoRaProtocol_Crc16(NULL, 0U) == 0xFFFFU);
}

static void CheckSingleBitCorruption(const uint8_t *valid_frame,
                                     uint16_t frame_length)
{
    uint8_t corrupted[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t byte_index;
    uint8_t bit_index;
    LoRaMessage message;

    for (byte_index = 0U; byte_index < frame_length; byte_index++)
    {
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            (void)memcpy(corrupted, valid_frame, frame_length);
            corrupted[byte_index] ^= (uint8_t)(1U << bit_index);
            CHECK(LoRaProtocol_Decode(corrupted, frame_length, &message) !=
                  LORA_PROTOCOL_OK);
        }
    }
}

static void TestSingleBitCorruption(void)
{
    CheckSingleBitCorruption(G_TO_M1_READ, (uint16_t)sizeof(G_TO_M1_READ));
    CheckSingleBitCorruption(M1_TO_S1_READ, (uint16_t)sizeof(M1_TO_S1_READ));
    CheckSingleBitCorruption(S1_TO_M1_TEMP, (uint16_t)sizeof(S1_TO_M1_TEMP));
    CheckSingleBitCorruption(M1_TO_G_TEMP, (uint16_t)sizeof(M1_TO_G_TEMP));
}

static void TestTemperaturePayload(void)
{
    LoRaMessage message;
    uint8_t index;

    CHECK(LoRaProtocol_Decode(S1_TO_M1_TEMP,
                              (uint16_t)sizeof(S1_TO_M1_TEMP),
                              &message) == LORA_PROTOCOL_OK);

    for (index = 0U; index < LORA_PROTOCOL_TEMP_COUNT; index++)
    {
        uint16_t raw = (uint16_t)((uint16_t)message.payload[index * 2U] |
                                  (uint16_t)((uint16_t)message.payload[index * 2U + 1U] << 8U));
        CHECK(raw == (uint16_t)(250U + index));
    }
}

static void TestInvalidFrames(void)
{
    uint8_t frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
    LoRaMessage message;

    CHECK(LoRaProtocol_Decode(NULL, 0U, &message) == LORA_PROTOCOL_NULL_POINTER);
    CHECK(LoRaProtocol_Decode(G_TO_M1_READ,
                              (uint16_t)sizeof(G_TO_M1_READ),
                              NULL) == LORA_PROTOCOL_NULL_POINTER);
    CHECK(LoRaProtocol_Decode(G_TO_M1_READ, 12U, &message) ==
          LORA_PROTOCOL_FRAME_TOO_SHORT);

    (void)memcpy(frame, G_TO_M1_READ, sizeof(G_TO_M1_READ));
    frame[0] = 0xABU;
    CHECK(LoRaProtocol_Decode(frame, (uint16_t)sizeof(G_TO_M1_READ), &message) ==
          LORA_PROTOCOL_INVALID_HEADER);

    (void)memcpy(frame, G_TO_M1_READ, sizeof(G_TO_M1_READ));
    frame[2] = 0x02U;
    UpdateFrameCrc(frame, (uint16_t)sizeof(G_TO_M1_READ));
    CHECK(LoRaProtocol_Decode(frame, (uint16_t)sizeof(G_TO_M1_READ), &message) ==
          LORA_PROTOCOL_UNSUPPORTED_VERSION);

    (void)memcpy(frame, G_TO_M1_READ, sizeof(G_TO_M1_READ));
    frame[11] ^= 0x01U;
    CHECK(LoRaProtocol_Decode(frame, (uint16_t)sizeof(G_TO_M1_READ), &message) ==
          LORA_PROTOCOL_CRC_MISMATCH);

    (void)memcpy(frame, G_TO_M1_READ, sizeof(G_TO_M1_READ));
    frame[10] = 2U;
    CHECK(LoRaProtocol_Decode(frame, (uint16_t)sizeof(G_TO_M1_READ), &message) ==
          LORA_PROTOCOL_FRAME_LENGTH_MISMATCH);

    (void)memcpy(frame, G_TO_M1_READ, sizeof(G_TO_M1_READ));
    frame[sizeof(G_TO_M1_READ)] = 0U;
    CHECK(LoRaProtocol_Decode(frame,
                              (uint16_t)(sizeof(G_TO_M1_READ) + 1U),
                              &message) == LORA_PROTOCOL_FRAME_LENGTH_MISMATCH);

    (void)memcpy(frame, G_TO_M1_READ, sizeof(G_TO_M1_READ));
    frame[10] = (uint8_t)(LORA_PROTOCOL_MAX_PAYLOAD_SIZE + 1U);
    CHECK(LoRaProtocol_Decode(frame, (uint16_t)sizeof(G_TO_M1_READ), &message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);
}

static LoRaMessage MakeMessage(uint8_t type,
                               uint8_t source_role,
                               uint8_t source_group,
                               uint8_t destination_role,
                               uint8_t destination_group,
                               uint8_t payload_length)
{
    LoRaMessage message;

    (void)memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION;
    message.type = type;
    message.source_role = source_role;
    message.source_group = source_group;
    message.destination_role = destination_role;
    message.destination_group = destination_group;
    message.flow_id = 1U;
    message.payload_length = payload_length;

    return message;
}

static void TestMessageValidation(void)
{
    LoRaMessage message;
    uint8_t output[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t output_length = 99U;

    message = MakeMessage((uint8_t)LORA_MSG_MANUAL_STOP,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          0U);
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK);
    CHECK(LoRaProtocol_Encode(&message, output, 12U, &output_length) ==
          LORA_PROTOCOL_OUTPUT_TOO_SMALL);
    CHECK(output_length == 0U);

    message.source_role = 0x04U;
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_ROLE);

    message = MakeMessage((uint8_t)LORA_MSG_MANUAL_STOP,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          1U,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          0U);
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_GROUP);

    message = MakeMessage((uint8_t)LORA_MSG_MANUAL_STOP,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          0U);
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_INVALID_DIRECTION);

    message = MakeMessage(0x55U,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          0U);
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_UNSUPPORTED_TYPE);

    message = MakeMessage((uint8_t)LORA_MSG_READ_TEMP,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          0U);
    CHECK(LoRaProtocol_ValidateMessage(&message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);

    message = MakeMessage((uint8_t)LORA_MSG_ERROR,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          1U);
    message.payload[0] = 1U;
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK);
    message.payload_length = 16U;
    CHECK(LoRaProtocol_ValidateMessage(&message) == LORA_PROTOCOL_OK);
    message.payload_length = 17U;
    CHECK(LoRaProtocol_ValidateMessage(&message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH);

    message = MakeMessage((uint8_t)LORA_MSG_READ_TEMP,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          1U);
    message.payload[0] = 2U;
    CHECK(LoRaProtocol_ValidateMessage(&message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_VALUE);

    message = MakeMessage((uint8_t)LORA_MSG_ACK,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          2U);
    message.payload[0] = 3U;
    CHECK(LoRaProtocol_ValidateMessage(&message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_VALUE);

    message = MakeMessage((uint8_t)LORA_MSG_RESULT,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          7U);
    message.payload[0] = 0U;
    message.payload[1] = 3U;
    message.payload[2] = 0U;
    CHECK(LoRaProtocol_ValidateMessage(&message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_VALUE);

    message = MakeMessage((uint8_t)LORA_MSG_ERROR,
                          (uint8_t)LORA_ROLE_MASTER,
                          1U,
                          (uint8_t)LORA_ROLE_CONTROL_ROOM,
                          0U,
                          1U);
    message.payload[0] = 0U;
    CHECK(LoRaProtocol_ValidateMessage(&message) ==
          LORA_PROTOCOL_INVALID_PAYLOAD_VALUE);

    CHECK(LoRaProtocol_ValidateMessage(NULL) == LORA_PROTOCOL_NULL_POINTER);
    CHECK(LoRaProtocol_Encode(NULL, output, sizeof(output), &output_length) ==
          LORA_PROTOCOL_NULL_POINTER);
    CHECK(LoRaProtocol_Encode(&message, NULL, sizeof(output), &output_length) ==
          LORA_PROTOCOL_NULL_POINTER);
    CHECK(LoRaProtocol_Encode(&message, output, sizeof(output), NULL) ==
          LORA_PROTOCOL_NULL_POINTER);
    CHECK(LoRaProtocol_Crc16(NULL, 1U) == 0U);
}

int main(void)
{
    TestStandardVectors();
    TestKnownCrcValue();
    TestSingleBitCorruption();
    TestTemperaturePayload();
    TestInvalidFrames();
    TestMessageValidation();

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
