#ifndef GATEWAY_RUNTIME_H
#define GATEWAY_RUNTIME_H

#include <stdint.h>

/*
 * 控制室上位机口与 LoRa 口采用同一份应用层报文。
 * 回调只会在 GatewayRuntime_Process() 的任务上下文中调用，绝不在串口中断内调用。
 */
typedef enum
{
    GATEWAY_OUTPUT_LORA = 0,
    GATEWAY_OUTPUT_PC = 1
} GatewayOutputPort;

typedef uint8_t (*GatewaySendCallback)(GatewayOutputPort port,
                                       const uint8_t *frame,
                                       uint16_t frame_length,
                                       void *context);

void GatewayRuntime_Init(GatewaySendCallback send_callback, void *context);
void GatewayRuntime_PushPcByteFromIsr(uint8_t byte);
void GatewayRuntime_PushLoRaByteFromIsr(uint8_t byte);
void GatewayRuntime_Process(uint32_t now_ms);

#endif /* GATEWAY_RUNTIME_H */
