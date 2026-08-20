#ifndef MASTER_TEMPERATURE_H
#define MASTER_TEMPERATURE_H

#include <stdint.h>

#include "lora_protocol.h"

typedef enum
{
    MASTER_TEMP_IDLE = 0,
    MASTER_TEMP_WAIT_SLAVE,
    MASTER_TEMP_REPLY_PENDING,
    MASTER_TEMP_ERROR_PENDING
} MasterTemperatureState;

typedef enum
{
    MASTER_TEMP_READ_USE_CACHE = 0,
    MASTER_TEMP_READ_REQUEST_SLAVE,
    MASTER_TEMP_READ_ALREADY_PENDING,
    MASTER_TEMP_READ_BUSY
} MasterTemperatureReadDecision;

typedef struct
{
    int16_t cache[LORA_PROTOCOL_TEMP_COUNT];
    uint32_t cache_tick;
    uint32_t request_tick;
    uint16_t pending_flow_id;
    uint8_t cache_valid;
    uint8_t state;
    uint8_t pending_error;
} MasterTemperatureService;

void MasterTemperature_Init(MasterTemperatureService *service);
uint8_t MasterTemperature_IsCacheFresh(const MasterTemperatureService *service,
                                       uint32_t now_ms);
MasterTemperatureReadDecision MasterTemperature_EvaluateRead(
    const MasterTemperatureService *service,
    uint16_t flow_id,
    uint8_t read_mode,
    uint32_t now_ms);
void MasterTemperature_BeginCacheReply(MasterTemperatureService *service,
                                       uint16_t flow_id);
void MasterTemperature_BeginSlaveRequest(MasterTemperatureService *service,
                                         uint16_t flow_id,
                                         uint32_t now_ms);
uint8_t MasterTemperature_AcceptSlaveData(MasterTemperatureService *service,
                                         uint16_t flow_id,
                                         const int16_t *temperatures,
                                         uint32_t now_ms);
uint8_t MasterTemperature_CheckTimeout(MasterTemperatureService *service,
                                       uint32_t now_ms,
                                       uint8_t timeout_error);
uint8_t MasterTemperature_GetPendingReply(const MasterTemperatureService *service,
                                          uint16_t *flow_id,
                                          int16_t *temperatures);
uint8_t MasterTemperature_GetPendingError(const MasterTemperatureService *service,
                                          uint16_t *flow_id,
                                          uint8_t *error_code);
void MasterTemperature_CompletePending(MasterTemperatureService *service);

#endif /* MASTER_TEMPERATURE_H */
