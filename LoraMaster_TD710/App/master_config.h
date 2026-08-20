#ifndef MASTER_CONFIG_H
#define MASTER_CONFIG_H

#define MASTER_GROUP_MIN                         (1U)
#define MASTER_GROUP_MAX                         (4U)

/* 现场可调参数：首次联调后按实际轮询周期和空口延迟复核。 */
#define MASTER_TEMP_CACHE_FRESH_MS               (5000UL)
#define MASTER_SLAVE_RESPONSE_TIMEOUT_MS         (3000UL)

#define MASTER_DEFAULT_FREQUENCY_X100            (3000U)
#define MASTER_MIN_FREQUENCY_X100                (0U)
#define MASTER_MAX_FREQUENCY_X100                (5000U)

#define MASTER_DEFAULT_TARGET_TEMP_X10           (260)
#define MASTER_MIN_TARGET_TEMP_X10               (-550)
#define MASTER_MAX_TARGET_TEMP_X10               (1250)

#define MASTER_AUTO_STOP_HYSTERESIS_X10          (5)
#define MASTER_AUTO_STOP_HOLD_MS                  (60000UL)

/* 安全停机和脏参数都限速重试，避免忙等待和Flash过度擦写。 */
#define MASTER_SAFETY_STOP_RETRY_MS                (1000UL)
#define MASTER_FLASH_RETRY_MS                      (5000UL)

#define MASTER_COMMAND_HISTORY_DEPTH             (4U)

#endif /* MASTER_CONFIG_H */
