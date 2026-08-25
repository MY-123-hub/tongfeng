/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "string.h"
#include "lora.h"
#include "modbus_async.h"
#include "DGUS.h"
#include "master_queues.h"
#include "master_runtime.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static uint32_t defaultTaskStack[256];
static osStaticThreadDef_t defaultTaskControl;
static uint32_t loRaTaskStack[256];
static osStaticThreadDef_t loRaTaskControl;
static uint32_t dgusTaskStack[128];
static osStaticThreadDef_t dgusTaskControl;
static uint32_t modBusTaskStack[256];
static osStaticThreadDef_t modBusTaskControl;

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId LoRaTaskHandle;
osThreadId DGUSTaskHandle;
osThreadId ModBusTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void StartLoRaTask(void const * argument);
void StartDGUSTask(void const * argument);
void StartModBusTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  if (MasterQueues_Init() == 0U)
  {
    Error_Handler();
  }
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadStaticDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256,
                    defaultTaskStack, &defaultTaskControl);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of LoRaTask */
  osThreadStaticDef(LoRaTask, StartLoRaTask, osPriorityAboveNormal, 0, 256,
                    loRaTaskStack, &loRaTaskControl);
  LoRaTaskHandle = osThreadCreate(osThread(LoRaTask), NULL);

  /* definition and creation of DGUSTask */
  osThreadStaticDef(DGUSTask, StartDGUSTask, osPriorityLow, 0, 128,
                    dgusTaskStack, &dgusTaskControl);
  DGUSTaskHandle = osThreadCreate(osThread(DGUSTask), NULL);

  /* definition and creation of ModBusTask */
  osThreadStaticDef(ModBusTask, StartModBusTask, osPriorityAboveNormal, 0, 256,
                    modBusTaskStack, &modBusTaskControl);
  ModBusTaskHandle = osThreadCreate(osThread(ModBusTask), NULL);

  if ((defaultTaskHandle == NULL) || (LoRaTaskHandle == NULL) ||
      (DGUSTaskHandle == NULL) || (ModBusTaskHandle == NULL))
  {
    Error_Handler();
  }

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS 1 */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  MasterRuntime_Init();
  /* Infinite loop */
  for(;;)
  {
    MasterRuntime_ProcessOne(HAL_GetTick(), pdMS_TO_TICKS(10U));
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartLoRaTask */
/**
* @brief 任务1：lora 点对点通讯接受处理 —— 通风条件判断与执行。
         任务2：DGUS 触摸数据应答
         执行周期：1ms
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartLoRaTask */
void StartLoRaTask(void const * argument)
{
  /* USER CODE BEGIN StartLoRaTask */
  /* ===== 测试模式（暂时注释，调试主从通信期间关闭）===== */
#if 0
  /* 测试模式：不依赖从机数据，上电即发正转 30Hz，20s 后减速停止，每 100ms 发一次 */
  uint32_t tick_ms = 0U;       /* 上电累计毫秒（任务周期 1ms） */
  uint32_t last_send_ms = 0U;  /* 上次发送命令的时刻 */
#endif

  /* Infinite loop */
  for(;;)
  {
    /* lora 点对点通讯接受处理 —— 通风条件判断与执行 */
    LoraP2PRX();
    LoraP2PTX();

    /* ===== 测试模式（暂时注释）===== */
#if 0
    /* 每 100ms 发一次 Modbus 命令 */
    tick_ms++;
    if ((tick_ms - last_send_ms) >= 100U)
    {
      last_send_ms = tick_ms;
      if (tick_ms < 20000U)              /* 前 20 秒：正转 */
      {
        ModbusTxVfdCmd(modbuswrite_RunFwd, VFD_TARGET_FREQ_X100);
      }
      else                                /* 20 秒后：减速停止 */
      {
        ModbusTxVfdCmd(modbuswrite_StopDec, VFD_TARGET_FREQ_X100);
      }
    }
#endif

    /* 避免同优先级任务被无延迟轮询长期挤占。 */
    osDelay(1);
  }
  /* USER CODE END StartLoRaTask */
}

/* USER CODE BEGIN Header_StartDGUSTask */
/**
* @brief 任务：更新 DGUS 的环境数据
         执行周期：5s
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDGUSTask */
void StartDGUSTask(void const * argument)
{
  /* USER CODE BEGIN StartDGUSTask */
  static MasterUiSnapshot ui_snapshot;
  uint32_t last_update_tick = HAL_GetTick() - 5000U;
  /* Infinite loop */
  for(;;)
  {
    /* DGUS 串口只由本任务处理，避免与 LoRa 任务交叉访问。 */
    DGUS_TouchAck();
    if ((uint32_t)(HAL_GetTick() - last_update_tick) >= 5000U)
    {
      last_update_tick = HAL_GetTick();
      if (MasterQueues_PeekUi(&ui_snapshot) == pdPASS)
      {
        /* 旧串口屏只有一个粮温字段，暂时显示36点中的第1点。
           0表示无效；环境温湿度和风压按当前需求不采集、不刷新。 */
        DGUS_WriteSingleData(DGUS_GrainTemp,
                             (int)ui_snapshot.temperatures[0]);
      }
    }
    osDelay(20);
  }
  /* USER CODE END StartDGUSTask */
}

/* USER CODE BEGIN Header_StartModBusTask */
/**
* @brief 任务：处理 Modbus 接收数据；更新LED1—通风开启标志（亮-通风状态开，灭-通风状态关）
         执行周期：1s
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartModBusTask */
void StartModBusTask(void const * argument)
{
  /* USER CODE BEGIN StartModBusTask */
  static VfdJob job;
  static VfdResult result;
  static MasterEvent event;
  static uint8_t adapter_result_pending;

  VfdModbus_Init();
  /* Infinite loop */
  for(;;)
  {
    VfdModbus_Process(HAL_GetTick(), MasterRuntime_GetControlEpoch());

    if (adapter_result_pending != 0U)
    {
      memset(&event, 0, sizeof(event));
      event.type = MASTER_EVENT_VFD_RESULT;
      event.data.vfd_result = result;
      if (MasterQueues_SendEvent(&event, 0U) == pdPASS)
      {
        adapter_result_pending = 0U;
      }
    }
    else if (VfdModbus_PeekResult(&result) != 0U)
    {
      memset(&event, 0, sizeof(event));
      event.type = MASTER_EVENT_VFD_RESULT;
      event.data.vfd_result = result;
      if (MasterQueues_SendEvent(&event, 0U) == pdPASS)
      {
        VfdModbus_AcknowledgeResult();
      }
    }
    else if ((VfdModbus_IsIdle() != 0U) &&
             (MasterQueues_ReceiveVfdJob(&job, 0U) == pdPASS))
    {
      if (job.epoch != MasterRuntime_GetControlEpoch())
      {
        memset(&result, 0, sizeof(result));
        result.flow_id = job.flow_id;
        result.frequency_x100 = job.frequency_x100;
        result.action = job.action;
        result.epoch = job.epoch;
        result.request_type = job.request_type;
        result.origin = job.origin;
        result.code = VFD_RESULT_CANCELED;
        adapter_result_pending = 1U;
      }
      else if (VfdModbus_Start(&job, HAL_GetTick()) !=
               VFD_MODBUS_START_ACCEPTED)
      {
        memset(&result, 0, sizeof(result));
        result.flow_id = job.flow_id;
        result.frequency_x100 = job.frequency_x100;
        result.action = job.action;
        result.epoch = job.epoch;
        result.request_type = job.request_type;
        result.origin = job.origin;
        result.code = VFD_RESULT_TX_ERROR;
        adapter_result_pending = 1U;
      }
    }

    osDelay(1);
  }
  /* USER CODE END StartModBusTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

