#ifndef TEST_FAKE_QUEUE_H
#define TEST_FAKE_QUEUE_H

#include <stdint.h>

#include "FreeRTOS.h"

typedef struct
{
    uint8_t *storage;
    UBaseType_t length;
    UBaseType_t item_size;
    UBaseType_t head;
    UBaseType_t tail;
    UBaseType_t count;
} StaticQueue_t;

typedef StaticQueue_t *QueueHandle_t;

QueueHandle_t xQueueCreateStatic(UBaseType_t length,
                                 UBaseType_t item_size,
                                 uint8_t *storage,
                                 StaticQueue_t *control);
BaseType_t xQueueSendToBack(QueueHandle_t queue,
                            const void *item,
                            TickType_t wait_ticks);
BaseType_t xQueueSendToFront(QueueHandle_t queue,
                             const void *item,
                             TickType_t wait_ticks);
BaseType_t xQueueReceive(QueueHandle_t queue,
                         void *item,
                         TickType_t wait_ticks);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueuePeek(QueueHandle_t queue,
                      void *item,
                      TickType_t wait_ticks);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);

#endif /* TEST_FAKE_QUEUE_H */
