#ifndef SLAVE_PROTOCOL_RUNTIME_H
#define SLAVE_PROTOCOL_RUNTIME_H

#include <stdint.h>

typedef struct
{
    volatile uint32_t rx_overflow_count;
    volatile uint32_t invalid_frame_count;
    volatile uint32_t ignored_message_count;
    volatile uint32_t duplicate_request_count;
    volatile uint32_t tx_failure_count;
} SlaveRuntimeDiagnostics;

extern SlaveRuntimeDiagnostics SlaveRuntimeDiag;

/* USART2中断只写入字节；解析、采样请求和发送均在主循环完成。 */
void SlaveRuntime_Init(uint8_t local_group);
uint8_t SlaveRuntime_IsApplicationMode(void);
void SlaveRuntime_PushRxByteFromIsr(uint8_t byte);
void SlaveRuntime_Process(uint32_t now_ms);
uint8_t SlaveRuntime_TakeSampleRequest(uint16_t *flow_id);
void SlaveRuntime_CompleteSample(uint16_t flow_id, const int16_t temperatures[36]);

#endif /* SLAVE_PROTOCOL_RUNTIME_H */
