#ifndef COMMAND_SERVICE_H
#define COMMAND_SERVICE_H

#include <stdint.h>

#include "lora_protocol.h"
#include "master_config.h"

#define COMMAND_FINGERPRINT_PAYLOAD_MAX   (2U)

typedef enum
{
    COMMAND_CLASS_NEW = 0,
    COMMAND_CLASS_DUPLICATE_PENDING,
    COMMAND_CLASS_DUPLICATE_COMPLETED,
    COMMAND_CLASS_FLOW_CONFLICT,
    COMMAND_CLASS_BUSY,
    COMMAND_CLASS_INVALID
} CommandClassification;

typedef struct
{
    uint16_t flow_id;
    uint8_t type;
    uint8_t payload_length;
    uint8_t payload[COMMAND_FINGERPRINT_PAYLOAD_MAX];
} CommandFingerprint;

typedef struct
{
    uint8_t valid;
    CommandFingerprint fingerprint;
} PendingCommand;

typedef struct
{
    uint8_t valid;
    CommandFingerprint fingerprint;
    uint8_t result_payload[7];
} CompletedCommand;

typedef struct
{
    PendingCommand pending;
    CompletedCommand history[MASTER_COMMAND_HISTORY_DEPTH];
    uint8_t next_history_index;
} CommandService;

void CommandService_Init(CommandService *service);
uint8_t CommandService_IsControlType(uint8_t message_type);
uint8_t CommandService_ValidateParameters(const LoRaMessage *message);
CommandClassification CommandService_Classify(const CommandService *service,
                                              const LoRaMessage *message);
uint8_t CommandService_Begin(CommandService *service,
                             const LoRaMessage *message);
uint8_t CommandService_Complete(CommandService *service,
                                const uint8_t *result_payload);
uint8_t CommandService_GetCompletedResult(const CommandService *service,
                                          const LoRaMessage *message,
                                          uint8_t *result_payload);

#endif /* COMMAND_SERVICE_H */
