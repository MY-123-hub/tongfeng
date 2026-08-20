#include "lora_transport.h"

#include "lora_rx_ring.h"

static LoRaRxRing g_lora_rx_ring;
static volatile uint8_t g_application_mode;
static volatile uint32_t g_uart_error_count;

void LoraTransport_Init(void)
{
    LoRaRxRing_Init(&g_lora_rx_ring);
    g_uart_error_count = 0U;
    g_application_mode = 0U;
}

void LoraTransport_EnableApplicationMode(void)
{
    g_application_mode = 1U;
}

uint8_t LoraTransport_IsApplicationMode(void)
{
    return g_application_mode;
}

uint8_t LoraTransport_PushRxFromIsr(uint8_t byte)
{
    if (g_application_mode == 0U)
    {
        return 0U;
    }

    return LoRaRxRing_PushFromIsr(&g_lora_rx_ring, byte);
}

void LoraTransport_ReportRxErrorFromIsr(void)
{
    if (g_application_mode != 0U)
    {
        g_uart_error_count++;
    }
}

uint8_t LoraTransport_PopRx(uint8_t *byte)
{
    return LoRaRxRing_Pop(&g_lora_rx_ring, byte);
}

uint32_t LoraTransport_GetDataLossCount(void)
{
    return g_lora_rx_ring.overflow_count + g_uart_error_count;
}

