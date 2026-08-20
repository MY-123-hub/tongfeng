#include "lora.h"


#include "dip_swich.h"
#include "lora_stream_parser.h"
#include "lora_transport.h"
#include "master_queues.h"
#include "master_identity.h"
#include "master_ingress.h"

LoRaDiagnostics LoRaDiag = {0};
uint16_t LORA_cntPre = 0;
static char *LORA_lastCommand;
static uint8_t LORA_commandFailures;
static LoRaStreamParser g_lora_stream_parser;
static LoRaMessage g_lora_received_message;
static uint32_t g_lora_last_data_loss_count;
static uint32_t g_lora_last_rx_tick;
static uint8_t g_lora_parser_initialized;
static uint8_t g_lora_tx_frame[LORA_PROTOCOL_MAX_FRAME_SIZE];
static uint16_t g_lora_tx_frame_length;
static uint8_t g_lora_tx_pending;

#define LORA_COMMAND_RETRY_COUNT 3U
#define LORA_RX_PROCESS_BUDGET 128U
#define LORA_FRAME_TIMEOUT_MS 200U


/*****************************************************
  * 函数名称：	LORA_Clear
  * 函数功能：	清空缓存
  * 入口参数：	无
  * 返回参数：	无
  * 说    明：  无
*****************************************************/
void LORA_Clear(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	memset(Rx2Buffer, 0, sizeof(Rx2Buffer));
	rx2_pointer = 0;
	rx2_frame_ready = 0;
	rx2_overflow = 0;
	LORA_cntPre = 0;
	if (primask == 0U)
	{
		__enable_irq();
	}
}


/*****************************************************
  * 函数名称：	LORA_WaitRecive
  * 函数功能：	等待接收完成
  * 入口参数：	无
  * 返回参数：	REV_OK-接收完成		REV_WAIT-接收超时未完成
  * 说    明：  循环调用检测是否接收完成
*****************************************************/
_Bool LORA_WaitRecive(void)
{
	if (rx2_overflow != 0U)
	{
		LORA_Clear();
		return REV_WAIT;
	}
	if (rx2_frame_ready != 0U)
	{
		return REV_OK;
	}
	if(rx2_pointer == 0) 							//如果接收计数为0 则说明没有处于接收数据中，所以直接跳出，结束函数
		return REV_WAIT;
	if(rx2_pointer == LORA_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
	{
    HAL_Delay(5);   // 确保接受数据完整性
    if(rx2_pointer == LORA_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
    {
	  rx2_frame_ready = 1;						// 冻结当前帧，防止解析期间被中断覆盖
      return REV_OK;								//返回接收完成标志
    }
	}
	LORA_cntPre = rx2_pointer;					//置为相同
    return REV_WAIT;								//返回接收未完成标志
}


/*****************************************************
  * 函数名称：	LORA_SendCmd
  * 函数功能：	发送命令
  * 入口参数：	cmd：命令
                res：需要检查的返回指令
  * 返回参数：	0-成功	1-失败
  * 说明：		
*****************************************************/
_Bool LORA_SendCmd(char *cmd, char *res)
{
	unsigned char timeOut = 30;

	LORA_Clear();
	Usart_SendString(huart2, (unsigned char *)cmd, strlen((const char *)cmd));
	
	while(timeOut--)
	{
		if(LORA_WaitRecive() == REV_OK)							//如果收到数据
		{
			if(strstr((const char *)Rx2Buffer, res) != NULL)		//如果检索到关键词
			{
				LORA_Clear();									//清空缓存
				LORA_commandFailures = 0U;
				LORA_lastCommand = cmd;
				return 0;
			}
		}
		
		HAL_Delay(10);
	}
	
	if (LORA_lastCommand != cmd)
	{
		LORA_lastCommand = cmd;
		LORA_commandFailures = 0U;
	}
	LORA_commandFailures++;
	if (LORA_commandFailures >= LORA_COMMAND_RETRY_COUNT)
	{
		/* 不让缺失的 LoRa 模块永久阻塞主机启动。 */
		LORA_commandFailures = 0U;
		LoRaDiag.config_failure_count++;
		LoRaDiag.configuration_degraded = 1U;
		return 0;
	}

	return 1;
}


/*****************************************************
  * 函数名称：	LORA_SendData
  * 函数功能：	发送数据
  * 入口参数：	data：数据
                len：长度
  * 返回参数：	无
  * 说明：		
*****************************************************/
void LORA_SendData(unsigned char *data, unsigned short len)
{
	LORA_Clear();								//清空接收缓存
  
  Usart_SendString(huart2, data, len);		//发送设备连接请求数据
}


/*****************************************************
  * 函数名称：	LORA_Init
  * 函数功能：	配置LORA参数
  * 入口参数：	无
  * 返回参数：	无
  * 说明：		
*****************************************************/
void LORA_Init(void)
{
  LoraTransport_Init();
  lora_reset_off();
  HAL_Delay(10);
  lora_reset_on();
  HAL_Delay(50);
  
  
	LORA_Clear();       // 清除串口 2 接受数组
  
#if 1   // 配置点对点透传通讯参数，仅设置一次，重启模组参数生效发送数据即可，低功耗模式另说
  
	LORA_SendCmd("AT+ENTM\r\n", "");      // 退出配置模式，重新进入配置模式，否则会卡住，因为在配置模式中不会回应进入配置的命令，导致后续程序卡住

	while(LORA_SendCmd("+++", "a"))       // 进入参数配置模式 "+++"——回复判定："a"
    HAL_Delay(50);
	
  while(LORA_SendCmd("a", "OK"))        // 进入参数配置模式 "a"——回复判定："OK"
    HAL_Delay(50);	

  while(LORA_SendCmd("AT+WMODE=TRANS\r\n", "OK"))        // 协议设置为透传模式 ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+PMODE=RUN\r\n", "OK"))        // 工作模式设置为 ”RUN“ ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+ITM=20\r\n", "OK"))        // 空闲时间设置为 20（s） ——回复判定："OK"；此参数对 RUN、LSR 模式无效
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+WTM=2000\r\n", "OK"))        // 唤醒时间：2000（ms） ——回复判定："OK"；此参数对 RUN、LSR 模式无效
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+RTO=500\r\n", "OK"))        // 接收超时：500（ms） ——回复判定："OK"；仅在 LR/LSR 模式下有效
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+FDMODE=OFF\r\n", "OK"))        // 关闭上下行分频 ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+CH=4700\r\n", "OK"))        // 接受/发送信道设置为：4700 ——回复判定："OK"；工作频段：(398+ch)MHz
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+SPD=10\r\n", "OK"))        // 速率：10 ——回复判定："OK"；10: 21875bps
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+PWR=22\r\n", "OK"))        // 发射功率：22 ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+FEC=1\r\n", "OK"))        // 前向纠错：关闭 ——回复判定："OK"；开启后数据传输更加稳定但降低通信速率
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+LBT=OFF\r\n", "OK"))        // LBT：关闭 ——回复判定："OK"；开启后 LoRa 发送前进行信道状态
    HAL_Delay(50);	
  
  {
    char cmd_addr[16];
    uint8_t dip_addr = MasterIdentity_GetRawGroup();

    sprintf(cmd_addr, "AT+ADDR=%u\r\n", (unsigned int)dip_addr);
    while(LORA_SendCmd(cmd_addr, "OK"))
      HAL_Delay(50);
  }
  
  while(LORA_SendCmd("AT+LRTO=3\r\n", "OK"))        // 超时重传时间：3s ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+UARTFT=10\r\n", "OK"))        // 串口字节间隔：10（ms） ——回复判定："OK"
    HAL_Delay(50);	

  while(LORA_SendCmd("AT+MTU=128\r\n", "OK"))        // LoRa 单包长度统一为 128 字节
    HAL_Delay(50);
  
  while(LORA_SendCmd("AT+UART=115200,8,1,NONE,NFC\r\n", "OK"))        // 串口参数设置，流控-NFC ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+Z\r\n", "Start"))        // 重启模块，指令生效 ——回复信息："LoRa Start!"
    HAL_Delay(50);	
  
#endif

  LoRaStreamParser_Init(&g_lora_stream_parser);
  g_lora_last_data_loss_count = 0U;
  g_lora_last_rx_tick = HAL_GetTick();
  g_lora_parser_initialized = 1U;
  g_lora_tx_pending = 0U;
  g_lora_tx_frame_length = 0U;
  LoraTransport_EnableApplicationMode();
}

/*****************************************************
  * 函数名称：	LoraP2PTrans
  * 函数功能：	Lora点对点透传发送数据
  * 入口参数：	无
  * 返回参数：	无
  * 说明：		  
*****************************************************/
void LoraP2PTX(void)
{
  LoRaMessage message;
  LoRaProtocolStatus status;

  if (g_lora_tx_pending == 0U)
  {
    if (MasterQueues_ReceiveLoRa(&message, 0U) != pdPASS)
    {
      return;
    }

    status = LoRaProtocol_Encode(&message,
                                 g_lora_tx_frame,
                                 (uint16_t)sizeof(g_lora_tx_frame),
                                 &g_lora_tx_frame_length);
    if (status != LORA_PROTOCOL_OK)
    {
      LoRaDiag.tx_encode_error_count++;
      return;
    }
    g_lora_tx_pending = 1U;
  }

  if (HAL_UART_Transmit(&huart2,
                        g_lora_tx_frame,
                        g_lora_tx_frame_length,
                        50U) != HAL_OK)
  {
    LoRaDiag.tx_uart_error_count++;
    /* 保留当前完整帧，下个任务周期重试，不能静默丢失ACK/RESULT。 */
    return;
  }
  g_lora_tx_pending = 0U;
  LoRaDiag.tx_frame_count++;
}

/*****************************************************
  * 函数名称：	LoraP2PRX
  * 函数功能：	Lora点对点透传处理接受数据
  * 入口参数：	无
  * 返回参数：	无
  * 说    明：	无
*****************************************************/
void LoraP2PRX(void)
{
  uint8_t rx_byte;
  uint16_t processed = 0U;
  uint32_t data_loss_count;

  if (g_lora_parser_initialized == 0U)
  {
    LoRaStreamParser_Init(&g_lora_stream_parser);
    g_lora_parser_initialized = 1U;
  }

  data_loss_count = LoraTransport_GetDataLossCount();
  if (data_loss_count != g_lora_last_data_loss_count)
  {
    LoRaStreamParser_AbortPartialFrame(&g_lora_stream_parser);
    g_lora_last_data_loss_count = data_loss_count;
  }

  while ((processed < LORA_RX_PROCESS_BUDGET) &&
         (LoraTransport_PopRx(&rx_byte) != 0U))
  {
    LoRaStreamResult result = LoRaStreamParser_PushByte(&g_lora_stream_parser,
                                                        rx_byte,
                                                        &g_lora_received_message);
    g_lora_last_rx_tick = HAL_GetTick();
    if (result == LORA_STREAM_FRAME_READY)
    {
      MasterEvent event;
      MasterIngressRoute route;

      LoRaDiag.last_message_type = g_lora_received_message.type;
      LoRaDiag.last_flow_id = g_lora_received_message.flow_id;
      route = MasterIngress_Route(&g_lora_received_message,
                                  MasterIdentity_GetGroup());
      if (route == MASTER_INGRESS_DROP)
      {
        LoRaDiag.address_filter_drop_count++;
        processed++;
        continue;
      }
      memset(&event, 0, sizeof(event));
      event.type = MASTER_EVENT_LORA_MESSAGE;
      event.data.lora_message = g_lora_received_message;
      if (MasterQueues_SendEvent(&event, 0U) != pdPASS)
      {
        LoRaDiag.event_queue_drop_count++;
      }
    }
    processed++;
  }

  if ((g_lora_stream_parser.length != 0U) &&
      ((uint32_t)(HAL_GetTick() - g_lora_last_rx_tick) >= LORA_FRAME_TIMEOUT_MS))
  {
    LoRaStreamParser_AbortPartialFrame(&g_lora_stream_parser);
  }

  LoRaDiag.accepted_frame_count = g_lora_stream_parser.accepted_frame_count;
  LoRaDiag.rejected_frame_count = g_lora_stream_parser.rejected_frame_count;
  LoRaDiag.aborted_frame_count = g_lora_stream_parser.aborted_frame_count;
  LoRaDiag.data_loss_count = data_loss_count;
}
