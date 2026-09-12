#include "DGUS.h"

#include <string.h>

static DGUSReceivedWrite g_writes[5];
static uint8_t g_write_count;
static uint8_t g_write_index;

static void DGUS_ClearRx(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memset(Rx1Buffer, 0, sizeof(Rx1Buffer));
    rx1_pointer = 0U;
    rx1_frame_ready = 0U;
    rx1_overflow = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t DGUS_IsScreenFlashControlAddress(uint16_t address)
{
    return ((address == 0x5030U) || (address == 0x5040U) ||
            (address == 0x5050U) || (address == 0x5060U)) ? 1U : 0U;
}

uint8_t DGUS_WriteWords(uint16_t address, const uint16_t *words, uint8_t count)
{
    uint8_t frame[3U + 3U + 2U * DGUS_MAX_WRITE_WORDS];
    uint8_t index;
    uint8_t length;

    if ((words == NULL) || (count == 0U) || (count > DGUS_MAX_WRITE_WORDS) ||
        (DGUS_IsScreenFlashControlAddress(address) != 0U))
    {
        return 0U;
    }
    length = (uint8_t)(3U + 2U * count);
    frame[0] = DGUS_FRAME_HEAD_0;
    frame[1] = DGUS_FRAME_HEAD_1;
    frame[2] = length;
    frame[3] = DGUS_CMD_WRITE_VP;
    frame[4] = (uint8_t)(address >> 8U);
    frame[5] = (uint8_t)address;
    for (index = 0U; index < count; index++)
    {
        frame[6U + 2U * index] = (uint8_t)(words[index] >> 8U);
        frame[7U + 2U * index] = (uint8_t)words[index];
    }
    /*
     * A DGUS packet is one UART byte stream.  Transmit the whole packet with
     * the real USART1 handle, rather than issuing one HAL call per byte via a
     * copied UART handle.  The local frame remains valid until this blocking
     * transfer has completed.
     */
    return (HAL_UART_Transmit(&huart1, frame, (uint16_t)(length + 3U), 20U) == HAL_OK) ? 1U : 0U;
}

uint8_t DGUS_WriteSingleData(uint16_t address, uint16_t value)
{
    return DGUS_WriteWords(address, &value, 1U);
}

uint8_t DGUS_WriteAscii(uint16_t address, const uint8_t *ascii, uint8_t byte_count)
{
    uint16_t words[DGUS_MAX_WRITE_WORDS];
    uint8_t word_count;
    uint8_t index;

    if ((ascii == NULL) || (byte_count == 0U) ||
        ((byte_count & 1U) != 0U) ||
        (byte_count > (uint8_t)(DGUS_MAX_WRITE_WORDS * 2U)))
    {
        return 0U;
    }
    word_count = (uint8_t)(byte_count / 2U);
    for (index = 0U; index < word_count; index++)
    {
        words[index] = (uint16_t)((uint16_t)ascii[index * 2U] << 8U) |
                       (uint16_t)ascii[index * 2U + 1U];
    }
    return DGUS_WriteWords(address, words, word_count);
}

uint8_t DGUS_ReadWords(uint16_t address, uint8_t count)
{
    uint8_t frame[7];

    if ((count == 0U) || (count > 5U))
    {
        return 0U;
    }
    frame[0] = DGUS_FRAME_HEAD_0;
    frame[1] = DGUS_FRAME_HEAD_1;
    frame[2] = 0x04U;
    frame[3] = DGUS_CMD_READ_VP_RESPONSE;
    frame[4] = (uint8_t)(address >> 8U);
    frame[5] = (uint8_t)address;
    frame[6] = count;
    return (HAL_UART_Transmit(&huart1, frame, (uint16_t)sizeof(frame), 20U) == HAL_OK) ? 1U : 0U;
}

void DGUS_ProcessRx(void)
{
    uint16_t frame_length;
    uint16_t payload_length;

    if (rx1_overflow != 0U)
    {
        DGUS_ClearRx();
        return;
    }
    if (rx1_pointer < 3U)
    {
        return;
    }
    payload_length = Rx1Buffer[2];
    frame_length = (uint16_t)(payload_length + 3U);
    if ((frame_length > UART_RX_BUFFER_SIZE) || (payload_length < 6U))
    {
        DGUS_ClearRx();
        return;
    }
    if (rx1_pointer < frame_length)
    {
        return;
    }
    if ((Rx1Buffer[0] == DGUS_FRAME_HEAD_0) &&
        (Rx1Buffer[1] == DGUS_FRAME_HEAD_1) &&
        (Rx1Buffer[3] == DGUS_CMD_READ_VP_RESPONSE) &&
        (Rx1Buffer[6] >= 1U) && (Rx1Buffer[6] <= 5U) &&
        (payload_length == (uint16_t)(4U + 2U * Rx1Buffer[6])))
    {
        uint8_t index;
        uint8_t words = Rx1Buffer[6];
        g_write_count = words;
        g_write_index = 0U;
        for (index = 0U; index < words; index++)
        {
            g_writes[index].address = (uint16_t)((uint16_t)(((uint16_t)Rx1Buffer[4] << 8U) |
                                                             Rx1Buffer[5]) + (uint16_t)index);
            g_writes[index].value = (uint16_t)(((uint16_t)Rx1Buffer[7U + 2U * index] << 8U) |
                                                Rx1Buffer[8U + 2U * index]);
        }
    }
    DGUS_ClearRx();
}

uint8_t DGUS_TakeReceivedWrite(DGUSReceivedWrite *write)
{
    uint32_t primask;

    if (write == NULL)
    {
        return 0U;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if (g_write_index >= g_write_count)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }
    *write = g_writes[g_write_index];
    g_write_index++;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return 1U;
}
