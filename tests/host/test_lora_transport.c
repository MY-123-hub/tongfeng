#include "lora_transport.h"

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

int main(void)
{
    uint16_t index;
    uint8_t byte = 0U;

    LoraTransport_Init();
    CHECK(LoraTransport_IsApplicationMode() == 0U);
    CHECK(LoraTransport_PushRxFromIsr(0x11U) == 0U);
    CHECK(LoraTransport_PopRx(&byte) == 0U);
    CHECK(LoraTransport_GetDataLossCount() == 0U);

    LoraTransport_EnableApplicationMode();
    CHECK(LoraTransport_IsApplicationMode() == 1U);

    for (index = 0U; index < 255U; index++)
    {
        CHECK(LoraTransport_PushRxFromIsr((uint8_t)index) == 1U);
    }
    CHECK(LoraTransport_PushRxFromIsr(0xEEU) == 0U);
    CHECK(LoraTransport_GetDataLossCount() == 1U);

    for (index = 0U; index < 255U; index++)
    {
        CHECK(LoraTransport_PopRx(&byte) == 1U);
        CHECK(byte == (uint8_t)index);
    }
    CHECK(LoraTransport_PopRx(&byte) == 0U);

    LoraTransport_ReportRxErrorFromIsr();
    CHECK(LoraTransport_GetDataLossCount() == 2U);
    CHECK(LoraTransport_PopRx(NULL) == 0U);

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

