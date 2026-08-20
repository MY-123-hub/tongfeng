#ifndef MASTER_IDENTITY_H
#define MASTER_IDENTITY_H

#include <stdint.h>

void MasterIdentity_Init(uint8_t raw_group);
uint8_t MasterIdentity_IsValid(void);
uint8_t MasterIdentity_GetGroup(void);
uint8_t MasterIdentity_GetRawGroup(void);

#endif /* MASTER_IDENTITY_H */
