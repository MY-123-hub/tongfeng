#ifndef MASTER_RUNTIME_H
#define MASTER_RUNTIME_H

#include <stdint.h>

#include "FreeRTOS.h"

typedef struct
{
    uint32_t routed_control_count;
    uint32_t routed_slave_count;
    uint32_t address_drop_count;
    uint32_t read_request_count;
    uint32_t temperature_accept_count;
    uint32_t temperature_reject_count;
    uint32_t temperature_timeout_count;
    uint32_t busy_reject_count;
    uint32_t lora_queue_failure_count;
    uint32_t command_accept_count;
    uint32_t command_duplicate_count;
    uint32_t command_conflict_count;
    uint32_t command_reject_count;
    uint32_t vfd_result_count;
    uint32_t stale_vfd_result_count;
    uint32_t auto_run_request_count;
    uint32_t auto_stop_request_count;
    uint32_t flash_failure_count;
    uint8_t identity_invalid;
    uint8_t parameters_from_defaults;
    uint8_t parameters_dirty;
} MasterRuntimeDiagnostics;

extern MasterRuntimeDiagnostics MasterRuntimeDiag;

void MasterRuntime_Init(void);
void MasterRuntime_ProcessOne(uint32_t now_ms, TickType_t wait_ticks);
uint32_t MasterRuntime_GetControlEpoch(void);

#endif /* MASTER_RUNTIME_H */
