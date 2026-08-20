#include "lora_rx_ring.h"

#include <stdint.h>
#include <stdio.h>

static unsigned int g_check_count;
static unsigned int g_failure_count;

#define CHECK(condition)                                                         \
    do                                                                           \
    {                                                                            \
        g_check_count++;                                                         \
        if (!(condition))                                                        \
        {                                                                        \
            g_failure_count++;                                                   \
            (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                        \
    } while (0)

static void TestInitialState(void)
{
    LoRaRxRing ring;
    uint8_t byte = 0U;

    LoRaRxRing_Init(&ring);
    CHECK(ring.head == 0U);
    CHECK(ring.tail == 0U);
    CHECK(ring.overflow_count == 0U);
    CHECK(LoRaRxRing_Count(&ring) == 0U);
    CHECK(LoRaRxRing_Pop(&ring, &byte) == 0U);
}

static void TestCapacityAndOrder(void)
{
    LoRaRxRing ring;
    uint16_t index;
    uint8_t byte = 0U;

    LoRaRxRing_Init(&ring);
    for (index = 0U; index < LORA_RX_RING_CAPACITY; index++)
    {
        CHECK(LoRaRxRing_PushFromIsr(&ring, (uint8_t)index) == 1U);
    }

    CHECK(LoRaRxRing_Count(&ring) == LORA_RX_RING_CAPACITY);
    CHECK(LoRaRxRing_PushFromIsr(&ring, 0xEEU) == 0U);
    CHECK(ring.overflow_count == 1U);
    CHECK(LoRaRxRing_Count(&ring) == LORA_RX_RING_CAPACITY);

    for (index = 0U; index < LORA_RX_RING_CAPACITY; index++)
    {
        CHECK(LoRaRxRing_Pop(&ring, &byte) == 1U);
        CHECK(byte == (uint8_t)index);
    }

    CHECK(LoRaRxRing_Count(&ring) == 0U);
    CHECK(LoRaRxRing_Pop(&ring, &byte) == 0U);
}

static void TestWrapAround(void)
{
    LoRaRxRing ring;
    uint32_t index;
    uint8_t byte = 0U;

    LoRaRxRing_Init(&ring);
    for (index = 0U; index < 10000U; index++)
    {
        CHECK(LoRaRxRing_PushFromIsr(&ring, (uint8_t)index) == 1U);
        CHECK(LoRaRxRing_Pop(&ring, &byte) == 1U);
        CHECK(byte == (uint8_t)index);
        CHECK(LoRaRxRing_Count(&ring) == 0U);
    }

    CHECK(ring.overflow_count == 0U);
}

static void TestInterleavedBursts(void)
{
    LoRaRxRing ring;
    uint16_t round;
    uint16_t index;
    uint8_t byte = 0U;

    LoRaRxRing_Init(&ring);
    for (round = 0U; round < 100U; round++)
    {
        for (index = 0U; index < 200U; index++)
        {
            CHECK(LoRaRxRing_PushFromIsr(&ring, (uint8_t)(round + index)) == 1U);
        }
        CHECK(LoRaRxRing_Count(&ring) == 200U);

        for (index = 0U; index < 200U; index++)
        {
            CHECK(LoRaRxRing_Pop(&ring, &byte) == 1U);
            CHECK(byte == (uint8_t)(round + index));
        }
        CHECK(LoRaRxRing_Count(&ring) == 0U);
    }

    CHECK(ring.overflow_count == 0U);
}

static void TestInvalidArguments(void)
{
    LoRaRxRing ring;
    uint8_t byte;

    LoRaRxRing_Init(NULL);
    LoRaRxRing_Init(&ring);
    CHECK(LoRaRxRing_PushFromIsr(NULL, 0U) == 0U);
    CHECK(LoRaRxRing_Pop(NULL, &byte) == 0U);
    CHECK(LoRaRxRing_Pop(&ring, NULL) == 0U);
    CHECK(LoRaRxRing_Count(NULL) == 0U);
}

int main(void)
{
    TestInitialState();
    TestCapacityAndOrder();
    TestWrapAround();
    TestInterleavedBursts();
    TestInvalidArguments();

    if (g_failure_count == 0U)
    {
        (void)printf("PASS: %u checks\n", g_check_count);
        return 0;
    }

    (void)printf("FAIL: %u of %u checks failed\n",
                 g_failure_count,
                 g_check_count);
    return 1;
}

