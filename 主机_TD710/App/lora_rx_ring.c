#include "lora_rx_ring.h"

#include <stddef.h>

#define LORA_RX_RING_INDEX_MASK    (LORA_RX_RING_SIZE - 1U)

void LoRaRxRing_Init(LoRaRxRing *ring)
{
    if (ring != NULL)
    {
        ring->head = 0U;
        ring->tail = 0U;
        ring->overflow_count = 0U;
    }
}

uint8_t LoRaRxRing_PushFromIsr(LoRaRxRing *ring, uint8_t byte)
{
    uint16_t head;
    uint16_t next_head;

    if (ring == NULL)
    {
        return 0U;
    }

    head = ring->head;
    next_head = (uint16_t)((head + 1U) & LORA_RX_RING_INDEX_MASK);
    if (next_head == ring->tail)
    {
        ring->overflow_count++;
        return 0U;
    }

    ring->data[head] = byte;
    ring->head = next_head;
    return 1U;
}

uint8_t LoRaRxRing_Pop(LoRaRxRing *ring, uint8_t *byte)
{
    uint16_t tail;

    if ((ring == NULL) || (byte == NULL))
    {
        return 0U;
    }

    tail = ring->tail;
    if (tail == ring->head)
    {
        return 0U;
    }

    *byte = ring->data[tail];
    ring->tail = (uint16_t)((tail + 1U) & LORA_RX_RING_INDEX_MASK);
    return 1U;
}

uint16_t LoRaRxRing_Count(const LoRaRxRing *ring)
{
    uint16_t head;
    uint16_t tail;

    if (ring == NULL)
    {
        return 0U;
    }

    head = ring->head;
    tail = ring->tail;
    return (uint16_t)(((uint32_t)head + LORA_RX_RING_SIZE - (uint32_t)tail) &
                      LORA_RX_RING_INDEX_MASK);
}
