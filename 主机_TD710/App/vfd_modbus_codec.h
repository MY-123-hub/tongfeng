#ifndef VFD_MODBUS_CODEC_H
#define VFD_MODBUS_CODEC_H

#include <stdint.h>

#define VFD_MODBUS_REQUEST_SIZE          (13U)
#define VFD_MODBUS_NORMAL_REPLY_SIZE     (8U)
#define VFD_MODBUS_EXCEPTION_REPLY_SIZE  (5U)

typedef enum
{
    VFD_CODEC_OK = 0,
    VFD_CODEC_NULL_POINTER,
    VFD_CODEC_INVALID_PARAMETER,
    VFD_CODEC_INVALID_LENGTH,
    VFD_CODEC_CRC_ERROR,
    VFD_CODEC_WRONG_ADDRESS,
    VFD_CODEC_WRONG_FUNCTION,
    VFD_CODEC_WRONG_ECHO,
    VFD_CODEC_EXCEPTION
} VfdCodecStatus;

uint16_t VfdCodec_Crc16(const uint8_t *data, uint16_t length);
VfdCodecStatus VfdCodec_BuildWriteCommand(uint8_t slave_address,
                                          uint16_t action,
                                          uint16_t frequency_x100,
                                          uint8_t *frame,
                                          uint16_t frame_capacity);
VfdCodecStatus VfdCodec_ParseWriteResponse(const uint8_t *frame,
                                           uint16_t frame_length,
                                           uint8_t expected_slave_address,
                                           uint8_t *exception_code);

#endif /* VFD_MODBUS_CODEC_H */
