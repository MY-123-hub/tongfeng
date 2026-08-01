#include "lora.h"


LoRaTypeDef LoRaType = {0};
uint16_t LORA_cntPre = 0;
static char *LORA_lastCommand;
static uint8_t LORA_commandFailures;

#define LORA_MAX_SENSOR_COUNT 6U
#define LORA_COMMAND_RETRY_COUNT 3U


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
  
  while(LORA_SendCmd("AT+ADDR=88\r\n", "OK"))        // 目标地址：88 ——回复判定："OK"
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
            ModbusTx(ModbusType.slave_addr,MudbusFun_writeSingle,modbuswrite_Actioncom,modbuswrite_FwStart);         /* 变频器 正转开始命令 */
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
          ModbusTx(ModbusType.slave_addr,MudbusFun_writeSingle,modbuswrite_Actioncom,modbuswrite_FwStop);          /* 变频器 正转停止命令 */
        }
      }

      LORA_Clear();       /* 清空 Lora 接收数据缓存 */
    }
    else
    {
      LORA_Clear();       /* 丢弃非温度帧，释放接收缓冲区 */
    }
  }
}







