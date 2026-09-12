#ifndef DGUS_H
#define DGUS_H

#include <stdint.h>

#include "usart.h"

#define DGUS_FRAME_HEAD_0                     (0x5AU)
#define DGUS_FRAME_HEAD_1                     (0xA5U)
#define DGUS_CMD_WRITE_VP                      (0x82U)
#define DGUS_CMD_READ_VP_RESPONSE              (0x83U)
#define DGUS_MAX_WRITE_WORDS                    (8U)

typedef enum
{
    DGUS_VP_MASTER_TEMPERATURE = 0x5000U,
    DGUS_VP_MASTER_HUMIDITY = 0x5001U,
    DGUS_VP_AVERAGE_TEMPERATURE = 0x5002U,
    DGUS_VP_AVERAGE_HUMIDITY = 0x5003U,
    DGUS_VP_TARGET_TEMPERATURE = 0x5013U,
    DGUS_VP_TARGET_HUMIDITY = 0x5014U,
    DGUS_VP_SLAVE_PRESSURE = 0x5071U,
    DGUS_VP_MASTER_PRESSURE = 0x5072U,
    DGUS_VP_FAN_ANIMATION = 0x5073U,
    DGUS_VP_INDICATOR_ANIMATION = 0x5075U,
    DGUS_VP_UPTIME = 0x6050U,
    DGUS_VP_PLAN_START = 0x6070U,
    DGUS_VP_PLAN_END = 0x6090U
} DGUSVpAddress;

typedef struct
{
    uint16_t address;
    uint16_t value;
} DGUSReceivedWrite;

uint8_t DGUS_WriteSingleData(uint16_t address, uint16_t value);
uint8_t DGUS_WriteWords(uint16_t address, const uint16_t *words, uint8_t count);
uint8_t DGUS_WriteAscii(uint16_t address, const uint8_t *ascii, uint8_t byte_count);
uint8_t DGUS_ReadWords(uint16_t address, uint8_t count);
uint8_t DGUS_IsScreenFlashControlAddress(uint16_t address);
void DGUS_ProcessRx(void);
uint8_t DGUS_TakeReceivedWrite(DGUSReceivedWrite *write);

#endif /* DGUS_H */
