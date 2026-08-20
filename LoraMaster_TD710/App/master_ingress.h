#ifndef MASTER_INGRESS_H
#define MASTER_INGRESS_H

#include <stdint.h>

#include "lora_protocol.h"

typedef enum
{
    MASTER_INGRESS_DROP = 0,
    MASTER_INGRESS_CONTROL_ROOM,
    MASTER_INGRESS_SLAVE
} MasterIngressRoute;

MasterIngressRoute MasterIngress_Route(const LoRaMessage *message,
                                       uint8_t local_group);

#endif /* MASTER_INGRESS_H */
