#include "usart.h"
#include "lora.h"
#include "stdio.h"
#include "string.h"
#include "interrupt.h"
#include "dip_switch.h"
#include "slave_protocol_runtime.h"


volatile uint16_t LORA_cntPre = 0;


/*****************************************************
  * �������ƣ�	LORA_Clear
  * �������ܣ�	��ջ���
  * ��ڲ�����	��
  * ���ز�����	��
  * ˵    ����  ��
*****************************************************/
void LORA_Clear(void)
{

	memset(Rx2Buffer, 0, sizeof(Rx2Buffer));
	rx2_pointer = 0;

}


/*****************************************************
  * �������ƣ�	LORA_WaitRecive
  * �������ܣ�	�ȴ��������
  * ��ڲ�����	��
  * ���ز�����	REV_OK-�������		REV_WAIT-���ճ�ʱδ���
  * ˵    ����  ѭ�����ü���Ƿ�������
*****************************************************/
_Bool LORA_WaitRecive(void)
{
	if(rx2_pointer == 0) 							//������ռ���Ϊ0 ��˵��û�д��ڽ��������У�����ֱ����������������
		return REV_WAIT;
	if(rx2_pointer == LORA_cntPre)				//�����һ�ε�ֵ�������ͬ����˵���������
	{
		rx2_pointer = 0;							//��0���ռ���
		return REV_OK;								//���ؽ�����ɱ�־
	}
	LORA_cntPre = rx2_pointer;					//��Ϊ��ͬ
	return REV_WAIT;								//���ؽ���δ��ɱ�־
}


/*****************************************************
  * �������ƣ�	LORA_SendCmd
  * �������ܣ�	��������
  * ��ڲ�����	cmd������
                res����Ҫ���ķ���ָ��
  * ���ز�����	0-�ɹ�	1-ʧ��
  * ˵����		
*****************************************************/
_Bool LORA_SendCmd(char *cmd, char *res)
{
	unsigned char timeOut = 200;

	Usart_SendString(huart2, (unsigned char *)cmd, strlen((const char *)cmd));
	
	while(timeOut--)
	{
		if(LORA_WaitRecive() == REV_OK)							//����յ�����
		{
			if(strstr((const char *)Rx2Buffer, res) != NULL)		//����������ؼ���
			{
				LORA_Clear();									//��ջ���
				
				return 0;
			}
		}
		
		HAL_Delay(10);
	}
	
	return 1;
}


/*****************************************************
  * �������ƣ�	LORA_SendData
  * �������ܣ�	��������
  * ��ڲ�����	data������
                len������
  * ���ز�����	��
  * ˵����		
*****************************************************/
void LORA_SendData(unsigned char *data, unsigned short len)
{
	LORA_Clear();								//��ս��ջ���
  
  Usart_SendString(huart2, data, len);		//�����豸������������
}


/*****************************************************
  * �������ƣ�	LORA_Init
  * �������ܣ�	����LORA����
  * ��ڲ�����	��
  * ���ز�����	��
  * ˵����		
*****************************************************/
void LORA_Init(void)
{
	LORA_Clear();       // ������� 2 ��������
  
#if 1   // ���õ�Ե�͸��ͨѶ������������һ�Σ�����ģ�������Ч�������ݼ��ɣ��͹���ģʽ��˵
  
	LORA_SendCmd("AT+ENTM\r\n", "");      // �˳�����ģʽ�����½�������ģʽ������Ῠס����Ϊ������ģʽ�в����Ӧ�������õ�������º�������ס

	while(LORA_SendCmd("+++", "a"))       // �����������ģʽ "+++"�����ظ��ж���"a"
    HAL_Delay(50);
	
  while(LORA_SendCmd("a", "OK"))        // �����������ģʽ "a"�����ظ��ж���"OK"
    HAL_Delay(50);	

  while(LORA_SendCmd("AT+WMODE=TRANS\r\n", "OK"))        // Э������Ϊ͸��ģʽ �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+PMODE=RUN\r\n", "OK"))        // ����ģʽ����Ϊ ��RUN�� �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+ITM=20\r\n", "OK"))        // ����ʱ������Ϊ 20��s�� �����ظ��ж���"OK"���˲����� RUN��LSR ģʽ��Ч
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+WTM=2000\r\n", "OK"))        // ����ʱ�䣺2000��ms�� �����ظ��ж���"OK"���˲����� RUN��LSR ģʽ��Ч
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+RTO=500\r\n", "OK"))        // ���ճ�ʱ��500��ms�� �����ظ��ж���"OK"������ LR/LSR ģʽ����Ч
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+FDMODE=OFF\r\n", "OK"))        // �ر������з�Ƶ �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+CH=4700\r\n", "OK"))        // ����/�����ŵ�����Ϊ��4700 �����ظ��ж���"OK"������Ƶ�Σ�(398+ch)MHz
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+SPD=10\r\n", "OK"))        // ���ʣ�10 �����ظ��ж���"OK"��10: 21875bps
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+PWR=22\r\n", "OK"))        // ���书�ʣ�22 �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+FEC=1\r\n", "OK"))        // ǰ��������ر� �����ظ��ж���"OK"�����������ݴ�������ȶ�������ͨ������
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+LBT=OFF\r\n", "OK"))        // LBT���ر� �����ظ��ж���"OK"�������� LoRa ����ǰ�����ŵ�״̬
    HAL_Delay(50);	
  
  char cmd_addr[16];
  sprintf(cmd_addr, "AT+ADDR=%d\r\n", DIP_Switch_Read());
  while(LORA_SendCmd(cmd_addr, "OK"))        // Ŀ���ַ��88 �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+LRTO=3\r\n", "OK"))        // ��ʱ�ش�ʱ�䣺3s �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+UARTFT=10\r\n", "OK"))        // �����ֽڼ����10��ms�� �����ظ��ж���"OK"
    HAL_Delay(50); 

  while(LORA_SendCmd("AT+MTU=128\r\n", "OK"))          // 36点温度帧85字节，统一使用128字节包长
    HAL_Delay(50);
  
  while(LORA_SendCmd("AT+UART=115200,8,1,NONE,NFC\r\n", "OK"))        // ���ڲ������ã�����-NFC �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+Z\r\n", "Start"))        // ����ģ�飬ָ����Ч �����ظ���Ϣ��"LoRa Start!"
    HAL_Delay(50);	
  
#endif

  /* 配置阶段仍用旧缓冲等待 AT 响应；从这里起切换到二进制协议接收环。 */
  SlaveRuntime_Init(DIP_Switch_Read());
}

/*****************************************************
  * �������ƣ�	LoraP2PTrans
  * �������ܣ�	Lora��Ե�͸����������
  * ��ڲ�����	��
  * ���ز�����	��
  * ˵����		
*****************************************************/
void LoraP2PTrans(void)
{
  SlaveRuntime_Process(HAL_GetTick());
}







