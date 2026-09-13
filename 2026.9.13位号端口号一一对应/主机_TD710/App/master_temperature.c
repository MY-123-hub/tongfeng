#include "master_temperature.h"

#include <string.h>

#include "master_config.h"

#define MASTER_TEMPERATURE_V3_RECORD_SIZE       (3U)
#define MASTER_TEMPERATURE_V3_BME_OFFSET \
    (LORA_PROTOCOL_TEMP_COUNT * MASTER_TEMPERATURE_V3_RECORD_SIZE)

static uint16_t MasterTemperature_ReadU16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8U));
}

uint8_t MasterTemperature_DecodeSlaveV3(const uint8_t *payload,
                                        uint8_t payload_length,
                                        int16_t *temperatures,
                                        uint8_t *positions,
                                        int16_t *slave_bme_temperature_x10)
{
    uint8_t received_index;

    if ((payload == NULL) || (temperatures == NULL) || (positions == NULL) ||
        (slave_bme_temperature_x10 == NULL) ||
        (payload_length != LORA_PROTOCOL_TEMP_PAYLOAD_SIZE))
    {
        return 0U;
    }

    for (received_index = 0U; received_index < LORA_PROTOCOL_TEMP_COUNT;
         received_index++)
    {
        temperatures[received_index] = LORA_PROTOCOL_TEMPERATURE_INVALID;
        positions[received_index] = 0U;
    }

    for (received_index = 0U; received_index < LORA_PROTOCOL_TEMP_COUNT;
         received_index++)
    {
        uint16_t source_offset =
            (uint16_t)received_index * MASTER_TEMPERATURE_V3_RECORD_SIZE;
        uint8_t position = payload[source_offset];
        int16_t temperature =
            (int16_t)MasterTemperature_ReadU16(&payload[source_offset + 1U]);
        uint8_t port = received_index / 6U;
        uint8_t target_index;

        if (position == 0U)
        {
            if (temperature != LORA_PROTOCOL_TEMPERATURE_INVALID)
            {
                return 0U;
            }
            continue;
        }
        if (position > 6U)
        {
            return 0U;
        }

        target_index = (uint8_t)(port * 6U + (position - 1U));
        if (positions[target_index] != 0U)
        {
            return 0U;
        }
        positions[target_index] = position;
        temperatures[target_index] = temperature;
    }

    *slave_bme_temperature_x10 = (int16_t)MasterTemperature_ReadU16(
        &payload[MASTER_TEMPERATURE_V3_BME_OFFSET]);
    return 1U;
}

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
                                         const uint8_t *positions,
                                         uint32_t now_ms)
{
    if ((service == NULL) || (temperatures == NULL) || (positions == NULL) ||
        (service->state != MASTER_TEMP_WAIT_SLAVE) ||
        (service->pending_flow_id != flow_id))
    {
        return 0U;
    }

    memcpy(service->cache, temperatures, sizeof(service->cache));
    memcpy(service->positions, positions, sizeof(service->positions));
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
                                          int16_t *temperatures,
                                          uint8_t *positions)
{
    if ((service == NULL) || (flow_id == NULL) || (temperatures == NULL) ||
        (positions == NULL) ||
        (service->state != MASTER_TEMP_REPLY_PENDING))
    {
        return 0U;
    }

    *flow_id = service->pending_flow_id;
    memcpy(temperatures, service->cache, sizeof(service->cache));
    memcpy(positions, service->positions, sizeof(service->positions));
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
