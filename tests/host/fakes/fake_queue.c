#include "queue.h"

#include <string.h>

QueueHandle_t xQueueCreateStatic(UBaseType_t length,
                                 UBaseType_t item_size,
                                 uint8_t *storage,
                                 StaticQueue_t *control)
{
    if ((length == 0U) || (item_size == 0U) ||
        (storage == NULL) || (control == NULL))
    {
        return NULL;
    }

    control->storage = storage;
    control->length = length;
    control->item_size = item_size;
    control->head = 0U;
    control->tail = 0U;
    control->count = 0U;
    return control;
}

BaseType_t xQueueSendToBack(QueueHandle_t queue,
                            const void *item,
                            TickType_t wait_ticks)
{
    (void)wait_ticks;
    if ((queue == NULL) || (item == NULL) || (queue->count >= queue->length))
    {
        return pdFAIL;
    }

    memcpy(&queue->storage[queue->tail * queue->item_size],
           item,
           queue->item_size);
    queue->tail = (queue->tail + 1U) % queue->length;
    queue->count++;
    return pdPASS;
}

BaseType_t xQueueSendToFront(QueueHandle_t queue,
                             const void *item,
                             TickType_t wait_ticks)
{
    (void)wait_ticks;
    if ((queue == NULL) || (item == NULL) || (queue->count >= queue->length))
    {
        return pdFAIL;
    }

    queue->head = (queue->head == 0U) ? (queue->length - 1U) :
                                       (queue->head - 1U);
    memcpy(&queue->storage[queue->head * queue->item_size],
           item,
           queue->item_size);
    queue->count++;
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *item,
                         TickType_t wait_ticks)
{
    (void)wait_ticks;
    if ((queue == NULL) || (item == NULL) || (queue->count == 0U))
    {
        return pdFAIL;
    }

    memcpy(item,
           &queue->storage[queue->head * queue->item_size],
           queue->item_size);
    queue->head = (queue->head + 1U) % queue->length;
    queue->count--;
    return pdPASS;
}

BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    if ((queue == NULL) || (item == NULL) || (queue->length != 1U))
    {
        return pdFAIL;
    }

    memcpy(queue->storage, item, queue->item_size);
    queue->head = 0U;
    queue->tail = 0U;
    queue->count = 1U;
    return pdPASS;
}

BaseType_t xQueuePeek(QueueHandle_t queue,
                      void *item,
                      TickType_t wait_ticks)
{
    (void)wait_ticks;
    if ((queue == NULL) || (item == NULL) || (queue->count == 0U))
    {
        return pdFAIL;
    }

    memcpy(item,
           &queue->storage[queue->head * queue->item_size],
           queue->item_size);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    return (queue != NULL) ? queue->count : 0U;
}
