#include "vfd_modbus_codec.h"

#include <stddef.h>

#include "master_config.h"
#include "master_messages.h"

#define VFD_FUNCTION_WRITE_MULTIPLE     (0x10U)
#define VFD_REGISTER_ACTION_HIGH        (0x20U)
#define VFD_REGISTER_ACTION_LOW         (0x00U)

uint16_t VfdCodec_Crc16(const uint8_t *data, uint16_t length)
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

VfdCodecStatus VfdCodec_BuildWriteCommand(uint8_t slave_address,
                                          uint16_t action,
                                          uint16_t frequency_x100,
                                          uint8_t *frame,
                                          uint16_t frame_capacity)
{
    uint16_t crc;

    if (frame == NULL)
    {
        return VFD_CODEC_NULL_POINTER;
    }
    if ((slave_address == 0U) ||
        ((action != VFD_ACTION_RUN_FORWARD) &&
         (action != VFD_ACTION_STOP_DECELERATE)) ||
        (frequency_x100 > MASTER_MAX_FREQUENCY_X100))
    {
        return VFD_CODEC_INVALID_PARAMETER;
    }
    if (frame_capacity < VFD_MODBUS_REQUEST_SIZE)
    {
        return VFD_CODEC_INVALID_LENGTH;
    }

    frame[0] = slave_address;
    frame[1] = VFD_FUNCTION_WRITE_MULTIPLE;
    frame[2] = VFD_REGISTER_ACTION_HIGH;
    frame[3] = VFD_REGISTER_ACTION_LOW;
    frame[4] = 0x00U;
    frame[5] = 0x02U;
    frame[6] = 0x04U;
    frame[7] = (uint8_t)(action >> 8U);
    frame[8] = (uint8_t)(action & 0x00FFU);
    frame[9] = (uint8_t)(frequency_x100 >> 8U);
    frame[10] = (uint8_t)(frequency_x100 & 0x00FFU);
    crc = VfdCodec_Crc16(frame, 11U);
    frame[11] = (uint8_t)(crc & 0x00FFU);
    frame[12] = (uint8_t)(crc >> 8U);
    return VFD_CODEC_OK;
}

VfdCodecStatus VfdCodec_ParseWriteResponse(const uint8_t *frame,
                                           uint16_t frame_length,
                                           uint8_t expected_slave_address,
                                           uint8_t *exception_code)
{
    uint16_t received_crc;
    uint16_t calculated_crc;

    if ((frame == NULL) || (exception_code == NULL))
    {
        return VFD_CODEC_NULL_POINTER;
    }
    *exception_code = 0U;
    if ((frame_length != VFD_MODBUS_NORMAL_REPLY_SIZE) &&
        (frame_length != VFD_MODBUS_EXCEPTION_REPLY_SIZE))
    {
        return VFD_CODEC_INVALID_LENGTH;
    }

    received_crc = (uint16_t)((uint16_t)frame[frame_length - 2U] |
                              (uint16_t)((uint16_t)frame[frame_length - 1U] << 8U));
    calculated_crc = VfdCodec_Crc16(frame, (uint16_t)(frame_length - 2U));
    if (received_crc != calculated_crc)
    {
        return VFD_CODEC_CRC_ERROR;
    }
    if (frame[0] != expected_slave_address)
    {
        return VFD_CODEC_WRONG_ADDRESS;
    }

    if ((frame[1] & 0x80U) != 0U)
    {
        if ((frame_length != VFD_MODBUS_EXCEPTION_REPLY_SIZE) ||
            (frame[1] != (uint8_t)(VFD_FUNCTION_WRITE_MULTIPLE | 0x80U)))
        {
            return VFD_CODEC_WRONG_FUNCTION;
        }
        *exception_code = frame[2];
        return VFD_CODEC_EXCEPTION;
    }

    if ((frame_length != VFD_MODBUS_NORMAL_REPLY_SIZE) ||
        (frame[1] != VFD_FUNCTION_WRITE_MULTIPLE))
    {
        return VFD_CODEC_WRONG_FUNCTION;
    }
    if ((frame[2] != VFD_REGISTER_ACTION_HIGH) ||
        (frame[3] != VFD_REGISTER_ACTION_LOW) ||
        (frame[4] != 0x00U) || (frame[5] != 0x02U))
    {
        return VFD_CODEC_WRONG_ECHO;
    }
    return VFD_CODEC_OK;
}
