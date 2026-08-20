#include "fake_parameter_store.h"

#include <string.h>

#include "master_config.h"
#include "master_messages.h"

static MasterParameters g_load_parameters;
static MasterParameters g_last_saved;
static ParameterStoreStatus g_load_status;
static ParameterStoreStatus g_save_status;
static uint32_t g_save_count;

void FakeParameterStore_Reset(void)
{
    g_load_parameters.frequency_x100 = MASTER_DEFAULT_FREQUENCY_X100;
    g_load_parameters.target_temperature_x10 = MASTER_DEFAULT_TARGET_TEMP_X10;
    g_load_parameters.control_mode = MASTER_CONTROL_MODE_AUTO;
    memset(&g_last_saved, 0, sizeof(g_last_saved));
    g_load_status = PARAMETER_STORE_DEFAULTS;
    g_save_status = PARAMETER_STORE_SAVED;
    g_save_count = 0U;
}

void FakeParameterStore_SetLoad(ParameterStoreStatus status,
                                const MasterParameters *parameters)
{
    g_load_status = status;
    if (parameters != NULL)
    {
        g_load_parameters = *parameters;
    }
}

void FakeParameterStore_SetSaveStatus(ParameterStoreStatus status)
{
    g_save_status = status;
}

uint32_t FakeParameterStore_GetSaveCount(void)
{
    return g_save_count;
}

const MasterParameters *FakeParameterStore_GetLastSaved(void)
{
    return &g_last_saved;
}

ParameterStoreStatus ParameterStore_Load(MasterParameters *parameters)
{
    if (parameters == NULL)
    {
        return PARAMETER_STORE_INVALID;
    }
    *parameters = g_load_parameters;
    return g_load_status;
}

ParameterStoreStatus ParameterStore_Save(const MasterParameters *parameters)
{
    if (parameters == NULL)
    {
        return PARAMETER_STORE_INVALID;
    }
    g_save_count++;
    g_last_saved = *parameters;
    return g_save_status;
}

uint32_t ParameterStore_GetGeneration(void)
{
    return 0U;
}

uint8_t ParameterStore_HasActiveRecord(void)
{
    return (g_load_status == PARAMETER_STORE_LOADED) ? 1U : 0U;
}
