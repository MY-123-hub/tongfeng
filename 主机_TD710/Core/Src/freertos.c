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
#include "BME280.h"

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
static uint32_t bme280TaskStack[256];
static osStaticThreadDef_t bme280TaskControl;

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId LoRaTaskHandle;
osThreadId DGUSTaskHandle;
osThreadId ModBusTaskHandle;
osThreadId BME280TaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void StartLoRaTask(void const * argument);
void StartDGUSTask(void const * argument);
void StartModBusTask(void const * argument);
void StartBME280Task(void const * argument);

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

  /* definition and creation of BME280Task */
  osThreadStaticDef(BME280Task, StartBME280Task, osPriorityLow, 0, 256,
                    bme280TaskStack, &bme280TaskControl);
  BME280TaskHandle = osThreadCreate(osThread(BME280Task), NULL);

  if ((defaultTaskHandle == NULL) || (LoRaTaskHandle == NULL) ||
      (DGUSTaskHandle == NULL) || (ModBusTaskHandle == NULL) ||
      (BME280TaskHandle == NULL))
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
  static MasterEvent dgus_event;
  uint32_t last_update_tick = HAL_GetTick() - 1000U;
  uint32_t last_screen_refresh_tick = HAL_GetTick() - 2000U;
  uint32_t boot_tick = HAL_GetTick();
  static uint8_t temperature_sent;
  static uint8_t humidity_sent;
  static uint8_t master_temperature_sent;
  static uint8_t master_humidity_sent;
  static uint8_t slave_pressure_sent;
  static uint8_t master_pressure_sent;
  static uint8_t fan_state_sent;
  static uint16_t last_temperature_x10;
  static uint16_t last_humidity_x10;
  static int16_t last_master_temperature_x10;
  static uint16_t last_master_humidity_x10;
  static uint16_t last_slave_pressure_hpa;
  static uint16_t last_master_pressure_hpa;
  static uint16_t last_fan_state;
  static uint32_t last_uptime_minute = 0xFFFFFFFFUL;
  static uint8_t initial_setpoint_read_requested;
  static uint32_t applied_remote_temperature_generation;
  /* Infinite loop */
  for(;;)
  {
    DGUSReceivedWrite dgus_write;

    /* USART1 is owned by this task: receive 0x83 writes before transmitting. */
    DGUS_ProcessRx();
    while (DGUS_TakeReceivedWrite(&dgus_write) != 0U)
    {
      memset(&dgus_event, 0, sizeof(dgus_event));
      dgus_event.type = MASTER_EVENT_DGUS_WRITE;
      dgus_event.data.dgus_write.address = dgus_write.address;
      dgus_event.data.dgus_write.value = dgus_write.value;
      (void)MasterQueues_SendEvent(&dgus_event, pdMS_TO_TICKS(20U));
    }

    if ((uint32_t)(HAL_GetTick() - last_update_tick) >= 1000U)
    {
      uint32_t now_tick = HAL_GetTick();
      last_update_tick = now_tick;
      if (MasterQueues_PeekUi(&ui_snapshot) == pdPASS)
      {
        uint8_t uptime_ascii[16];
        uint32_t elapsed_minutes = (uint32_t)(now_tick - boot_tick) / 60000U;
        uint32_t minutes = elapsed_minutes;
        uint32_t years;
        uint8_t months;
        uint8_t days;
        uint8_t hours;
        uint8_t minute_of_hour;

        /* Re-send only real-time status.  Editable screen fields must not be
           periodically overwritten while an operator is entering a value. */
        if ((uint32_t)(now_tick - last_screen_refresh_tick) >= 2000U)
        {
          last_screen_refresh_tick = now_tick;
          temperature_sent = 0U;
          humidity_sent = 0U;
          master_temperature_sent = 0U;
          master_humidity_sent = 0U;
          slave_pressure_sent = 0U;
          master_pressure_sent = 0U;
          fan_state_sent = 0U;
          last_uptime_minute = 0xFFFFFFFFUL;
        }

        minute_of_hour = (uint8_t)(minutes % 60U); minutes /= 60U;
        hours = (uint8_t)(minutes % 24U); minutes /= 24U;
        days = (uint8_t)(minutes % 30U); minutes /= 30U;
        months = (uint8_t)(minutes % 12U); minutes /= 12U;
        years = minutes;
        uptime_ascii[0] = (uint8_t)('0' + ((years / 1000U) % 10U));
        uptime_ascii[1] = (uint8_t)('0' + ((years / 100U) % 10U));
        uptime_ascii[2] = (uint8_t)('0' + ((years / 10U) % 10U));
        uptime_ascii[3] = (uint8_t)('0' + (years % 10U));
        uptime_ascii[4] = (uint8_t)'-';
        uptime_ascii[5] = (uint8_t)('0' + (months / 10U));
        uptime_ascii[6] = (uint8_t)('0' + (months % 10U));
        uptime_ascii[7] = (uint8_t)'-';
        uptime_ascii[8] = (uint8_t)('0' + (days / 10U));
        uptime_ascii[9] = (uint8_t)('0' + (days % 10U));
        uptime_ascii[10] = (uint8_t)'-';
        uptime_ascii[11] = (uint8_t)('0' + (hours / 10U));
        uptime_ascii[12] = (uint8_t)('0' + (hours % 10U));
        uptime_ascii[13] = (uint8_t)':';
        uptime_ascii[14] = (uint8_t)('0' + (minute_of_hour / 10U));
        uptime_ascii[15] = (uint8_t)('0' + (minute_of_hour % 10U));

        /* BME280 values physically connected to the master itself. */
        if ((ui_snapshot.environment_valid != 0U) &&
            ((master_temperature_sent == 0U) ||
             (last_master_temperature_x10 !=
              ui_snapshot.environment_temperature_x10)))
        {
          if (DGUS_WriteSingleData(DGUS_VP_MASTER_TEMPERATURE,
              (uint16_t)ui_snapshot.environment_temperature_x10) != 0U)
          {
            last_master_temperature_x10 =
              ui_snapshot.environment_temperature_x10;
            master_temperature_sent = 1U;
          }
        }
        if ((ui_snapshot.environment_valid != 0U) &&
            ((master_humidity_sent == 0U) ||
             (last_master_humidity_x10 !=
              ui_snapshot.environment_humidity_x10)))
        {
          if (DGUS_WriteSingleData(DGUS_VP_MASTER_HUMIDITY,
              ui_snapshot.environment_humidity_x10) != 0U)
          {
            last_master_humidity_x10 = ui_snapshot.environment_humidity_x10;
            master_humidity_sent = 1U;
          }
        }
        /* Pressure is received as Pa but a DGUS VP is one 16-bit word.
           Publish hPa (Pa / 100): normal atmospheric pressure is 1013. */
        if (ui_snapshot.slave_environment_pressure_valid != 0U)
        {
          uint16_t pressure_hpa =
              (uint16_t)(ui_snapshot.slave_environment_pressure_pa / 100UL);
          if ((slave_pressure_sent == 0U) ||
              (last_slave_pressure_hpa != pressure_hpa))
          {
            if (DGUS_WriteSingleData(DGUS_VP_SLAVE_PRESSURE,
                                     pressure_hpa) != 0U)
            {
              last_slave_pressure_hpa = pressure_hpa;
              slave_pressure_sent = 1U;
            }
          }
        }
        if (ui_snapshot.environment_valid != 0U)
        {
          uint16_t pressure_hpa =
              (uint16_t)(ui_snapshot.environment_pressure_pa / 100UL);
          if ((master_pressure_sent == 0U) ||
              (last_master_pressure_hpa != pressure_hpa))
          {
            if (DGUS_WriteSingleData(DGUS_VP_MASTER_PRESSURE,
                                     pressure_hpa) != 0U)
            {
              last_master_pressure_hpa = pressure_hpa;
              master_pressure_sent = 1U;
            }
          }
        }

        if ((ui_snapshot.temperature_valid != 0U) &&
            ((temperature_sent == 0U) ||
             (last_temperature_x10 != ui_snapshot.average_temperature_x10)))
        {
          if (DGUS_WriteSingleData(DGUS_VP_AVERAGE_TEMPERATURE,
                                   ui_snapshot.average_temperature_x10) != 0U)
          {
            last_temperature_x10 = ui_snapshot.average_temperature_x10;
            temperature_sent = 1U;
          }
        }
        if ((ui_snapshot.humidity_valid != 0U) &&
            ((humidity_sent == 0U) ||
             (last_humidity_x10 != ui_snapshot.average_humidity_x10)))
        {
          if (DGUS_WriteSingleData(DGUS_VP_AVERAGE_HUMIDITY,
                                   ui_snapshot.average_humidity_x10) != 0U)
          {
            last_humidity_x10 = ui_snapshot.average_humidity_x10;
            humidity_sent = 1U;
          }
        }

        /* Only a successfully accepted upper-computer command owns this
           one-shot write.  Screen-originated edits are never echoed back. */
        if (applied_remote_temperature_generation !=
            ui_snapshot.target_temperature_screen_generation)
        {
          if (DGUS_WriteSingleData(DGUS_VP_TARGET_TEMPERATURE,
                                   (uint16_t)ui_snapshot.target_temperature_x10) != 0U)
          {
            applied_remote_temperature_generation =
                ui_snapshot.target_temperature_screen_generation;
          }
        }
        if ((initial_setpoint_read_requested == 0U) &&
            (ui_snapshot.target_temperature_screen_generation == 0U) &&
            ((uint32_t)(now_tick - boot_tick) >= 2000U))
        {
          /* 0x5013 and 0x5014 are consecutive.  The reply is passed to the
             runtime through the same 0x83 path as an operator edit. */
          if (DGUS_ReadWords(DGUS_VP_TARGET_TEMPERATURE, 2U) != 0U)
          {
            initial_setpoint_read_requested = 1U;
          }
        }
        if (last_uptime_minute != elapsed_minutes)
        {
          if (DGUS_WriteAscii(DGUS_VP_UPTIME, uptime_ascii,
                              (uint8_t)sizeof(uptime_ascii)) != 0U)
          {
            last_uptime_minute = elapsed_minutes;
          }
        }
        /* The time VPs are ASCII text controls on this project.  Never write
           0x6070--0x6074 or 0x6090--0x6094 from the master. */
        if ((fan_state_sent == 0U) || (last_fan_state !=
            ((ui_snapshot.fan_state == MASTER_FAN_STATE_RUNNING) ? 1U : 0U)))
        {
          uint16_t fan_state = (ui_snapshot.fan_state == MASTER_FAN_STATE_RUNNING) ? 1U : 0U;

          if ((DGUS_WriteSingleData(DGUS_VP_FAN_ANIMATION, fan_state) != 0U) &&
              (DGUS_WriteSingleData(DGUS_VP_INDICATOR_ANIMATION, fan_state) != 0U))
          {
            last_fan_state = fan_state;
            fan_state_sent = 1U;
          }
        }
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

/* USER CODE BEGIN Header_StartBME280Task */
/**
* @brief BME280 environment sampling task. PB14=SCL, PB15=SDA.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartBME280Task */
void StartBME280Task(void const * argument)
{
  /* USER CODE BEGIN StartBME280Task */
  BME280_HandleTypeDef sensor;
  BME280_Data sensor_data;
  MasterEnvironmentSample sample;
  BME280_Status status;
  uint32_t measurement_delay_ms;

  (void)argument;
  memset(&sensor, 0, sizeof(sensor));
  memset(&sensor_data, 0, sizeof(sensor_data));
  memset(&sample, 0, sizeof(sample));

  for(;;)
  {
    status = BME280_Init(&sensor);
    if (status == BME280_OK)
    {
      status = BME280_Config(&sensor,
                             BME280_OVERSAMPLING_X1,
                             BME280_OVERSAMPLING_X1,
                             BME280_OVERSAMPLING_X1,
                             BME280_FILTER_OFF);
    }

    if (status != BME280_OK)
    {
      sample.valid = 0U;
      sample.error_code = (uint8_t)status;
      sample.sample_tick = HAL_GetTick();
      (void)MasterQueues_OverwriteEnvironment(&sample);
      osDelay(1000U);
      continue;
    }

    measurement_delay_ms = BME280_GetMeasurementDelayMs(&sensor);
    for(;;)
    {
      status = BME280_TriggerMeasurement(&sensor);
      if (status == BME280_OK)
      {
        osDelay(measurement_delay_ms);
        status = BME280_ReadMeasurement(&sensor, &sensor_data);
      }

      memset(&sample, 0, sizeof(sample));
      sample.sample_tick = HAL_GetTick();
      sample.error_code = (uint8_t)status;
      if ((status == BME280_OK) &&
          (sensor_data.temperature_x100 >= -4000) &&
          (sensor_data.temperature_x100 <= 8500) &&
          (sensor_data.humidity_x1024 <= 102400U) &&
          (sensor_data.pressure_pa >= 30000U) &&
          (sensor_data.pressure_pa <= 110000U))
      {
        if (sensor_data.temperature_x100 >= 0)
        {
          sample.temperature_x10 =
              (int16_t)((sensor_data.temperature_x100 + 5) / 10);
        }
        else
        {
          sample.temperature_x10 =
              (int16_t)((sensor_data.temperature_x100 - 5) / 10);
        }
        sample.humidity_x10 = (uint16_t)(
            (sensor_data.humidity_x1024 * 10U + 512U) / 1024U);
        sample.pressure_pa = sensor_data.pressure_pa;
        sample.valid = 1U;
      }
      else
      {
        sample.valid = 0U;
        if (status == BME280_OK)
        {
          sample.error_code = (uint8_t)BME280_ERROR_NOT_READY;
        }
      }
      (void)MasterQueues_OverwriteEnvironment(&sample);

      if (sample.valid == 0U)
      {
        osDelay(1000U);
        break;
      }
      osDelay(1000U);
    }
  }
  /* USER CODE END StartBME280Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

