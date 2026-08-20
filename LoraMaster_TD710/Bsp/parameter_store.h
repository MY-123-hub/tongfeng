#ifndef PARAMETER_STORE_H
#define PARAMETER_STORE_H

#include <stdint.h>

#include "parameter_record.h"

#define PARAMETER_STORE_SLOT_A_ADDRESS   (0x0800F800UL)
#define PARAMETER_STORE_SLOT_B_ADDRESS   (0x0800FC00UL)
#define PARAMETER_STORE_PAGE_SIZE        (0x00000400UL)

typedef enum
{
    PARAMETER_STORE_LOADED = 0,
    PARAMETER_STORE_DEFAULTS,
    PARAMETER_STORE_SAVED,
    PARAMETER_STORE_UNCHANGED,
    PARAMETER_STORE_INVALID,
    PARAMETER_STORE_FLASH_ERROR,
    PARAMETER_STORE_VERIFY_ERROR
} ParameterStoreStatus;

ParameterStoreStatus ParameterStore_Load(MasterParameters *parameters);
ParameterStoreStatus ParameterStore_Save(const MasterParameters *parameters);
uint32_t ParameterStore_GetGeneration(void);
uint8_t ParameterStore_HasActiveRecord(void);

#endif /* PARAMETER_STORE_H */
