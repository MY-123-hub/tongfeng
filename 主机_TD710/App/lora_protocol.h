#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include <stdint.h>

#define LORA_PROTOCOL_HEADER_1            (0xAAU)
#define LORA_PROTOCOL_HEADER_2            (0x55U)
#define LORA_PROTOCOL_VERSION             (0x01U)
#define LORA_PROTOCOL_MAX_PAYLOAD_SIZE    (96U)
#define LORA_PROTOCOL_MIN_FRAME_SIZE      (13U)
#define LORA_PROTOCOL_MAX_FRAME_SIZE      (109U)
#define LORA_PROTOCOL_TEMP_COUNT          (36U)
#define LORA_PROTOCOL_TEMP_PAYLOAD_SIZE   (72U)

typedef enum
{
    LORA_ROLE_CONTROL_ROOM = 0x01,
    LORA_ROLE_MASTER       = 0x02,
    LORA_ROLE_SLAVE        = 0x03
} LoRaRole;

typedef enum
{
    LORA_MSG_READ_TEMP       = 0x01,
    LORA_MSG_TEMP_36         = 0x02,
    LORA_MSG_SET_FREQ        = 0x10,
    LORA_MSG_SET_TARGET_TEMP = 0x11,
    LORA_MSG_MANUAL_RUN      = 0x12,
    LORA_MSG_MANUAL_STOP     = 0x13,
    LORA_MSG_SET_AUTO        = 0x14,
    LORA_MSG_QUERY_STATUS    = 0x15,
    LORA_MSG_ACK             = 0x20,
    LORA_MSG_RESULT          = 0x21,
    LORA_MSG_ERROR           = 0x7E
} LoRaMessageType;

typedef enum
{
    LORA_PROTOCOL_OK = 0,
    LORA_PROTOCOL_NULL_POINTER,
    LORA_PROTOCOL_FRAME_TOO_SHORT,
    LORA_PROTOCOL_INVALID_HEADER,
    LORA_PROTOCOL_UNSUPPORTED_VERSION,
    LORA_PROTOCOL_INVALID_ROLE,
    LORA_PROTOCOL_INVALID_GROUP,
    LORA_PROTOCOL_INVALID_DIRECTION,
    LORA_PROTOCOL_UNSUPPORTED_TYPE,
    LORA_PROTOCOL_INVALID_PAYLOAD_LENGTH,
    LORA_PROTOCOL_INVALID_PAYLOAD_VALUE,
    LORA_PROTOCOL_FRAME_LENGTH_MISMATCH,
    LORA_PROTOCOL_CRC_MISMATCH,
    LORA_PROTOCOL_OUTPUT_TOO_SMALL
} LoRaProtocolStatus;

typedef struct
{
    uint8_t version;
    uint8_t type;
    uint8_t source_role;
    uint8_t source_group;
    uint8_t destination_role;
    uint8_t destination_group;
    uint16_t flow_id;
    uint8_t payload_length;
    uint8_t payload[LORA_PROTOCOL_MAX_PAYLOAD_SIZE];
} LoRaMessage;

/**
 ******************************************************************************
  @功能：计算 CRC-16/MODBUS 校验值
  @日期：2026-08-19
  @参数：[输入] data - 待校验数据
         [输入] length - 数据字节数
  @返回值：uint16_t - CRC 数值；data 为空且 length 非零时返回 0
  @使用说明：返回值写入报文时低字节在前
 ******************************************************************************
 */
uint16_t LoRaProtocol_Crc16(const uint8_t *data, uint16_t length);

/**
 ******************************************************************************
  @功能：校验一条结构化 LoRa 消息的版本、地址、方向和数据长度
  @日期：2026-08-19
  @参数：[输入] message - 待校验消息
  @返回值：LoRaProtocolStatus - 校验结果
  @使用说明：本函数不计算帧 CRC，只校验结构化字段
 ******************************************************************************
 */
LoRaProtocolStatus LoRaProtocol_ValidateMessage(const LoRaMessage *message);

/**
 ******************************************************************************
  @功能：把结构化消息编码为完整 LoRa 二进制帧
  @日期：2026-08-19
  @参数：[输入] message - 待编码消息
         [输出] output - 输出帧缓冲区
         [输入] output_size - 输出缓冲区容量
         [输出] output_length - 实际输出字节数
  @返回值：LoRaProtocolStatus - 编码结果
  @使用说明：输出缓冲区在发送完成前由调用者持有
 ******************************************************************************
 */
LoRaProtocolStatus LoRaProtocol_Encode(const LoRaMessage *message,
                                       uint8_t *output,
                                       uint16_t output_size,
                                       uint16_t *output_length);

/**
 ******************************************************************************
  @功能：校验并解析一条边界完整的 LoRa 二进制帧
  @日期：2026-08-19
  @参数：[输入] frame - 完整帧首地址
         [输入] frame_length - 完整帧字节数
         [输出] message - 解析后的结构化消息
  @返回值：LoRaProtocolStatus - 解码结果
  @使用说明：拆包和粘包由上层流式解析器处理，本函数要求传入恰好一帧
 ******************************************************************************
 */
LoRaProtocolStatus LoRaProtocol_Decode(const uint8_t *frame,
                                       uint16_t frame_length,
                                       LoRaMessage *message);

#endif /* LORA_PROTOCOL_H */
