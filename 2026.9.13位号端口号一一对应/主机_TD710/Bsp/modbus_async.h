#ifndef MODBUS_ASYNC_H
#define MODBUS_ASYNC_H

#include <stdint.h>

#include "master_messages.h"

#define VFD_MODBUS_RESPONSE_TIMEOUT_MS   (200UL)
#define VFD_MODBUS_TX_TIMEOUT_MS         (50UL)
#define VFD_MODBUS_FRAME_GAP_MS          (5UL)
#define VFD_MODBUS_RX_CAPACITY           (16U)
#define VFD_MODBUS_MAX_ATTEMPTS          (2U)

typedef enum
{
    VFD_MODBUS_IDLE = 0,
    VFD_MODBUS_TX_ACTIVE,
    VFD_MODBUS_WAIT_RESPONSE,
    VFD_MODBUS_RESULT_PENDING
} VfdModbusState;

typedef enum
{
    VFD_MODBUS_START_ACCEPTED = 0,
    VFD_MODBUS_START_BUSY,
    VFD_MODBUS_START_INVALID
} VfdModbusStartStatus;

typedef struct
{
    uint32_t start_count;
    uint32_t busy_count;
    uint32_t retry_count;
    uint32_t success_count;
    uint32_t timeout_count;
    uint32_t response_error_count;
    uint32_t uart_error_count;
    uint32_t rx_overflow_count;
    uint32_t stray_byte_count;
    uint8_t state;
} VfdModbusDiagnostics;

extern VfdModbusDiagnostics VfdModbusDiag;

void VfdModbus_Init(void);
VfdModbusStartStatus VfdModbus_Start(const VfdJob *job, uint32_t now_ms);
void VfdModbus_Process(uint32_t now_ms, uint32_t current_epoch);
void VfdModbus_OnRxByteFromIsr(uint8_t byte, uint32_t now_ms);
void VfdModbus_OnTxCompleteFromIsr(uint32_t now_ms);
void VfdModbus_OnUartErrorFromIsr(uint32_t hal_error);
uint8_t VfdModbus_IsIdle(void);
uint8_t VfdModbus_PeekResult(VfdResult *result);
void VfdModbus_AcknowledgeResult(void);

#endif /* MODBUS_ASYNC_H */
