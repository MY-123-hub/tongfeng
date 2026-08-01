#include "usart.h"
#include "lora.h"
#include "stdio.h"
#include "string.h"
#include "interrupt.h"


uint16_t LORA_cntPre = 0;


/*****************************************************
  * 函数名称：	LORA_Clear
  * 函数功能：	清空缓存
  * 入口参数：	无
  * 返回参数：	无
  * 说    明：  无
*****************************************************/
void LORA_Clear(void)
{

	memset(Rx2Buffer, 0, sizeof(Rx2Buffer));
	rx2_pointer = 0;

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
	if(rx2_pointer == 0) 							//如果接收计数为0 则说明没有处于接收数据中，所以直接跳出，结束函数
		return REV_WAIT;
	if(rx2_pointer == LORA_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
	{
		rx2_pointer = 0;							//清0接收计数
		return REV_OK;								//返回接收完成标志
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
	unsigned char timeOut = 200;

	Usart_SendString(huart2, (unsigned char *)cmd, strlen((const char *)cmd));
	
	while(timeOut--)
	{
		if(LORA_WaitRecive() == REV_OK)							//如果收到数据
		{
			if(strstr((const char *)Rx2Buffer, res) != NULL)		//如果检索到关键词
			{
				LORA_Clear();									//清空缓存
				
				return 0;
			}
		}
		
		HAL_Delay(10);
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
void LoraP2PTrans(void)
{
  // 发送数据测试
//	LORA_SendData((unsigned char *)"SlaveTestData:123",strlen((const char *)"SlaveTestData:123"));
}







