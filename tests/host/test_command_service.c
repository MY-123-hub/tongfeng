#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_service.h"

static uint32_t g_checks;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        g_checks++;                                                             \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

static LoRaMessage MakeCommand(uint8_t type, uint16_t flow_id, uint16_t value)
{
    LoRaMessage message;

    memset(&message, 0, sizeof(message));
    message.type = type;
    message.flow_id = flow_id;
    if ((type == LORA_MSG_SET_FREQ) || (type == LORA_MSG_SET_TARGET_TEMP))
    {
        message.payload_length = 2U;
        message.payload[0] = (uint8_t)(value & 0x00FFU);
        message.payload[1] = (uint8_t)(value >> 8U);
    }
    return message;
}

static void TestValidation(void)
{
    LoRaMessage message;

    message = MakeCommand(LORA_MSG_SET_FREQ, 1U, 0U);
    CHECK(CommandService_ValidateParameters(&message) == 1U);
    message = MakeCommand(LORA_MSG_SET_FREQ, 1U, MASTER_MAX_FREQUENCY_X100);
    CHECK(CommandService_ValidateParameters(&message) == 1U);
    message = MakeCommand(LORA_MSG_SET_FREQ, 1U,
                          (uint16_t)(MASTER_MAX_FREQUENCY_X100 + 1U));
    CHECK(CommandService_ValidateParameters(&message) == 0U);

    message = MakeCommand(LORA_MSG_SET_TARGET_TEMP, 2U,
                          (uint16_t)(int16_t)MASTER_MIN_TARGET_TEMP_X10);
    CHECK(CommandService_ValidateParameters(&message) == 1U);
    message = MakeCommand(LORA_MSG_SET_TARGET_TEMP, 2U,
                          (uint16_t)(int16_t)MASTER_MAX_TARGET_TEMP_X10);
    CHECK(CommandService_ValidateParameters(&message) == 1U);
    message = MakeCommand(LORA_MSG_SET_TARGET_TEMP, 2U,
                          (uint16_t)(int16_t)(MASTER_MIN_TARGET_TEMP_X10 - 1));
    CHECK(CommandService_ValidateParameters(&message) == 0U);
    message = MakeCommand(LORA_MSG_SET_TARGET_TEMP, 2U,
                          (uint16_t)(int16_t)(MASTER_MAX_TARGET_TEMP_X10 + 1));
    CHECK(CommandService_ValidateParameters(&message) == 0U);

    message = MakeCommand(LORA_MSG_MANUAL_STOP, 3U, 0U);
    CHECK(CommandService_ValidateParameters(&message) == 1U);
    message.type = LORA_MSG_READ_TEMP;
    CHECK(CommandService_ValidateParameters(&message) == 0U);
    CHECK(CommandService_ValidateParameters(NULL) == 0U);
}

static void TestPendingDuplicateConflictAndBusy(void)
{
    CommandService service;
    LoRaMessage first;
    LoRaMessage duplicate;
    LoRaMessage conflict;
    LoRaMessage other;
    uint8_t result[7] = {0U, 2U, 0U, 0xB8U, 0x0BU, 0x04U, 0x01U};
    uint8_t copied[7];

    CommandService_Init(&service);
    first = MakeCommand(LORA_MSG_MANUAL_STOP, 100U, 0U);
    duplicate = first;
    conflict = MakeCommand(LORA_MSG_MANUAL_RUN, 100U, 0U);
    other = MakeCommand(LORA_MSG_SET_FREQ, 101U, 3000U);

    CHECK(CommandService_Classify(&service, &first) == COMMAND_CLASS_NEW);
    CHECK(CommandService_Begin(&service, &first) == 1U);
    CHECK(CommandService_Classify(&service, &duplicate) ==
          COMMAND_CLASS_DUPLICATE_PENDING);
    CHECK(CommandService_Classify(&service, &conflict) ==
          COMMAND_CLASS_FLOW_CONFLICT);
    CHECK(CommandService_Classify(&service, &other) == COMMAND_CLASS_BUSY);
    CHECK(CommandService_Begin(&service, &other) == 0U);

    CHECK(CommandService_Complete(&service, result) == 1U);
    CHECK(service.pending.valid == 0U);
    CHECK(CommandService_Classify(&service, &duplicate) ==
          COMMAND_CLASS_DUPLICATE_COMPLETED);
    CHECK(CommandService_Classify(&service, &conflict) ==
          COMMAND_CLASS_FLOW_CONFLICT);
    memset(copied, 0, sizeof(copied));
    CHECK(CommandService_GetCompletedResult(&service, &duplicate, copied) == 1U);
    CHECK(memcmp(result, copied, sizeof(result)) == 0);
    CHECK(CommandService_GetCompletedResult(&service, &conflict, copied) == 0U);
}

static void TestPayloadFingerprintAndHistoryRollover(void)
{
    CommandService service;
    LoRaMessage message;
    LoRaMessage conflict;
    uint8_t result[7];
    uint8_t copied[7];
    uint16_t flow;

    CommandService_Init(&service);
    for (flow = 1U; flow <= 5U; flow++)
    {
        message = MakeCommand(LORA_MSG_SET_FREQ, flow,
                              (uint16_t)(1000U + flow));
        memset(result, (int)flow, sizeof(result));
        CHECK(CommandService_Classify(&service, &message) == COMMAND_CLASS_NEW);
        CHECK(CommandService_Begin(&service, &message) == 1U);
        CHECK(CommandService_Complete(&service, result) == 1U);
    }

    message = MakeCommand(LORA_MSG_SET_FREQ, 1U, 1001U);
    CHECK(CommandService_Classify(&service, &message) == COMMAND_CLASS_NEW);
    CHECK(CommandService_GetCompletedResult(&service, &message, copied) == 0U);
    message = MakeCommand(LORA_MSG_SET_FREQ, 5U, 1005U);
    CHECK(CommandService_Classify(&service, &message) ==
          COMMAND_CLASS_DUPLICATE_COMPLETED);
    CHECK(CommandService_GetCompletedResult(&service, &message, copied) == 1U);
    CHECK(copied[0] == 5U);

    conflict = message;
    conflict.payload[0]++;
    CHECK(CommandService_Classify(&service, &conflict) ==
          COMMAND_CLASS_FLOW_CONFLICT);
}

static void TestInvalidAndNull(void)
{
    CommandService service;
    LoRaMessage message;
    uint8_t result[7] = {0};

    CommandService_Init(&service);
    message = MakeCommand(LORA_MSG_READ_TEMP, 1U, 0U);
    CHECK(CommandService_Classify(&service, &message) == COMMAND_CLASS_INVALID);
    message = MakeCommand(LORA_MSG_SET_FREQ, 1U, 1000U);
    message.payload_length = 3U;
    CHECK(CommandService_Classify(&service, &message) == COMMAND_CLASS_INVALID);
    CHECK(CommandService_Classify(NULL, &message) == COMMAND_CLASS_INVALID);
    CHECK(CommandService_Classify(&service, NULL) == COMMAND_CLASS_INVALID);
    CHECK(CommandService_Begin(NULL, &message) == 0U);
    CHECK(CommandService_Complete(&service, result) == 0U);
    CHECK(CommandService_GetCompletedResult(NULL, &message, result) == 0U);
    CommandService_Init(NULL);
}

int main(void)
{
    TestValidation();
    TestPendingDuplicateConflictAndBusy();
    TestPayloadFingerprintAndHistoryRollover();
    TestInvalidAndNull();
    printf("command_service: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
