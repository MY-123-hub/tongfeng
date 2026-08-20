#include "command_service.h"

#include <stddef.h>
#include <string.h>

static uint8_t CommandService_MakeFingerprint(const LoRaMessage *message,
                                              CommandFingerprint *fingerprint)
{
    if ((message == NULL) || (fingerprint == NULL) ||
        (message->payload_length > COMMAND_FINGERPRINT_PAYLOAD_MAX) ||
        (CommandService_IsControlType(message->type) == 0U))
    {
        return 0U;
    }

    memset(fingerprint, 0, sizeof(*fingerprint));
    fingerprint->flow_id = message->flow_id;
    fingerprint->type = message->type;
    fingerprint->payload_length = message->payload_length;
    if (message->payload_length != 0U)
    {
        memcpy(fingerprint->payload,
               message->payload,
               message->payload_length);
    }
    return 1U;
}

static uint8_t CommandService_FingerprintEqual(const CommandFingerprint *left,
                                               const CommandFingerprint *right)
{
    if ((left->flow_id != right->flow_id) ||
        (left->type != right->type) ||
        (left->payload_length != right->payload_length))
    {
        return 0U;
    }
    return (memcmp(left->payload,
                   right->payload,
                   left->payload_length) == 0) ? 1U : 0U;
}

void CommandService_Init(CommandService *service)
{
    if (service != NULL)
    {
        memset(service, 0, sizeof(*service));
    }
}

uint8_t CommandService_IsControlType(uint8_t message_type)
{
    return ((message_type >= LORA_MSG_SET_FREQ) &&
            (message_type <= LORA_MSG_QUERY_STATUS)) ? 1U : 0U;
}

uint8_t CommandService_ValidateParameters(const LoRaMessage *message)
{
    uint16_t raw;
    int16_t target;

    if ((message == NULL) ||
        (CommandService_IsControlType(message->type) == 0U))
    {
        return 0U;
    }

    if (message->type == LORA_MSG_SET_FREQ)
    {
        raw = (uint16_t)((uint16_t)message->payload[0] |
                         (uint16_t)((uint16_t)message->payload[1] << 8U));
        return (raw <= MASTER_MAX_FREQUENCY_X100) ? 1U : 0U;
    }

    if (message->type == LORA_MSG_SET_TARGET_TEMP)
    {
        raw = (uint16_t)((uint16_t)message->payload[0] |
                         (uint16_t)((uint16_t)message->payload[1] << 8U));
        target = (int16_t)raw;
        return ((target >= MASTER_MIN_TARGET_TEMP_X10) &&
                (target <= MASTER_MAX_TARGET_TEMP_X10)) ? 1U : 0U;
    }

    return 1U;
}

CommandClassification CommandService_Classify(const CommandService *service,
                                              const LoRaMessage *message)
{
    CommandFingerprint incoming;
    uint32_t i;

    if ((service == NULL) ||
        (CommandService_MakeFingerprint(message, &incoming) == 0U))
    {
        return COMMAND_CLASS_INVALID;
    }

    if ((service->pending.valid != 0U) &&
        (service->pending.fingerprint.flow_id == incoming.flow_id))
    {
        return (CommandService_FingerprintEqual(&service->pending.fingerprint,
                                                &incoming) != 0U) ?
               COMMAND_CLASS_DUPLICATE_PENDING : COMMAND_CLASS_FLOW_CONFLICT;
    }

    for (i = 0U; i < MASTER_COMMAND_HISTORY_DEPTH; i++)
    {
        if ((service->history[i].valid != 0U) &&
            (service->history[i].fingerprint.flow_id == incoming.flow_id))
        {
            return (CommandService_FingerprintEqual(&service->history[i].fingerprint,
                                                    &incoming) != 0U) ?
                   COMMAND_CLASS_DUPLICATE_COMPLETED : COMMAND_CLASS_FLOW_CONFLICT;
        }
    }

    return (service->pending.valid != 0U) ? COMMAND_CLASS_BUSY : COMMAND_CLASS_NEW;
}

uint8_t CommandService_Begin(CommandService *service,
                             const LoRaMessage *message)
{
    CommandFingerprint incoming;

    if ((service == NULL) || (service->pending.valid != 0U) ||
        (CommandService_MakeFingerprint(message, &incoming) == 0U))
    {
        return 0U;
    }

    service->pending.valid = 1U;
    service->pending.fingerprint = incoming;
    return 1U;
}

uint8_t CommandService_Complete(CommandService *service,
                                const uint8_t *result_payload)
{
    CompletedCommand *entry;

    if ((service == NULL) || (result_payload == NULL) ||
        (service->pending.valid == 0U))
    {
        return 0U;
    }

    entry = &service->history[service->next_history_index];
    memset(entry, 0, sizeof(*entry));
    entry->valid = 1U;
    entry->fingerprint = service->pending.fingerprint;
    memcpy(entry->result_payload, result_payload, sizeof(entry->result_payload));
    service->next_history_index = (uint8_t)(
        (service->next_history_index + 1U) % MASTER_COMMAND_HISTORY_DEPTH);
    memset(&service->pending, 0, sizeof(service->pending));
    return 1U;
}

uint8_t CommandService_GetCompletedResult(const CommandService *service,
                                          const LoRaMessage *message,
                                          uint8_t *result_payload)
{
    CommandFingerprint incoming;
    uint32_t i;

    if ((service == NULL) || (result_payload == NULL) ||
        (CommandService_MakeFingerprint(message, &incoming) == 0U))
    {
        return 0U;
    }

    for (i = 0U; i < MASTER_COMMAND_HISTORY_DEPTH; i++)
    {
        if ((service->history[i].valid != 0U) &&
            (CommandService_FingerprintEqual(&service->history[i].fingerprint,
                                             &incoming) != 0U))
        {
            memcpy(result_payload,
                   service->history[i].result_payload,
                   sizeof(service->history[i].result_payload));
            return 1U;
        }
    }
    return 0U;
}
