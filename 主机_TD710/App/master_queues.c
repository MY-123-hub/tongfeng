#include "master_queues.h"

#include <string.h>

MasterQueueDiagnostics MasterQueueDiag;

static StaticQueue_t g_event_queue_control;
static StaticQueue_t g_lora_tx_queue_control;
static StaticQueue_t g_vfd_job_queue_control;
static StaticQueue_t g_ui_queue_control;

static uint8_t g_event_queue_storage[MASTER_EVENT_QUEUE_DEPTH * sizeof(MasterEvent)];
static uint8_t g_lora_tx_queue_storage[MASTER_LORA_TX_QUEUE_DEPTH * sizeof(LoRaMessage)];
static uint8_t g_vfd_job_queue_storage[MASTER_VFD_JOB_QUEUE_DEPTH * sizeof(VfdJob)];
static uint8_t g_ui_queue_storage[MASTER_UI_QUEUE_DEPTH * sizeof(MasterUiSnapshot)];

static QueueHandle_t g_event_queue;
static QueueHandle_t g_lora_tx_queue;
static QueueHandle_t g_vfd_job_queue;
static QueueHandle_t g_ui_queue;

uint8_t MasterQueues_Init(void)
{
    memset(&MasterQueueDiag, 0, sizeof(MasterQueueDiag));

    g_event_queue = xQueueCreateStatic(MASTER_EVENT_QUEUE_DEPTH,
                                       sizeof(MasterEvent),
                                       g_event_queue_storage,
                                       &g_event_queue_control);
    g_lora_tx_queue = xQueueCreateStatic(MASTER_LORA_TX_QUEUE_DEPTH,
                                         sizeof(LoRaMessage),
                                         g_lora_tx_queue_storage,
                                         &g_lora_tx_queue_control);
    g_vfd_job_queue = xQueueCreateStatic(MASTER_VFD_JOB_QUEUE_DEPTH,
                                         sizeof(VfdJob),
                                         g_vfd_job_queue_storage,
                                         &g_vfd_job_queue_control);
    g_ui_queue = xQueueCreateStatic(MASTER_UI_QUEUE_DEPTH,
                                    sizeof(MasterUiSnapshot),
                                    g_ui_queue_storage,
                                    &g_ui_queue_control);

    if ((g_event_queue == NULL) || (g_lora_tx_queue == NULL) ||
        (g_vfd_job_queue == NULL) || (g_ui_queue == NULL))
    {
        return 0U;
    }

    return 1U;
}

BaseType_t MasterQueues_SendEvent(const MasterEvent *event, TickType_t wait_ticks)
{
    BaseType_t result;

    if ((event == NULL) || (g_event_queue == NULL))
    {
        return pdFAIL;
    }

    result = xQueueSendToBack(g_event_queue, event, wait_ticks);
    if (result != pdPASS)
    {
        MasterQueueDiag.event_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_ReceiveEvent(MasterEvent *event, TickType_t wait_ticks)
{
    if ((event == NULL) || (g_event_queue == NULL))
    {
        return pdFAIL;
    }
    return xQueueReceive(g_event_queue, event, wait_ticks);
}

BaseType_t MasterQueues_SendLoRa(const LoRaMessage *message, TickType_t wait_ticks)
{
    BaseType_t result;

    if ((message == NULL) || (g_lora_tx_queue == NULL))
    {
        return pdFAIL;
    }

    result = xQueueSendToBack(g_lora_tx_queue, message, wait_ticks);
    if (result != pdPASS)
    {
        MasterQueueDiag.lora_tx_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_ReceiveLoRa(LoRaMessage *message, TickType_t wait_ticks)
{
    if ((message == NULL) || (g_lora_tx_queue == NULL))
    {
        return pdFAIL;
    }
    return xQueueReceive(g_lora_tx_queue, message, wait_ticks);
}

BaseType_t MasterQueues_SendVfdJob(const VfdJob *job, TickType_t wait_ticks)
{
    BaseType_t result;

    if ((job == NULL) || (g_vfd_job_queue == NULL))
    {
        return pdFAIL;
    }

    result = xQueueSendToBack(g_vfd_job_queue, job, wait_ticks);
    if (result != pdPASS)
    {
        MasterQueueDiag.vfd_job_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_SendVfdJobFront(const VfdJob *job, TickType_t wait_ticks)
{
    BaseType_t result;

    if ((job == NULL) || (g_vfd_job_queue == NULL))
    {
        return pdFAIL;
    }

    result = xQueueSendToFront(g_vfd_job_queue, job, wait_ticks);
    if (result != pdPASS)
    {
        MasterQueueDiag.vfd_job_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_SendEmergencyVfdJob(const VfdJob *job)
{
    VfdJob discarded;
    BaseType_t result;

    if ((job == NULL) || (g_vfd_job_queue == NULL))
    {
        return pdFAIL;
    }

    result = xQueueSendToFront(g_vfd_job_queue, job, 0U);
    if (result == pdPASS)
    {
        return pdPASS;
    }

    /* 调用前已提升control_epoch，队列中旧任务均已失效。
       满队列时丢弃一条旧任务，为安全停机保留队首位置。 */
    if (xQueueReceive(g_vfd_job_queue, &discarded, 0U) == pdPASS)
    {
        MasterQueueDiag.vfd_job_evicted_count++;
    }
    result = xQueueSendToFront(g_vfd_job_queue, job, 0U);
    if (result != pdPASS)
    {
        MasterQueueDiag.vfd_job_full_count++;
    }
    return result;
}

BaseType_t MasterQueues_ReceiveVfdJob(VfdJob *job, TickType_t wait_ticks)
{
    if ((job == NULL) || (g_vfd_job_queue == NULL))
    {
        return pdFAIL;
    }
    return xQueueReceive(g_vfd_job_queue, job, wait_ticks);
}

BaseType_t MasterQueues_OverwriteUi(const MasterUiSnapshot *snapshot)
{
    BaseType_t result;

    if ((snapshot == NULL) || (g_ui_queue == NULL))
    {
        return pdFAIL;
    }

    result = xQueueOverwrite(g_ui_queue, snapshot);
    if (result != pdPASS)
    {
        MasterQueueDiag.ui_write_failure_count++;
    }
    return result;
}

BaseType_t MasterQueues_PeekUi(MasterUiSnapshot *snapshot)
{
    if ((snapshot == NULL) || (g_ui_queue == NULL))
    {
        return pdFAIL;
    }
    return xQueuePeek(g_ui_queue, snapshot, 0U);
}

UBaseType_t MasterQueues_EventWaiting(void)
{
    return (g_event_queue != NULL) ? uxQueueMessagesWaiting(g_event_queue) : 0U;
}

UBaseType_t MasterQueues_LoRaWaiting(void)
{
    return (g_lora_tx_queue != NULL) ? uxQueueMessagesWaiting(g_lora_tx_queue) : 0U;
}

UBaseType_t MasterQueues_VfdWaiting(void)
{
    return (g_vfd_job_queue != NULL) ? uxQueueMessagesWaiting(g_vfd_job_queue) : 0U;
}
