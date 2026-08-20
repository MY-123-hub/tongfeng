#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "master_queues.h"

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

static void TestEventQueue(void)
{
    MasterEvent input;
    MasterEvent output;
    uint32_t i;

    memset(&input, 0, sizeof(input));
    input.type = MASTER_EVENT_LORA_MESSAGE;

    for (i = 0U; i < MASTER_EVENT_QUEUE_DEPTH; i++)
    {
        input.data.lora_message.flow_id = (uint16_t)(100U + i);
        CHECK(MasterQueues_SendEvent(&input, 0U) == pdPASS);
    }
    CHECK(MasterQueues_EventWaiting() == MASTER_EVENT_QUEUE_DEPTH);
    CHECK(MasterQueues_SendEvent(&input, 0U) == pdFAIL);
    CHECK(MasterQueueDiag.event_full_count == 1U);

    for (i = 0U; i < MASTER_EVENT_QUEUE_DEPTH; i++)
    {
        memset(&output, 0, sizeof(output));
        CHECK(MasterQueues_ReceiveEvent(&output, 0U) == pdPASS);
        CHECK(output.type == MASTER_EVENT_LORA_MESSAGE);
        CHECK(output.data.lora_message.flow_id == (uint16_t)(100U + i));
    }
    CHECK(MasterQueues_ReceiveEvent(&output, 0U) == pdFAIL);
}

static void TestLoRaAndVfdQueues(void)
{
    LoRaMessage message;
    LoRaMessage received;
    VfdJob job;
    VfdJob received_job;
    uint32_t i;

    memset(&message, 0, sizeof(message));
    message.version = LORA_PROTOCOL_VERSION;
    message.type = LORA_MSG_TEMP_36;
    message.payload_length = LORA_PROTOCOL_TEMP_PAYLOAD_SIZE;
    message.payload[71] = 0xA5U;
    for (i = 0U; i < MASTER_LORA_TX_QUEUE_DEPTH; i++)
    {
        message.flow_id = (uint16_t)i;
        CHECK(MasterQueues_SendLoRa(&message, 0U) == pdPASS);
    }
    CHECK(MasterQueues_SendLoRa(&message, 0U) == pdFAIL);
    CHECK(MasterQueueDiag.lora_tx_full_count == 1U);
    for (i = 0U; i < MASTER_LORA_TX_QUEUE_DEPTH; i++)
    {
        CHECK(MasterQueues_ReceiveLoRa(&received, 0U) == pdPASS);
        CHECK(received.flow_id == (uint16_t)i);
        CHECK(received.payload[71] == 0xA5U);
    }

    memset(&job, 0, sizeof(job));
    for (i = 0U; i < MASTER_VFD_JOB_QUEUE_DEPTH; i++)
    {
        job.epoch = i + 10U;
        CHECK(MasterQueues_SendVfdJob(&job, 0U) == pdPASS);
    }
    CHECK(MasterQueues_SendVfdJob(&job, 0U) == pdFAIL);
    CHECK(MasterQueueDiag.vfd_job_full_count == 1U);
    for (i = 0U; i < MASTER_VFD_JOB_QUEUE_DEPTH; i++)
    {
        CHECK(MasterQueues_ReceiveVfdJob(&received_job, 0U) == pdPASS);
        CHECK(received_job.epoch == i + 10U);
    }
}

static void TestUiOverwriteAndReinit(void)
{
    MasterUiSnapshot input;
    MasterUiSnapshot output;

    memset(&input, 0, sizeof(input));
    CHECK(MasterQueues_PeekUi(&output) == pdFAIL);
    input.frequency_x100 = 3000U;
    CHECK(MasterQueues_OverwriteUi(&input) == pdPASS);
    input.frequency_x100 = 5000U;
    input.temperatures[35] = 285;
    CHECK(MasterQueues_OverwriteUi(&input) == pdPASS);
    memset(&output, 0, sizeof(output));
    CHECK(MasterQueues_PeekUi(&output) == pdPASS);
    CHECK(output.frequency_x100 == 5000U);
    CHECK(output.temperatures[35] == 285);

    CHECK(MasterQueues_Init() == 1U);
    CHECK(MasterQueues_EventWaiting() == 0U);
    CHECK(MasterQueues_LoRaWaiting() == 0U);
    CHECK(MasterQueues_VfdWaiting() == 0U);
    CHECK(MasterQueueDiag.event_full_count == 0U);
}

static void TestEmergencyVfdEvictsOldest(void)
{
    VfdJob job;
    VfdJob output;
    uint32_t i;

    CHECK(MasterQueues_Init() == 1U);
    memset(&job, 0, sizeof(job));
    for (i = 0U; i < MASTER_VFD_JOB_QUEUE_DEPTH; i++)
    {
        job.flow_id = (uint16_t)(10U + i);
        CHECK(MasterQueues_SendVfdJob(&job, 0U) == pdPASS);
    }

    job.flow_id = 99U;
    job.action = VFD_ACTION_STOP_DECELERATE;
    CHECK(MasterQueues_SendEmergencyVfdJob(&job) == pdPASS);
    CHECK(MasterQueueDiag.vfd_job_evicted_count == 1U);
    CHECK(MasterQueues_ReceiveVfdJob(&output, 0U) == pdPASS);
    CHECK(output.flow_id == 99U);
    CHECK(output.action == VFD_ACTION_STOP_DECELERATE);
}

static void TestNullGuards(void)
{
    CHECK(MasterQueues_SendEvent(NULL, 0U) == pdFAIL);
    CHECK(MasterQueues_ReceiveEvent(NULL, 0U) == pdFAIL);
    CHECK(MasterQueues_SendLoRa(NULL, 0U) == pdFAIL);
    CHECK(MasterQueues_ReceiveLoRa(NULL, 0U) == pdFAIL);
    CHECK(MasterQueues_SendVfdJob(NULL, 0U) == pdFAIL);
    CHECK(MasterQueues_SendEmergencyVfdJob(NULL) == pdFAIL);
    CHECK(MasterQueues_ReceiveVfdJob(NULL, 0U) == pdFAIL);
    CHECK(MasterQueues_OverwriteUi(NULL) == pdFAIL);
    CHECK(MasterQueues_PeekUi(NULL) == pdFAIL);
}

int main(void)
{
    CHECK(MASTER_EVENT_QUEUE_DEPTH == 4U);
    CHECK(MASTER_LORA_TX_QUEUE_DEPTH == 4U);
    CHECK(MASTER_VFD_JOB_QUEUE_DEPTH == 3U);
    CHECK(MASTER_UI_QUEUE_DEPTH == 1U);
    CHECK(sizeof(((LoRaMessage *)0)->payload) == LORA_PROTOCOL_MAX_PAYLOAD_SIZE);
    CHECK(MasterQueues_Init() == 1U);

    TestEventQueue();
    TestLoRaAndVfdQueues();
    TestUiOverwriteAndReinit();
    TestEmergencyVfdEvictsOldest();
    TestNullGuards();

    printf("master_queues: %lu checks passed\n", (unsigned long)g_checks);
    return 0;
}
