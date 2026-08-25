#ifndef TEST_FAKE_GATEWAY_RUNTIME_H
#define TEST_FAKE_GATEWAY_RUNTIME_H

#include <stdint.h>

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

#endif /* TEST_FAKE_GATEWAY_RUNTIME_H */
