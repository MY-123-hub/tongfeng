#ifndef MASTER_PROTOCOL_VALUES_H
#define MASTER_PROTOCOL_VALUES_H

#define MASTER_ACK_ACCEPTED               (0x00U)
#define MASTER_ACK_DUPLICATE              (0x01U)
#define MASTER_ACK_REJECTED               (0x02U)

#define MASTER_ERROR_NONE                 (0x00U)
#define MASTER_ERROR_INVALID_PARAMETER    (0x01U)
#define MASTER_ERROR_STATE_NOT_ALLOWED    (0x02U)
#define MASTER_ERROR_BUSY                 (0x03U)
#define MASTER_ERROR_SLAVE_TIMEOUT        (0x04U)
#define MASTER_ERROR_VFD_TIMEOUT          (0x05U)
#define MASTER_ERROR_VFD_RESPONSE         (0x06U)
#define MASTER_ERROR_FLASH                (0x07U)
#define MASTER_ERROR_FLOW_CONFLICT        (0x08U)
#define MASTER_ERROR_UNSUPPORTED          (0x09U)

#endif /* MASTER_PROTOCOL_VALUES_H */
