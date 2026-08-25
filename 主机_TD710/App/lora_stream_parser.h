#ifndef LORA_STREAM_PARSER_H
#define LORA_STREAM_PARSER_H

#include <stdint.h>

#include "lora_protocol.h"

typedef enum
{
    LORA_STREAM_WAITING = 0,
    LORA_STREAM_FRAME_READY,
    LORA_STREAM_FRAME_REJECTED,
    LORA_STREAM_INVALID_ARGUMENT
} LoRaStreamResult;

typedef struct
{
    uint8_t frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t length;
    uint16_t expected_length;
    uint32_t accepted_frame_count;
    uint32_t rejected_frame_count;
    uint32_t discarded_byte_count;
    uint32_t aborted_frame_count;
} LoRaStreamParser;

/**
 ******************************************************************************
  @功能：初始化 LoRa 流式拆包状态机和诊断计数
  @日期：2026-08-19
  @参数：[输出] parser - 状态机对象
  @返回值：无
  @使用说明：parser 应由 LoRa 任务独占，不得在中断和任务间同时调用
 ******************************************************************************
 */
void LoRaStreamParser_Init(LoRaStreamParser *parser);

/**
 ******************************************************************************
  @功能：向拆包状态机输入一个串口字节
  @日期：2026-08-19
  @参数：[输入输出] parser - 状态机对象
         [输入] byte - 新收到的字节
         [输出] message - 完整合法帧的解析结果
  @返回值：LoRaStreamResult - 等待、收到合法帧、拒绝坏帧或参数错误
  @使用说明：只有返回 LORA_STREAM_FRAME_READY 时 message 才是本次新消息
 ******************************************************************************
 */
LoRaStreamResult LoRaStreamParser_PushByte(LoRaStreamParser *parser,
                                           uint8_t byte,
                                           LoRaMessage *message);

/**
 ******************************************************************************
  @功能：因帧间超时或上层复位而放弃当前未完成帧
  @日期：2026-08-19
  @参数：[输入输出] parser - 状态机对象
  @返回值：无
  @使用说明：仅在确认当前帧长期未完成时调用；诊断累计计数不会清零
 ******************************************************************************
 */
void LoRaStreamParser_AbortPartialFrame(LoRaStreamParser *parser);

#endif /* LORA_STREAM_PARSER_H */

