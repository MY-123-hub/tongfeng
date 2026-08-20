#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "master_config.h"
#include "master_identity.h"
#include "master_ingress.h"
#include "master_temperature.h"

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

static LoRaMessage MakeMessage(uint8_t source_role,
                               uint8_t source_group,
                               uint8_t destination_group)
{
    LoRaMessage message;

    memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION;
    message.source_role = source_role;
    message.source_group = source_group;
    message.destination_role = LORA_ROLE_MASTER;
    message.destination_group = destination_group;
    return message;
}

static void TestIdentityAndIngress(void)
{
    LoRaMessage message;
    uint8_t group;

    MasterIdentity_Init(0U);
    CHECK(MasterIdentity_IsValid() == 0U);
    CHECK(MasterIdentity_GetGroup() == 0U);
    CHECK(MasterIdentity_GetRawGroup() == 0U);
    MasterIdentity_Init(5U);
    CHECK(MasterIdentity_IsValid() == 0U);
    CHECK(MasterIdentity_GetRawGroup() == 5U);

    for (group = MASTER_GROUP_MIN; group <= MASTER_GROUP_MAX; group++)
    {
        MasterIdentity_Init(group);
        CHECK(MasterIdentity_IsValid() == 1U);
        CHECK(MasterIdentity_GetGroup() == group);

        message = MakeMessage(LORA_ROLE_CONTROL_ROOM, 0U, group);
        CHECK(MasterIngress_Route(&message, group) == MASTER_INGRESS_CONTROL_ROOM);
        message.destination_group = (group == MASTER_GROUP_MAX) ? 1U : (uint8_t)(group + 1U);
        CHECK(MasterIngress_Route(&message, group) == MASTER_INGRESS_DROP);

        message = MakeMessage(LORA_ROLE_SLAVE, group, group);
        CHECK(MasterIngress_Route(&message, group) == MASTER_INGRESS_SLAVE);
        message.source_group = (group == MASTER_GROUP_MAX) ? 1U : (uint8_t)(group + 1U);
        CHECK(MasterIngress_Route(&message, group) == MASTER_INGRESS_DROP);
    }

    message = MakeMessage(LORA_ROLE_CONTROL_ROOM, 0U, 1U);
    message.destination_role = LORA_ROLE_SLAVE;
    CHECK(MasterIngress_Route(&message, 1U) == MASTER_INGRESS_DROP);
    CHECK(MasterIngress_Route(&message, 0U) == MASTER_INGRESS_DROP);
    CHECK(MasterIngress_Route(NULL, 1U) == MASTER_INGRESS_DROP);
}

static void SeedCache(MasterTemperatureService *service,
                      uint32_t tick)
{
    uint32_t i;

    service->cache_valid = 1U;
    service->cache_tick = tick;
    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        service->cache[i] = (int16_t)(250 + (int16_t)i);
    }
}

static void TestCacheDecisions(void)
{
    MasterTemperatureService service;

    MasterTemperature_Init(&service);
    CHECK(service.state == MASTER_TEMP_IDLE);
    CHECK(MasterTemperature_IsCacheFresh(&service, 0U) == 0U);
    CHECK(MasterTemperature_EvaluateRead(&service, 100U, 0U, 0U) ==
          MASTER_TEMP_READ_REQUEST_SLAVE);

    SeedCache(&service, 1000U);
    CHECK(MasterTemperature_IsCacheFresh(&service, 6000U) == 1U);
    CHECK(MasterTemperature_IsCacheFresh(&service, 6001U) == 0U);
    CHECK(MasterTemperature_EvaluateRead(&service, 100U, 0U, 6000U) ==
          MASTER_TEMP_READ_USE_CACHE);
    CHECK(MasterTemperature_EvaluateRead(&service, 100U, 1U, 6000U) ==
          MASTER_TEMP_READ_REQUEST_SLAVE);

    MasterTemperature_BeginCacheReply(&service, 100U);
    CHECK(service.state == MASTER_TEMP_REPLY_PENDING);
    CHECK(MasterTemperature_EvaluateRead(&service, 100U, 0U, 6000U) ==
          MASTER_TEMP_READ_ALREADY_PENDING);
    CHECK(MasterTemperature_EvaluateRead(&service, 101U, 0U, 6000U) ==
          MASTER_TEMP_READ_BUSY);
    MasterTemperature_CompletePending(&service);
    CHECK(service.state == MASTER_TEMP_IDLE);

    service.cache_tick = 0xFFFFFF00UL;
    CHECK(MasterTemperature_IsCacheFresh(&service,
          (uint32_t)(0xFFFFFF00UL + MASTER_TEMP_CACHE_FRESH_MS)) == 1U);
}

static void TestSlaveReplyAndDeepCopy(void)
{
    MasterTemperatureService service;
    int16_t input[LORA_PROTOCOL_TEMP_COUNT];
    int16_t output[LORA_PROTOCOL_TEMP_COUNT];
    uint16_t flow_id = 0U;
    uint32_t i;

    MasterTemperature_Init(&service);
    for (i = 0U; i < LORA_PROTOCOL_TEMP_COUNT; i++)
    {
        input[i] = (int16_t)(-55 + (int16_t)i);
    }
    input[7] = 0;

    MasterTemperature_BeginSlaveRequest(&service, 100U, 500U);
    CHECK(service.state == MASTER_TEMP_WAIT_SLAVE);
    CHECK(MasterTemperature_EvaluateRead(&service, 100U, 0U, 500U) ==
          MASTER_TEMP_READ_ALREADY_PENDING);
    CHECK(MasterTemperature_AcceptSlaveData(&service, 99U, input, 600U) == 0U);
    CHECK(service.cache_valid == 0U);
    CHECK(MasterTemperature_AcceptSlaveData(&service, 100U, input, 600U) == 1U);
    CHECK(service.state == MASTER_TEMP_REPLY_PENDING);
    CHECK(service.cache_valid == 1U);
    input[0] = 999;
    memset(output, 0, sizeof(output));
    CHECK(MasterTemperature_GetPendingReply(&service, &flow_id, output) == 1U);
    CHECK(flow_id == 100U);
    CHECK(output[0] == -55);
    CHECK(output[7] == 0);
    CHECK(output[35] == -20);
    MasterTemperature_CompletePending(&service);
    CHECK(service.state == MASTER_TEMP_IDLE);
}

static void TestTimeoutLateFrameAndWrap(void)
{
    MasterTemperatureService service;
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT] = {0};
    uint16_t flow_id;
    uint8_t error_code;
    uint32_t start;

    MasterTemperature_Init(&service);
    MasterTemperature_BeginSlaveRequest(&service, 200U, 1000U);
    CHECK(MasterTemperature_CheckTimeout(&service, 3999U, 4U) == 0U);
    CHECK(MasterTemperature_CheckTimeout(&service, 4000U, 4U) == 1U);
    CHECK(MasterTemperature_GetPendingError(&service, &flow_id, &error_code) == 1U);
    CHECK(flow_id == 200U);
    CHECK(error_code == 4U);
    CHECK(MasterTemperature_AcceptSlaveData(&service, 200U,
                                             temperatures, 4001U) == 0U);
    MasterTemperature_CompletePending(&service);
    CHECK(MasterTemperature_AcceptSlaveData(&service, 200U,
                                             temperatures, 4002U) == 0U);

    start = 0xFFFFFF00UL;
    MasterTemperature_BeginSlaveRequest(&service, 201U, start);
    CHECK(MasterTemperature_CheckTimeout(
              &service,
              (uint32_t)(start + MASTER_SLAVE_RESPONSE_TIMEOUT_MS - 1U),
              4U) == 0U);
    CHECK(MasterTemperature_CheckTimeout(
              &service,
              (uint32_t)(start + MASTER_SLAVE_RESPONSE_TIMEOUT_MS),
              4U) == 1U);
}

static void TestNullGuards(void)
{
    MasterTemperatureService service;
    int16_t temperatures[LORA_PROTOCOL_TEMP_COUNT] = {0};
    uint16_t flow_id;
    uint8_t error_code;

    MasterTemperature_Init(NULL);
    MasterTemperature_Init(&service);
    CHECK(MasterTemperature_IsCacheFresh(NULL, 0U) == 0U);
    CHECK(MasterTemperature_EvaluateRead(NULL, 1U, 0U, 0U) ==
          MASTER_TEMP_READ_BUSY);
    CHECK(MasterTemperature_AcceptSlaveData(NULL, 1U, temperatures, 0U) == 0U);
    CHECK(MasterTemperature_AcceptSlaveData(&service, 1U, NULL, 0U) == 0U);
    CHECK(MasterTemperature_CheckTimeout(NULL, 0U, 4U) == 0U);
    CHECK(MasterTemperature_GetPendingReply(NULL, &flow_id, temperatures) == 0U);
    CHECK(MasterTemperature_GetPendingReply(&service, NULL, temperatures) == 0U);
    CHECK(MasterTemperature_GetPendingError(NULL, &flow_id, &error_code) == 0U);
    MasterTemperature_CompletePending(NULL);
}

int main(void)
{
    TestIdentityAndIngress();
    TestCacheDecisions();
    TestSlaveReplyAndDeepCopy();
    TestTimeoutLateFrameAndWrap();
    TestNullGuards();
    printf("master_temperature: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
