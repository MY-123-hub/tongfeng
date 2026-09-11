#ifndef SLAVE_PROTOCOL_RUNTIME_H
#define SLAVE_PROTOCOL_RUNTIME_H

#include <stdint.h>

#define SLAVE_TELEMETRY_POINT_COUNT          (36U)
#define SLAVE_TEMPERATURE_INVALID_X10        (-32767 - 1)
#define SLAVE_HUMIDITY_INVALID_X10           (0xFFFFU)
#define SLAVE_PRESSURE_INVALID_PA            (0xFFFFFFFFUL)
#define SLAVE_RAIN_VALUE_UNAVAILABLE         (0xFFFFU)

typedef struct
{
    int16_t node_temperature_x10[SLAVE_TELEMETRY_POINT_COUNT];
    uint16_t node_humidity_x10[SLAVE_TELEMETRY_POINT_COUNT];
    int16_t slave_bme_temperature_x10;
    uint16_t slave_bme_humidity_x10;
    uint32_t slave_bme_pressure_pa;
    uint16_t rain_value;
    uint32_t sample_tick;
} SlaveTelemetrySnapshot;

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
void SlaveRuntime_UpdateSnapshot(const SlaveTelemetrySnapshot *snapshot);
void SlaveRuntime_CompleteSample(uint16_t flow_id,
                                 const SlaveTelemetrySnapshot *snapshot);

#endif /* SLAVE_PROTOCOL_RUNTIME_H */
