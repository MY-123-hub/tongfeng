#include "master_identity.h"

#include "master_config.h"

static uint8_t g_raw_group;
static uint8_t g_group_valid;

void MasterIdentity_Init(uint8_t raw_group)
{
    g_raw_group = raw_group;
    g_group_valid = ((raw_group >= MASTER_GROUP_MIN) &&
                     (raw_group <= MASTER_GROUP_MAX)) ? 1U : 0U;
}

uint8_t MasterIdentity_IsValid(void)
{
    return g_group_valid;
}

uint8_t MasterIdentity_GetGroup(void)
{
    return (g_group_valid != 0U) ? g_raw_group : 0U;
}

uint8_t MasterIdentity_GetRawGroup(void)
{
    return g_raw_group;
}
