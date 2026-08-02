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
#include "modbus.h"
#include "DGUS.h"

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
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of LoRaTask */
  osThreadDef(LoRaTask, StartLoRaTask, osPriorityIdle, 0, 128);
  LoRaTaskHandle = osThreadCreate(osThread(LoRaTask), NULL);

  /* definition and creation of DGUSTask */
  osThreadDef(DGUSTask, StartDGUSTask, osPriorityIdle, 0, 128);
  DGUSTaskHandle = osThreadCreate(osThread(DGUSTask), NULL);

  /* definition and creation of ModBusTask */
  osThreadDef(ModBusTask, StartModBusTask, osPriorityIdle, 0, 128);
  ModBusTaskHandle = osThreadCreate(osThread(ModBusTask), NULL);

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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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
  /* Infinite loop */
  for(;;)
  {
    /* lora 点对点通讯接受处理 —— 通风条件判断与执行 */
    LoraP2PRX();       

    /* DGUS 触摸数据应答 */
    DGUS_TouchAck();

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
  /* Infinite loop */
  for(;;)
  {
    /* 更新 DGUS 的环境数据 */
    DGUS_WriteSingleData(DGUS_GrainTemp,LoRaType.DS18B20_Data[0]);   //粮面温度   
    DGUS_WriteSingleData(DGUS_EnvirHumi,LoRaType.DHT11_Humi);   //环境湿度   
    DGUS_WriteSingleData(DGUS_EnvirTemp,LoRaType.DHT11_Temp);   //环境温度   
    DGUS_WriteSingleData(DGUS_GrainSpeed,LoRaType.WindPressure);   //环境温度   
    osDelay(5000);
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
  /* Infinite loop */
  for(;;)
  {
    /* 更新LED1——通风状态标志 */
//    if(SysVariType.vent_open_flag==1)led1_on();else led1_off();
    
    /* 处理 Modbus 接收数据 */
    ModBusRxProc();
    
    /* 回包应在毫秒级处理，避免阻塞下一条变频器命令。 */
    osDelay(10);
  }
  /* USER CODE END StartModBusTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

