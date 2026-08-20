#ifndef GATEWAY_RUNTIME_H
#define GATEWAY_RUNTIME_H

#include <stdint.h>

/* 控制室串口与上位机使用与 LoRa 完全相同的二进制帧。 */
typedef enum
{
    GATEWAY_OUTPUT_LORA = 0,
    GATEWAY_OUTPUT_PC = 1
} GatewayOutputPort;

typedef void (*GatewaySendCallback)(GatewayOutputPort port,
                                    const uint8_t *frame,
                                    uint16_t frame_length,
                                    void *context);

void GatewayRuntime_Init(GatewaySendCallback send_callback, void *context);
void GatewayRuntime_PushPcByteFromIsr(uint8_t byte);
void GatewayRuntime_PushLoRaByteFromIsr(uint8_t byte);
void GatewayRuntime_Process(uint32_t now_ms);

#endif /* GATEWAY_RUNTIME_H */
