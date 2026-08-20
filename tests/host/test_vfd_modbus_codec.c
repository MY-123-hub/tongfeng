#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "master_config.h"
#include "master_messages.h"
#include "vfd_modbus_codec.h"

static uint32_t g_checks;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        g_checks++;                                                             \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

static void TestKnownVectors(void)
{
    static const uint8_t run_request[VFD_MODBUS_REQUEST_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x04U,
        0x00U, 0x01U, 0x13U, 0x88U, 0x36U, 0xF8U
    };
    static const uint8_t stop_request[VFD_MODBUS_REQUEST_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x04U,
        0x00U, 0x03U, 0x13U, 0x88U, 0x97U, 0x38U
    };
    static const uint8_t reply[VFD_MODBUS_NORMAL_REPLY_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x4AU, 0x08U
    };
    static const uint8_t exception[VFD_MODBUS_EXCEPTION_REPLY_SIZE] = {
        0x01U, 0x90U, 0x08U, 0x4DU, 0xC6U
    };
    uint8_t frame[VFD_MODBUS_REQUEST_SIZE];
    uint8_t exception_code;

    CHECK(VfdCodec_Crc16((const uint8_t *)"123456789", 9U) == 0x4B37U);
    CHECK(VfdCodec_BuildWriteCommand(1U, VFD_ACTION_RUN_FORWARD,
                                     5000U, frame, sizeof(frame)) == VFD_CODEC_OK);
    CHECK(memcmp(frame, run_request, sizeof(run_request)) == 0);
    CHECK(VfdCodec_BuildWriteCommand(1U, VFD_ACTION_STOP_DECELERATE,
                                     5000U, frame, sizeof(frame)) == VFD_CODEC_OK);
    CHECK(memcmp(frame, stop_request, sizeof(stop_request)) == 0);
    CHECK(VfdCodec_ParseWriteResponse(reply, sizeof(reply), 1U,
                                      &exception_code) == VFD_CODEC_OK);
    CHECK(exception_code == 0U);
    CHECK(VfdCodec_ParseWriteResponse(exception, sizeof(exception), 1U,
                                      &exception_code) == VFD_CODEC_EXCEPTION);
    CHECK(exception_code == 8U);
}

static void TestRequestBitDamage(void)
{
    uint8_t frame[VFD_MODBUS_REQUEST_SIZE];
    uint8_t damaged[VFD_MODBUS_REQUEST_SIZE];
    uint16_t bit;
    uint16_t crc;
    uint16_t stored;

    CHECK(VfdCodec_BuildWriteCommand(1U, VFD_ACTION_RUN_FORWARD,
                                     5000U, frame, sizeof(frame)) == VFD_CODEC_OK);
    for (bit = 0U; bit < 88U; bit++)
    {
        memcpy(damaged, frame, sizeof(damaged));
        damaged[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
        crc = VfdCodec_Crc16(damaged, 11U);
        stored = (uint16_t)((uint16_t)damaged[11] |
                            (uint16_t)((uint16_t)damaged[12] << 8U));
        CHECK(crc != stored);
    }
}

static void TestReplyFailures(void)
{
    static const uint8_t valid[VFD_MODBUS_NORMAL_REPLY_SIZE] = {
        0x01U, 0x10U, 0x20U, 0x00U, 0x00U, 0x02U, 0x4AU, 0x08U
    };
    uint8_t frame[VFD_MODBUS_NORMAL_REPLY_SIZE];
    uint8_t exception_code;
    uint16_t crc;

    memcpy(frame, valid, sizeof(frame));
    frame[6] ^= 1U;
    CHECK(VfdCodec_ParseWriteResponse(frame, sizeof(frame), 1U,
                                      &exception_code) == VFD_CODEC_CRC_ERROR);

    memcpy(frame, valid, sizeof(frame));
    frame[0] = 2U;
    crc = VfdCodec_Crc16(frame, 6U);
    frame[6] = (uint8_t)crc;
    frame[7] = (uint8_t)(crc >> 8U);
    CHECK(VfdCodec_ParseWriteResponse(frame, sizeof(frame), 1U,
                                      &exception_code) == VFD_CODEC_WRONG_ADDRESS);

    memcpy(frame, valid, sizeof(frame));
    frame[1] = 0x06U;
    crc = VfdCodec_Crc16(frame, 6U);
    frame[6] = (uint8_t)crc;
    frame[7] = (uint8_t)(crc >> 8U);
    CHECK(VfdCodec_ParseWriteResponse(frame, sizeof(frame), 1U,
                                      &exception_code) == VFD_CODEC_WRONG_FUNCTION);

    memcpy(frame, valid, sizeof(frame));
    frame[3] = 1U;
    crc = VfdCodec_Crc16(frame, 6U);
    frame[6] = (uint8_t)crc;
    frame[7] = (uint8_t)(crc >> 8U);
    CHECK(VfdCodec_ParseWriteResponse(frame, sizeof(frame), 1U,
                                      &exception_code) == VFD_CODEC_WRONG_ECHO);
    CHECK(VfdCodec_ParseWriteResponse(valid, 7U, 1U,
                                      &exception_code) == VFD_CODEC_INVALID_LENGTH);
}

static void TestGuards(void)
{
    uint8_t frame[VFD_MODBUS_REQUEST_SIZE];
    uint8_t exception_code;

    CHECK(VfdCodec_Crc16(NULL, 1U) == 0U);
    CHECK(VfdCodec_Crc16(NULL, 0U) == 0xFFFFU);
    CHECK(VfdCodec_BuildWriteCommand(1U, VFD_ACTION_RUN_FORWARD,
                                     1000U, NULL, sizeof(frame)) ==
          VFD_CODEC_NULL_POINTER);
    CHECK(VfdCodec_BuildWriteCommand(0U, VFD_ACTION_RUN_FORWARD,
                                     1000U, frame, sizeof(frame)) ==
          VFD_CODEC_INVALID_PARAMETER);
    CHECK(VfdCodec_BuildWriteCommand(1U, 2U, 1000U, frame, sizeof(frame)) ==
          VFD_CODEC_INVALID_PARAMETER);
    CHECK(VfdCodec_BuildWriteCommand(1U, VFD_ACTION_RUN_FORWARD,
                                     (uint16_t)(MASTER_MAX_FREQUENCY_X100 + 1U),
                                     frame, sizeof(frame)) ==
          VFD_CODEC_INVALID_PARAMETER);
    CHECK(VfdCodec_BuildWriteCommand(1U, VFD_ACTION_RUN_FORWARD,
                                     1000U, frame,
                                     VFD_MODBUS_REQUEST_SIZE - 1U) ==
          VFD_CODEC_INVALID_LENGTH);
    CHECK(VfdCodec_ParseWriteResponse(NULL, 8U, 1U, &exception_code) ==
          VFD_CODEC_NULL_POINTER);
    CHECK(VfdCodec_ParseWriteResponse(frame, 8U, 1U, NULL) ==
          VFD_CODEC_NULL_POINTER);
}

int main(void)
{
    TestKnownVectors();
    TestRequestBitDamage();
    TestReplyFailures();
    TestGuards();
    printf("vfd_modbus_codec: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
