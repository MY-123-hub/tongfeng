#include "lora.h"


#include "dip_swich.h"
#include <stdarg.h>

LoRaTypeDef LoRaType = {0};
volatile uint32_t LoraControlTemperatureRxCount;
volatile uint32_t LoraControlUart1TxCount;
volatile uint32_t LoraControlUart1TxErrorCount;
uint16_t LORA_cntPre = 0;
static char *LORA_lastCommand;
static uint8_t LORA_commandFailures;

#define LORA_MAX_SENSOR_COUNT 6U
#define LORA_COMMAND_RETRY_COUNT 3U

#define LORA_MASTER_GROUP 0x01U
#define LORA_CONTROL_GROUP 0x00U
#define LORA_CONTROL_UART1_TEXT_SIZE 1024U

#define UTF8_MASTER    "\xE4\xB8\xBB\xE6\x9C\xBA"
#define UTF8_SLAVE     "\xE4\xBB\x8E\xE6\x9C\xBA"
#define UTF8_DATA_TYPE "\xE6\x95\xB0\xE6\x8D\xAE\xE7\xB1\xBB\xE5\x9E\x8B"
#define UTF8_PATH      "\xE8\xB7\xAF"
#define UTF8_TEMP      "\xE6\xB8\xA9\xE5\xBA\xA6"
#define UTF8_FLOW      "\xE6\xB5\x81\xE6\xB0\xB4\xE5\x8F\xB7"
#define UTF8_PORT      "\xE7\xAB\xAF\xE5\x8F\xA3"
#define UTF8_INVALID   "\xE6\x97\xA0\xE6\x95\x88"
#define UTF8_PACKET    "\xE6\x95\xB0\xE6\x8D\xAE\xE5\x8C\x85"
#define UTF8_BEGIN     "\xE5\xBC\x80\xE5\xA7\x8B"
#define UTF8_END       "\xE7\xBB\x93\xE6\x9D\x9F"
#define UTF8_PROBE     "\xE6\x8E\xA2\xE5\xA4\xB4"
#define UTF8_CELSIUS   "\xE2\x84\x83"

static uint8_t s_protocol_rx_buffer[LORA_PROTOCOL_FRAME_MAX_SIZE];
static volatile uint8_t s_protocol_rx_index;
static volatile uint8_t s_protocol_rx_expected;
static volatile uint8_t s_protocol_rx_ready;
static int16_t s_control_temperature[LORA_PROTOCOL_TEMPERATURE_COUNT];
static char s_control_uart1_text[LORA_CONTROL_UART1_TEXT_SIZE];

static void LoraProtocolResetReceiver(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	s_protocol_rx_index = 0U;
	s_protocol_rx_expected = 0U;
	s_protocol_rx_ready = 0U;
	if (primask == 0U)
	{
		__enable_irq();
	}
}

static uint16_t LoraProtocolCrc16(const uint8_t *data, uint16_t length)
{
	uint16_t crc = 0xFFFFU;
	uint16_t index;
	uint8_t bit;

	for (index = 0U; index < length; index++)
	{
		crc ^= data[index];
		for (bit = 0U; bit < 8U; bit++)
		{
			if ((crc & 0x0001U) != 0U)
			{
				crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
			}
			else
			{
				crc >>= 1U;
			}
		}
	}

	return crc;
}

void LoraProtocolFeedByte(uint8_t data)
{
	if (s_protocol_rx_ready != 0U)
	{
		return;
	}

	if (s_protocol_rx_index == 0U)
	{
		if (data == LORA_PROTOCOL_HEAD_0)
		{
			s_protocol_rx_buffer[s_protocol_rx_index++] = data;
		}
		return;
	}

	if (s_protocol_rx_index == 1U)
	{
		if (data == LORA_PROTOCOL_HEAD_1)
		{
			s_protocol_rx_buffer[s_protocol_rx_index++] = data;
		}
		else if (data == LORA_PROTOCOL_HEAD_0)
		{
			s_protocol_rx_buffer[0] = data;
		}
		else
		{
			s_protocol_rx_index = 0U;
		}
		return;
	}

	if (s_protocol_rx_index >= LORA_PROTOCOL_FRAME_MAX_SIZE)
	{
		s_protocol_rx_index = 0U;
		s_protocol_rx_expected = 0U;
		return;
	}

	s_protocol_rx_buffer[s_protocol_rx_index++] = data;
	if (s_protocol_rx_index == LORA_PROTOCOL_HEADER_SIZE)
	{
		if (s_protocol_rx_buffer[10] > LORA_PROTOCOL_MAX_PAYLOAD)
		{
			s_protocol_rx_index = 0U;
			return;
		}
		s_protocol_rx_expected = (uint8_t)(LORA_PROTOCOL_HEADER_SIZE +
			s_protocol_rx_buffer[10] + LORA_PROTOCOL_CRC_SIZE);
	}

	if ((s_protocol_rx_expected != 0U) &&
		(s_protocol_rx_index == s_protocol_rx_expected))
	{
		s_protocol_rx_ready = 1U;
	}
}

static uint8_t LoraProtocolTakeFrame(LoraProtocolFrame *frame)
{
	uint8_t data_len;
	uint16_t crc_received;
	uint16_t crc_calculated;
	uint16_t frame_length;
	uint8_t valid = 0U;

	if ((frame == NULL) || (s_protocol_rx_ready == 0U))
	{
		return 0U;
	}

	data_len = s_protocol_rx_buffer[10];
	frame_length = LORA_PROTOCOL_HEADER_SIZE + data_len;
	if ((data_len <= LORA_PROTOCOL_MAX_PAYLOAD) &&
		(s_protocol_rx_expected == (frame_length + LORA_PROTOCOL_CRC_SIZE)) &&
		(s_protocol_rx_buffer[2] == LORA_PROTOCOL_VERSION))
	{
		crc_received = (uint16_t)s_protocol_rx_buffer[frame_length] |
			((uint16_t)s_protocol_rx_buffer[frame_length + 1U] << 8U);
		crc_calculated = LoraProtocolCrc16(s_protocol_rx_buffer, frame_length);
		if (crc_received == crc_calculated)
		{
			frame->type = s_protocol_rx_buffer[3];
			frame->src_role = s_protocol_rx_buffer[4];
			frame->src_group = s_protocol_rx_buffer[5];
			frame->dst_role = s_protocol_rx_buffer[6];
			frame->dst_group = s_protocol_rx_buffer[7];
			frame->flow_id = (uint16_t)s_protocol_rx_buffer[8] |
				((uint16_t)s_protocol_rx_buffer[9] << 8U);
			frame->data_len = data_len;
			if (data_len > 0U)
			{
				memcpy(frame->data, &s_protocol_rx_buffer[LORA_PROTOCOL_HEADER_SIZE], data_len);
			}
			valid = 1U;
		}
	}

	LoraProtocolResetReceiver();
	return valid;
}

static void LoraControlStoreTemperature(const uint8_t data[LORA_PROTOCOL_TEMPERATURE_BYTES])
{
	uint8_t index;

	for (index = 0U; index < LORA_PROTOCOL_TEMPERATURE_COUNT; index++)
	{
		s_control_temperature[index] = (int16_t)((uint16_t)data[index * 2U] |
			((uint16_t)data[index * 2U + 1U] << 8U));
	}

	/* 兼容现有显示任务，仅更新第 1 个通道 6 个温度。 */
	LoRaType.DS18B20_PORT = 0;
	LoRaType.DS18B20_NUM = LORA_MAX_SENSOR_COUNT;
	for (index = 0U; index < LORA_MAX_SENSOR_COUNT; index++)
	{
		LoRaType.DS18B20_Data[index] = s_control_temperature[index];
	}
}

static uint16_t LoraControlTextAppend(uint16_t offset, const char *format, ...)
{
	va_list args;
	size_t remaining;
	int written;

	if ((format == NULL) || (offset >= LORA_CONTROL_UART1_TEXT_SIZE))
	{
		return offset;
	}

	remaining = LORA_CONTROL_UART1_TEXT_SIZE - offset;
	va_start(args, format);
	written = vsnprintf(&s_control_uart1_text[offset], remaining, format, args);
	va_end(args);

	if (written < 0)
	{
		return offset;
	}
	if ((size_t)written >= remaining)
	{
		return LORA_CONTROL_UART1_TEXT_SIZE - 1U;
	}

	return (uint16_t)(offset + (uint16_t)written);
}

static uint16_t LoraControlAppendTemperature(uint16_t offset, int16_t temperature)
{
	int32_t magnitude;

	if (temperature == LORA_PROTOCOL_TEMPERATURE_INVALID)
	{
		return LoraControlTextAppend(offset, UTF8_INVALID);
	}

	magnitude = (int32_t)temperature;
	if (magnitude < 0)
	{
		magnitude = -magnitude;
		return LoraControlTextAppend(offset, "-%ld.%ld" UTF8_CELSIUS,
			(long)(magnitude / 10L), (long)(magnitude % 10L));
	}

	return LoraControlTextAppend(offset, "%ld.%ld" UTF8_CELSIUS,
		(long)(magnitude / 10L), (long)(magnitude % 10L));
}

static uint16_t LoraControlBuildUart1Text(const LoraProtocolFrame *frame)
{
	uint16_t offset = 0U;
	uint8_t port;
	uint8_t probe;
	uint8_t temperature_index;

	if (frame == NULL)
	{
		return 0U;
	}

	s_control_uart1_text[0] = '\0';
	offset = LoraControlTextAppend(offset, "[" UTF8_PACKET UTF8_BEGIN "]\r\n");
	/* 当前架构中 Mx 与 Sx 一一配对，因此从机编号与主机组号相同。 */
	offset = LoraControlTextAppend(offset,
		UTF8_MASTER ":M%u," UTF8_SLAVE ":S%u,"
		UTF8_DATA_TYPE ":36" UTF8_PATH UTF8_TEMP ","
		UTF8_FLOW ":%u\r\n",
		(unsigned int)frame->src_group,
		(unsigned int)frame->src_group,
		(unsigned int)frame->flow_id);

	for (port = 0U; port < 6U; port++)
	{
		offset = LoraControlTextAppend(offset, UTF8_PORT "%u:",
			(unsigned int)(port + 1U));
		for (probe = 0U; probe < 6U; probe++)
		{
			temperature_index = (uint8_t)(port * 6U + probe);
			offset = LoraControlTextAppend(offset, UTF8_PROBE "%u=",
				(unsigned int)(probe + 1U));
			offset = LoraControlAppendTemperature(offset,
				s_control_temperature[temperature_index]);
			if (probe < 5U)
			{
				offset = LoraControlTextAppend(offset, ",");
			}
		}
		offset = LoraControlTextAppend(offset, "\r\n");
	}

	offset = LoraControlTextAppend(offset, "[" UTF8_PACKET UTF8_END "]\r\n\r\n");
	return offset;
}


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
	LoraProtocolResetReceiver();
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
  
  /* 联调阶段控制室地址固定为 88，必须与主机保持一致。 */
  while(LORA_SendCmd("AT+ADDR=88\r\n", "OK"))
    HAL_Delay(50);
  
  while(LORA_SendCmd("AT+LRTO=3\r\n", "OK"))        // 超时重传时间：3s ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+UARTFT=10\r\n", "OK"))        // 串口字节间隔：10（ms） ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+UART=115200,8,1,NONE,NFC\r\n", "OK"))        // 串口参数设置，流控-NFC ——回复判定："OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+Z\r\n", "Start"))        // 重启模块，指令生效 ——回复信息："LoRa Start!"
    HAL_Delay(50);	
  
#endif
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
	LoraProtocolFrame frame;

	if (LoraProtocolTakeFrame(&frame) != 0U)
	{
		if ((frame.type == LORA_TYPE_TEMP_36) &&
			(frame.src_role == LORA_ROLE_MASTER) &&
			(frame.src_group == LORA_MASTER_GROUP) &&
			(frame.dst_role == LORA_ROLE_CONTROL) &&
			(frame.dst_group == LORA_CONTROL_GROUP) &&
			(frame.data_len == LORA_PROTOCOL_TEMPERATURE_BYTES))
		{
			uint16_t text_length;

			LoraControlStoreTemperature(frame.data);
			LoraControlTemperatureRxCount++;
			text_length = LoraControlBuildUart1Text(&frame);
			/* USART1/PA9 输出 UTF-8 中文文本，串口助手使用文本模式。 */
			if ((text_length > 0U) &&
				(HAL_UART_Transmit(&huart1, (uint8_t *)s_control_uart1_text,
				text_length, 200U) == HAL_OK))
			{
				LoraControlUart1TxCount++;
			}
			else
			{
				LoraControlUart1TxErrorCount++;
			}
		}
	}

#if 0 /* 旧文本报文和自动通风控制已停用，保留仅供历史对照。 */
  int Tdata[17] = {0};
  int parse_count;
  
  if(LORA_WaitRecive() == REV_OK)							//如果收到数据
  {
    if(strstr((const char *)Rx2Buffer, "TM") != NULL)		//如果检索到关键词:TM——温度数据关键词
    {
      
      /* 处理接受的 Lora 温度数据——作数据整形处理 */ 
      // 数据格式对比： PORT:00,NUM:04,TM:26.8/27.0/27.0/26.8/0.0/0.0,DHT11_H:60.0,DHT11_T:27.7,Pressure:31
      parse_count = sscanf((const char *)Rx2Buffer,"PORT:%02d,NUM:%02d,TM:%d.%d/%d.%d/%d.%d/%d.%d/%d.%d/%d.%d,DHT11_H:%d.%d,DHT11_T:%d.%d,Pressure:%d",&LoRaType.DS18B20_PORT,&LoRaType.DS18B20_NUM,
              &Tdata[0],&Tdata[1],&Tdata[2],&Tdata[3],&Tdata[4],&Tdata[5],&Tdata[6],&Tdata[7],&Tdata[8],&Tdata[9],&Tdata[10],&Tdata[11],
              &Tdata[12],&Tdata[13],&Tdata[14],&Tdata[15],&Tdata[16]);
      if ((parse_count != 19) || (LoRaType.DS18B20_NUM <= 0) ||
          (LoRaType.DS18B20_NUM > (int)LORA_MAX_SENSOR_COUNT))
      {
        LORA_Clear();
        return;
      }
      for(uint8_t i=0;i<6;i++){LoRaType.DS18B20_Data[i]=Tdata[i*2]*10+Tdata[i*2+1];}
      LoRaType.DHT11_Humi=Tdata[12]*10+Tdata[13];
      LoRaType.DHT11_Temp=Tdata[14]*10+Tdata[15];
      LoRaType.WindPressure=Tdata[16]*10;

#if 0     //!!!调试后关闭
      /* 打印查看——调试 */ 
      HAL_UART_Transmit(&huart2,(const unsigned char *)Rx2Buffer,strlen((const char *)Rx2Buffer),50);      //打印原数据
      printf("端口:%02d,数量:%02d,温度:%d %d %d %d %d %d DHT11_Humi:%d DHT11_Temp:%d \r\n \r\n",LoRaType.DS18B20_PORT,LoRaType.DS18B20_NUM,
        LoRaType.DS18B20_Data[0],LoRaType.DS18B20_Data[1],LoRaType.DS18B20_Data[2],LoRaType.DS18B20_Data[3],
        LoRaType.DS18B20_Data[4],LoRaType.DS18B20_Data[5],LoRaType.DHT11_Humi,LoRaType.DHT11_Temp);  
        printf("\r\n设定温度：%d \r\n",SysVariType.vent_temp);      
#endif
      
      /********************* 通风条件判断 ***********************/ 
      if(SysVariType.vent_open_flag==0)    /* 通风关闭状态-开启条件判断 */
      {
        SysVariType.vent_temp_outmax_num = 0;      /* 每个有效报文独立统计异常温度点 */
        /* 逐个判断温度点是否异常 */
        for(uint8_t i = 0U; i < (uint8_t)LoRaType.DS18B20_NUM; i++)
        {
          if(LoRaType.DS18B20_Data[i] > SysVariType.vent_temp) SysVariType.vent_temp_outmax_num++;    /* 超过三个温度点大于设定温度——开启通风 */ 
          if(SysVariType.vent_temp_outmax_num>=3)
          {
            ModbusTxVfdCmd(modbuswrite_RunFwd, VFD_TARGET_FREQ_X100);         /* 变频器 正转运行命令 */
            break;  /* ModbusTx 已将该事务标为在途，禁止同一帧重复发命令 */
          }
        }
      }
      else               /* 通风开启状态-关闭条件判断 */
      {
        uint8_t all_below_threshold = 1U;
        for(uint8_t i = 0U; i < (uint8_t)LoRaType.DS18B20_NUM; i++)
        {
          if(LoRaType.DS18B20_Data[i] > SysVariType.vent_temp)
          {
            all_below_threshold = 0U;
            break;
          }
        }
        if (all_below_threshold != 0U)
        {
          ModbusTxVfdCmd(modbuswrite_StopDec, VFD_TARGET_FREQ_X100);          /* 变频器 减速停机命令 */
        }
      }

      LORA_Clear();       /* 清空 Lora 接收数据缓存 */
    }
    else
    {
      LORA_Clear();       /* 丢弃非温度帧，释放接收缓冲区 */
    }
  }
#endif
}







