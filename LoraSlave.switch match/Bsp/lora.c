#include "usart.h"
#include "lora.h"
#include "stdio.h"
#include "string.h"
#include "interrupt.h"
volatile uint16_t LORA_cntPre = 0;
volatile uint32_t LoraSlavePollRxCount;
volatile uint32_t LoraSlaveTemperatureTxCount;

#define LORA_MASTER_GROUP 0x01U
#define LORA_SLAVE_REPLY_DELAY_MS 50U

static uint8_t s_protocol_rx_buffer[LORA_PROTOCOL_FRAME_MAX_SIZE];
static volatile uint8_t s_protocol_rx_index;
static volatile uint8_t s_protocol_rx_expected;
static volatile uint8_t s_protocol_rx_ready;
static int16_t s_slave_temperature[LORA_PROTOCOL_TEMPERATURE_COUNT];
static uint8_t s_slave_temperature_valid;
static uint8_t s_slave_reply_payload[LORA_PROTOCOL_TEMPERATURE_BYTES];
static uint16_t s_slave_reply_flow_id;
static uint32_t s_slave_reply_due_ms;
static uint8_t s_slave_reply_pending;

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

static void LoraProtocolSendFrame(uint8_t type, uint8_t src_role, uint8_t src_group,
    uint8_t dst_role, uint8_t dst_group, uint16_t flow_id,
    const uint8_t *data, uint8_t data_len)
{
    uint8_t frame[LORA_PROTOCOL_FRAME_MAX_SIZE];
    uint16_t crc;
    uint16_t total_length;

    if (data_len > LORA_PROTOCOL_MAX_PAYLOAD)
    {
        return;
    }

    frame[0] = LORA_PROTOCOL_HEAD_0;
    frame[1] = LORA_PROTOCOL_HEAD_1;
    frame[2] = LORA_PROTOCOL_VERSION;
    frame[3] = type;
    frame[4] = src_role;
    frame[5] = src_group;
    frame[6] = dst_role;
    frame[7] = dst_group;
    frame[8] = (uint8_t)(flow_id & 0x00FFU);
    frame[9] = (uint8_t)(flow_id >> 8U);
    frame[10] = data_len;
    if ((data_len > 0U) && (data != NULL))
    {
        memcpy(&frame[LORA_PROTOCOL_HEADER_SIZE], data, data_len);
    }

    total_length = LORA_PROTOCOL_HEADER_SIZE + data_len;
    crc = LoraProtocolCrc16(frame, total_length);
    frame[total_length] = (uint8_t)(crc & 0x00FFU);
    frame[total_length + 1U] = (uint8_t)(crc >> 8U);
    HAL_UART_Transmit(&huart2, frame, total_length + LORA_PROTOCOL_CRC_SIZE, 100U);
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

static void LoraSlavePrepareTemperatureCache(void)
{
    uint8_t index;

    if (s_slave_temperature_valid != 0U)
    {
        return;
    }

    for (index = 0U; index < LORA_PROTOCOL_TEMPERATURE_COUNT; index++)
    {
        s_slave_temperature[index] = LORA_PROTOCOL_TEMPERATURE_INVALID;
    }
    s_slave_temperature_valid = 1U;
}


/*****************************************************
  * �������ƣ�	LORA_Clear
  * �������ܣ�	��ջ���
  * ��ڲ�����	��
  * ���ز�����	��
  * ˵    ����  ��
*****************************************************/
void LORA_Clear(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	memset(Rx2Buffer, 0, sizeof(Rx2Buffer));
	rx2_pointer = 0;
	LORA_cntPre = 0U;
	if (primask == 0U)
	{
		__enable_irq();
	}
	LoraProtocolResetReceiver();

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
  
  while(LORA_SendCmd("AT+ADDR=88\r\n", "OK"))        // 联调阶段三端固定为同一地址 88
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+LRTO=3\r\n", "OK"))        // ��ʱ�ش�ʱ�䣺3s �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+UARTFT=10\r\n", "OK"))        // �����ֽڼ����10��ms�� �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+UART=115200,8,1,NONE,NFC\r\n", "OK"))        // ���ڲ������ã�����-NFC �����ظ��ж���"OK"
    HAL_Delay(50);	
  
  while(LORA_SendCmd("AT+Z\r\n", "Start"))        // ����ģ�飬ָ����Ч �����ظ���Ϣ��"LoRa Start!"
    HAL_Delay(50);	
  
#endif
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
  // �������ݲ���
//	LORA_SendData((unsigned char *)"SlaveTestData:123",strlen((const char *)"SlaveTestData:123"));
}

void LoraSlaveUpdateTemperatureCache(const int16_t temperature[LORA_PROTOCOL_TEMPERATURE_COUNT])
{
    uint8_t index;

    if (temperature == NULL)
    {
        return;
    }

    for (index = 0U; index < LORA_PROTOCOL_TEMPERATURE_COUNT; index++)
    {
        s_slave_temperature[index] = temperature[index];
    }
    s_slave_temperature_valid = 1U;
}

void LoraSlaveProcess(void)
{
    LoraProtocolFrame frame;
    uint8_t index;

    if ((s_slave_reply_pending != 0U) &&
        ((int32_t)(HAL_GetTick() - s_slave_reply_due_ms) >= 0))
    {
        LoraProtocolSendFrame(LORA_TYPE_TEMP_36,
            LORA_ROLE_SLAVE, LORA_MASTER_GROUP,
            LORA_ROLE_MASTER, LORA_MASTER_GROUP,
            s_slave_reply_flow_id, s_slave_reply_payload,
            LORA_PROTOCOL_TEMPERATURE_BYTES);
        LoraSlaveTemperatureTxCount++;
        s_slave_reply_pending = 0U;
    }

    if (LoraProtocolTakeFrame(&frame) == 0U)
    {
        return;
    }

    /* 当前试验只响应主机的“读取缓存温度”请求（mode = 0）。 */
    if ((frame.type != LORA_TYPE_READ_TEMP) ||
        (frame.src_role != LORA_ROLE_MASTER) ||
        (frame.src_group != LORA_MASTER_GROUP) ||
        (frame.dst_role != LORA_ROLE_SLAVE) ||
        (frame.dst_group != LORA_MASTER_GROUP) ||
        (frame.data_len != 1U) || (frame.data[0] != 0U))
    {
        return;
    }

    LoraSlavePrepareTemperatureCache();
	LoraSlavePollRxCount++;
    if (s_slave_reply_pending != 0U)
    {
        return;
    }
    for (index = 0U; index < LORA_PROTOCOL_TEMPERATURE_COUNT; index++)
    {
        s_slave_reply_payload[index * 2U] =
            (uint8_t)((uint16_t)s_slave_temperature[index] & 0x00FFU);
        s_slave_reply_payload[index * 2U + 1U] =
            (uint8_t)((uint16_t)s_slave_temperature[index] >> 8U);
    }

    s_slave_reply_flow_id = frame.flow_id;
    s_slave_reply_due_ms = HAL_GetTick() + LORA_SLAVE_REPLY_DELAY_MS;
    s_slave_reply_pending = 1U;
}







