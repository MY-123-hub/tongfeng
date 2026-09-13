#include "master_ingress.h"

#include <stddef.h>

#include "master_config.h"

MasterIngressRoute MasterIngress_Route(const LoRaMessage *message,
                                       uint8_t local_group)
{
    if ((message == NULL) || (local_group < MASTER_GROUP_MIN) ||
        (local_group > MASTER_GROUP_MAX))
    {
        return MASTER_INGRESS_DROP;
    }

    if ((message->destination_role != LORA_ROLE_MASTER) ||
        (message->destination_group != local_group))
    {
        return MASTER_INGRESS_DROP;
    }

    if ((message->source_role == LORA_ROLE_CONTROL_ROOM) &&
        (message->source_group == 0U))
    {
        return MASTER_INGRESS_CONTROL_ROOM;
    }

    if ((message->source_role == LORA_ROLE_SLAVE) &&
        (message->source_group == local_group))
    {
        return MASTER_INGRESS_SLAVE;
    }

    return MASTER_INGRESS_DROP;
}
