#ifndef TEST_FAKE_PARAMETER_STORE_CONTROL_H
#define TEST_FAKE_PARAMETER_STORE_CONTROL_H

#include <stdint.h>

#include "parameter_store.h"

void FakeParameterStore_Reset(void);
void FakeParameterStore_SetLoad(ParameterStoreStatus status,
                                const MasterParameters *parameters);
void FakeParameterStore_SetSaveStatus(ParameterStoreStatus status);
uint32_t FakeParameterStore_GetSaveCount(void);
const MasterParameters *FakeParameterStore_GetLastSaved(void);

#endif /* TEST_FAKE_PARAMETER_STORE_CONTROL_H */
