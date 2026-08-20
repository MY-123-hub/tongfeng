#include "master_temperature.h"

#include <string.h>

#include "master_config.h"

void MasterTemperature_Init(MasterTemperatureService *service)
{
    if (service != NULL)
    {
        memset(service, 0, sizeof(*service));
        service->state = MASTER_TEMP_IDLE;
    }
}

uint8_t MasterTemperature_IsCacheFresh(const MasterTemperatureService *service,
                                       uint32_t now_ms)
{
    if ((service == NULL) || (service->cache_valid == 0U))
    {
        return 0U;
    }
    return ((uint32_t)(now_ms - service->cache_tick) <=
            MASTER_TEMP_CACHE_FRESH_MS) ? 1U : 0U;
}

MasterTemperatureReadDecision MasterTemperature_EvaluateRead(
    const MasterTemperatureService *service,
    uint16_t flow_id,
    uint8_t read_mode,
    uint32_t now_ms)
{
    if (service == NULL)
    {
        return MASTER_TEMP_READ_BUSY;
    }

    if (service->state != MASTER_TEMP_IDLE)
    {
        return (service->pending_flow_id == flow_id) ?
               MASTER_TEMP_READ_ALREADY_PENDING : MASTER_TEMP_READ_BUSY;
    }

    if ((read_mode == 0U) &&
        (MasterTemperature_IsCacheFresh(service, now_ms) != 0U))
    {
        return MASTER_TEMP_READ_USE_CACHE;
    }
    return MASTER_TEMP_READ_REQUEST_SLAVE;
}

void MasterTemperature_BeginCacheReply(MasterTemperatureService *service,
                                       uint16_t flow_id)
{
    if ((service != NULL) && (service->state == MASTER_TEMP_IDLE) &&
        (service->cache_valid != 0U))
    {
        service->pending_flow_id = flow_id;
        service->state = MASTER_TEMP_REPLY_PENDING;
    }
}

void MasterTemperature_BeginSlaveRequest(MasterTemperatureService *service,
                                         uint16_t flow_id,
                                         uint32_t now_ms)
{
    if ((service != NULL) && (service->state == MASTER_TEMP_IDLE))
    {
        service->pending_flow_id = flow_id;
        service->request_tick = now_ms;
        service->pending_error = 0U;
        service->state = MASTER_TEMP_WAIT_SLAVE;
    }
}

uint8_t MasterTemperature_AcceptSlaveData(MasterTemperatureService *service,
                                         uint16_t flow_id,
                                         const int16_t *temperatures,
                                         uint32_t now_ms)
{
    if ((service == NULL) || (temperatures == NULL) ||
        (service->state != MASTER_TEMP_WAIT_SLAVE) ||
        (service->pending_flow_id != flow_id))
    {
        return 0U;
    }

    memcpy(service->cache, temperatures, sizeof(service->cache));
    service->cache_tick = now_ms;
    service->cache_valid = 1U;
    service->state = MASTER_TEMP_REPLY_PENDING;
    return 1U;
}

uint8_t MasterTemperature_CheckTimeout(MasterTemperatureService *service,
                                       uint32_t now_ms,
                                       uint8_t timeout_error)
{
    if ((service == NULL) || (service->state != MASTER_TEMP_WAIT_SLAVE))
    {
        return 0U;
    }

    if ((uint32_t)(now_ms - service->request_tick) <
        MASTER_SLAVE_RESPONSE_TIMEOUT_MS)
    {
        return 0U;
    }

    service->pending_error = timeout_error;
    service->state = MASTER_TEMP_ERROR_PENDING;
    return 1U;
}

uint8_t MasterTemperature_GetPendingReply(const MasterTemperatureService *service,
                                          uint16_t *flow_id,
                                          int16_t *temperatures)
{
    if ((service == NULL) || (flow_id == NULL) || (temperatures == NULL) ||
        (service->state != MASTER_TEMP_REPLY_PENDING))
    {
        return 0U;
    }

    *flow_id = service->pending_flow_id;
    memcpy(temperatures, service->cache, sizeof(service->cache));
    return 1U;
}

uint8_t MasterTemperature_GetPendingError(const MasterTemperatureService *service,
                                          uint16_t *flow_id,
                                          uint8_t *error_code)
{
    if ((service == NULL) || (flow_id == NULL) || (error_code == NULL) ||
        (service->state != MASTER_TEMP_ERROR_PENDING))
    {
        return 0U;
    }

    *flow_id = service->pending_flow_id;
    *error_code = service->pending_error;
    return 1U;
}

void MasterTemperature_CompletePending(MasterTemperatureService *service)
{
    if ((service != NULL) &&
        ((service->state == MASTER_TEMP_REPLY_PENDING) ||
         (service->state == MASTER_TEMP_ERROR_PENDING)))
    {
        service->pending_flow_id = 0U;
        service->pending_error = 0U;
        service->state = MASTER_TEMP_IDLE;
    }
}
