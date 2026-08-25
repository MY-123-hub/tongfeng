#include "parameter_store.h"

#include <string.h>

#include "main.h"
#include "master_config.h"
#include "master_messages.h"

static MasterParameters g_current_parameters;
static uint32_t g_generation;
static uint8_t g_active_slot;
static uint8_t g_has_active_record;

static const uint8_t *ParameterStore_SlotAddress(uint8_t slot)
{
    uint32_t address = (slot == 0U) ? PARAMETER_STORE_SLOT_A_ADDRESS :
                                     PARAMETER_STORE_SLOT_B_ADDRESS;
    return (const uint8_t *)address;
}

static uint8_t ParameterStore_Equal(const MasterParameters *left,
                                    const MasterParameters *right)
{
    return ((left->frequency_x100 == right->frequency_x100) &&
            (left->target_temperature_x10 == right->target_temperature_x10) &&
            (left->control_mode == right->control_mode)) ? 1U : 0U;
}

ParameterStoreStatus ParameterStore_Load(MasterParameters *parameters)
{
    uint8_t selected_slot;

    if (parameters == NULL)
    {
        return PARAMETER_STORE_INVALID;
    }

    if (ParameterRecord_SelectNewest(
            ParameterStore_SlotAddress(0U),
            ParameterStore_SlotAddress(1U),
            &g_current_parameters,
            &g_generation,
            &selected_slot) != 0U)
    {
        g_active_slot = selected_slot;
        g_has_active_record = 1U;
        *parameters = g_current_parameters;
        return PARAMETER_STORE_LOADED;
    }

    g_current_parameters.frequency_x100 = MASTER_DEFAULT_FREQUENCY_X100;
    g_current_parameters.target_temperature_x10 = MASTER_DEFAULT_TARGET_TEMP_X10;
    g_current_parameters.control_mode = MASTER_CONTROL_MODE_AUTO;
    g_generation = 0U;
    g_active_slot = 0U;
    g_has_active_record = 0U;
    *parameters = g_current_parameters;
    return PARAMETER_STORE_DEFAULTS;
}

ParameterStoreStatus ParameterStore_Save(const MasterParameters *parameters)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;
    uint32_t target_address;
    uint32_t offset;
    uint32_t next_generation;
    uint16_t halfword;
    uint8_t target_slot;
    uint8_t record[PARAMETER_RECORD_SIZE];
    MasterParameters verified_parameters;
    uint32_t verified_generation;
    HAL_StatusTypeDef hal_status;

    if (ParameterRecord_ParametersValid(parameters) == 0U)
    {
        return PARAMETER_STORE_INVALID;
    }
    if ((g_has_active_record != 0U) &&
        (ParameterStore_Equal(parameters, &g_current_parameters) != 0U))
    {
        return PARAMETER_STORE_UNCHANGED;
    }

    target_slot = (g_has_active_record != 0U) ? (uint8_t)(g_active_slot ^ 1U) : 0U;
    target_address = (target_slot == 0U) ? PARAMETER_STORE_SLOT_A_ADDRESS :
                                          PARAMETER_STORE_SLOT_B_ADDRESS;
    next_generation = g_generation + 1U;
    if (ParameterRecord_Encode(parameters, next_generation,
                               record, sizeof(record)) != PARAMETER_RECORD_OK)
    {
        return PARAMETER_STORE_INVALID;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return PARAMETER_STORE_FLASH_ERROR;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = target_address;
    erase.NbPages = 1U;
    hal_status = HAL_FLASHEx_Erase(&erase, &page_error);
    if (hal_status == HAL_OK)
    {
        for (offset = 0U; offset < PARAMETER_RECORD_COMMIT_OFFSET; offset += 2U)
        {
            halfword = (uint16_t)((uint16_t)record[offset] |
                                  (uint16_t)((uint16_t)record[offset + 1U] << 8U));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                  target_address + offset,
                                  halfword) != HAL_OK)
            {
                hal_status = HAL_ERROR;
                break;
            }
        }
    }

    if ((hal_status == HAL_OK) &&
        (memcmp((const void *)target_address,
                record,
                PARAMETER_RECORD_COMMIT_OFFSET) != 0))
    {
        hal_status = HAL_ERROR;
    }

    if (hal_status == HAL_OK)
    {
        for (offset = PARAMETER_RECORD_COMMIT_OFFSET;
             offset < PARAMETER_RECORD_SIZE;
             offset += 2U)
        {
            halfword = (uint16_t)((uint16_t)record[offset] |
                                  (uint16_t)((uint16_t)record[offset + 1U] << 8U));
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                  target_address + offset,
                                  halfword) != HAL_OK)
            {
                hal_status = HAL_ERROR;
                break;
            }
        }
    }
    (void)HAL_FLASH_Lock();

    if (hal_status != HAL_OK)
    {
        return PARAMETER_STORE_FLASH_ERROR;
    }
    if (ParameterRecord_Decode((const uint8_t *)target_address,
                               PARAMETER_RECORD_SIZE,
                               &verified_parameters,
                               &verified_generation) != PARAMETER_RECORD_OK)
    {
        return PARAMETER_STORE_VERIFY_ERROR;
    }
    if ((verified_generation != next_generation) ||
        (ParameterStore_Equal(&verified_parameters, parameters) == 0U))
    {
        return PARAMETER_STORE_VERIFY_ERROR;
    }

    g_current_parameters = *parameters;
    g_generation = next_generation;
    g_active_slot = target_slot;
    g_has_active_record = 1U;
    return PARAMETER_STORE_SAVED;
}

uint32_t ParameterStore_GetGeneration(void)
{
    return g_generation;
}

uint8_t ParameterStore_HasActiveRecord(void)
{
    return g_has_active_record;
}
