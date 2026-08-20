#ifndef MASTER_QUEUES_H
#define MASTER_QUEUES_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "master_messages.h"

#define MASTER_EVENT_QUEUE_DEPTH       (4U)
#define MASTER_LORA_TX_QUEUE_DEPTH     (4U)
#define MASTER_VFD_JOB_QUEUE_DEPTH     (3U)
#define MASTER_UI_QUEUE_DEPTH          (1U)

typedef struct
{
    uint32_t event_full_count;
    uint32_t lora_tx_full_count;
    uint32_t vfd_job_full_count;
    uint32_t vfd_job_evicted_count;
    uint32_t ui_write_failure_count;
} MasterQueueDiagnostics;

extern MasterQueueDiagnostics MasterQueueDiag;

/**
 ******************************************************************************
  @功能：创建主机业务使用的全部静态队列
  @日期：2026-08-19
  @参数：无
  @返回值：uint8_t - 1 表示全部创建成功，0 表示失败
  @使用说明：必须在调度器启动前调用一次；队列存储区全部位于静态 RAM
 ******************************************************************************
 */
uint8_t MasterQueues_Init(void);

BaseType_t MasterQueues_SendEvent(const MasterEvent *event, TickType_t wait_ticks);
BaseType_t MasterQueues_ReceiveEvent(MasterEvent *event, TickType_t wait_ticks);

BaseType_t MasterQueues_SendLoRa(const LoRaMessage *message, TickType_t wait_ticks);
BaseType_t MasterQueues_ReceiveLoRa(LoRaMessage *message, TickType_t wait_ticks);

BaseType_t MasterQueues_SendVfdJob(const VfdJob *job, TickType_t wait_ticks);
BaseType_t MasterQueues_SendVfdJobFront(const VfdJob *job, TickType_t wait_ticks);
BaseType_t MasterQueues_SendEmergencyVfdJob(const VfdJob *job);
BaseType_t MasterQueues_ReceiveVfdJob(VfdJob *job, TickType_t wait_ticks);

BaseType_t MasterQueues_OverwriteUi(const MasterUiSnapshot *snapshot);
BaseType_t MasterQueues_PeekUi(MasterUiSnapshot *snapshot);

UBaseType_t MasterQueues_EventWaiting(void);
UBaseType_t MasterQueues_LoRaWaiting(void);
UBaseType_t MasterQueues_VfdWaiting(void);

#endif /* MASTER_QUEUES_H */
