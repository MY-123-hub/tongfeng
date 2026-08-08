#include "bsp_ds18b20.h"
#include "core_delay.h" 
#include "stdio.h" 


unsigned char DS18B20_ID[6][MaxSensorNum][8];
unsigned char DS18B20_SensorNum[6];
 
// ����DS18B20�õ���I/O��
void DS18B20_GPIO_Config(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	/*����һ��GPIO_InitTypeDef���͵Ľṹ��*/
  GPIO_InitTypeDef GPIO_InitStruct = {0};

	/*gpio.c���ѿ���GPIOA����ʱ��*/
 
  GPIO_InitStruct.Pin = PINx;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
}
 
// ��������
void DS18B20_Mode_IPU(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	/*����һ��GPIO_InitTypeDef���͵Ľṹ��*/
  GPIO_InitTypeDef GPIO_InitStruct = {0};
 
  GPIO_InitStruct.Pin = PINx;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}
 
// �������
void DS18B20_Mode_Out(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	/*����һ��GPIO_InitTypeDef���͵Ľṹ��*/
  GPIO_InitTypeDef GPIO_InitStruct = {0};
 
  GPIO_InitStruct.Pin = PINx;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}
 
// ��λ���������ӻ����͸�λ����
void DS18B20_Rst(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	DS18B20_Mode_Out(GPIOx,PINx);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);		// ��������480us�ĵ͵�ƽ��λ�ź�
	DS18B20_DELAY_US(480);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);;	// �ڲ�����λ�źź��轫��������
	DS18B20_DELAY_US(15);
}
 
// ���ӻ����������ص�Ӧ�����塣�ӻ����յ������ĸ�λ�źź󣬻���15~60us���������һ��Ӧ������
uint8_t DS18B20_Answer_Check(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	uint8_t delay = 0;
	DS18B20_Mode_IPU(GPIOx,PINx); // ��������Ϊ��������
	// �ȴ�Ӧ�����壨һ��60~240us�ĵ͵�ƽ�ź� ���ĵ���
	// ���100us�ڣ�û��Ӧ�����壬�˳�������ע�⣺�ӻ����յ������ĸ�λ�źź󣬻���15~60us���������һ����������
	while (HAL_GPIO_ReadPin(GPIOx,PINx)&&delay < 100)
	{
		delay++;
		DS18B20_DELAY_US(1);
	}
	// ����100us�����û��Ӧ�����壬�˳�����
	if (delay >= 100)//Hu200
		return 1;
	else
		delay = 0;
	// ��Ӧ�����壬�Ҵ���ʱ�䲻����240us
	while (!HAL_GPIO_ReadPin(GPIOx,PINx)&&delay < 240)
	{
		delay++;
		DS18B20_DELAY_US(1);
	}
	if (delay >= 240)
		return 1;
	return 0;
}
 
// ��DS18B20��ȡ1��λ
uint8_t DS18B20_Read_Bit(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	uint8_t data;
	DS18B20_Mode_Out(GPIOx,PINx);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET); // ��ʱ�����ʼ���������������� >1us <15us �ĵ͵�ƽ�ź�
	DS18B20_DELAY_US(2);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
	DS18B20_DELAY_US(12);
	DS18B20_Mode_IPU(GPIOx,PINx);// ���ó����룬�ͷ����ߣ����ⲿ�������轫��������
	if (HAL_GPIO_ReadPin(GPIOx,PINx))
		data = 1;
	else
		data = 0;
	DS18B20_DELAY_US(50);
	return data;
}
 
// ��DS18B20��ȡ2��λ
uint8_t DS18B20_Read_2Bit(GPIO_TypeDef * GPIOx,uint16_t PINx)//����λ �ӳ���
{
	uint8_t i;
	uint8_t dat = 0;
	for (i = 2; i > 0; i--)
	{
		dat = dat << 1;
		DS18B20_Mode_Out(GPIOx,PINx);
		HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);
		DS18B20_DELAY_US(2);
		HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
		DS18B20_Mode_IPU(GPIOx,PINx);
		DS18B20_DELAY_US(12);
		if (HAL_GPIO_ReadPin(GPIOx,PINx))	dat |= 0x01;
		DS18B20_DELAY_US(50);
	}
	return dat;
}
 
// ��DS18B20��ȡ1���ֽ�
uint8_t DS18B20_Read_Byte(GPIO_TypeDef * GPIOx,uint16_t PINx)	// read one byte
{
	uint8_t i, j, dat;
	dat = 0;
	for (i = 0; i < 8; i++)
	{
		j = DS18B20_Read_Bit(GPIOx,PINx);
		dat = (dat) | (j << i);
	}
	return dat;
}
 
// д1λ��DS18B20
void DS18B20_Write_Bit(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t dat)
{
	DS18B20_Mode_Out(GPIOx,PINx);
	if (dat)
	{
		HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// Write 1
		DS18B20_DELAY_US(2);
		HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
		DS18B20_DELAY_US(60);
	}
	else
	{
		HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// Write 0
		DS18B20_DELAY_US(60);
		HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
		DS18B20_DELAY_US(2);
	}
}
 
// д1�ֽڵ�DS18B20
void DS18B20_Write_Byte(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t dat)
{
	uint8_t j;
	uint8_t testb;
	DS18B20_Mode_Out(GPIOx,PINx);
	for (j = 1; j <= 8; j++)
	{
		testb = dat & 0x01;
		dat = dat >> 1;
    HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// ����2us������д״̬
    DS18B20_DELAY_US(9);
		if (testb)
		{
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// д1
			DS18B20_DELAY_US(10);
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
			DS18B20_DELAY_US(50);
		}
		else
		{
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// д0
			DS18B20_DELAY_US(60);
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);// �ͷ�����
			DS18B20_DELAY_US(2);
		}
	}
    DS18B20_DELAY_US(100);
    HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
    DS18B20_DELAY_US(4);
}
 
//��ʼ��DS18B20��IO�ڣ�ͬʱ���DS�Ĵ���
uint8_t DS18B20_Init(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	DS18B20_GPIO_Config(GPIOx,PINx);
	DS18B20_Rst(GPIOx,PINx);
	return DS18B20_Answer_Check(GPIOx,PINx);
}
 
// ��ds18b20�õ��¶�ֵ�����ȣ�0.1C�������¶�ֵ��-550~1250����Temperature1���ظ���ʵ���¶�
uint8_t crc_t[10];
float DS18B20_Get_Temp(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t channel, uint8_t i)
{
	uint8_t j;//ƥ����ֽ�
	uint8_t TL, TH;
	short Temperature;
	float Temperature1;
  
  /* ������Դģʽ������ȡ�¶� */
	DS18B20_Rst(GPIOx,PINx);                            // ��λ
	DS18B20_Answer_Check(GPIOx,PINx);                   // ���ܴ�������-�ȴ��ӻ�׼����
//	DS18B20_Write_Byte(GPIOx,PINx,0xcc);              // ������Դ����˴�����ʹ�á����� ROM�������Ҫ���͡�ƥ��ROM������
	DS18B20_Write_Byte(GPIOx,PINx,0x55);                // ���͡�����ROM�����������ָ���豸
	for (j = 0; j < 8; j++)DS18B20_Write_Byte(GPIOx,PINx,DS18B20_ID[channel][i][j]);// �����豸 ID ��
	DS18B20_Write_Byte(GPIOx,PINx,0x44);                // ����ת������
  HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);         // ת������ͺ󣬼����������ǿ����������������750000us���ṩ��ֵ��ܹ�����¶�ת��
  DS18B20_DELAY_US(750000);                           // ת������ͺ󣬼����������ǿ����������������750000us���ṩ��ֵ��ܹ�����¶�ת��
	DS18B20_Rst(GPIOx,PINx);                            // ��λ
	DS18B20_Answer_Check(GPIOx,PINx);                   // ���ܴ�������-�ȴ��ӻ�׼����
	DS18B20_Write_Byte(GPIOx,PINx,0x55);                // ���͡�����ROM�����������ָ���豸
	for (j = 0; j < 8; j++)DS18B20_Write_Byte(GPIOx,PINx,DS18B20_ID[channel][i][j]);// �����豸 ID ��
	DS18B20_Write_Byte(GPIOx,PINx,0xbe);                // ���͡���ȡ�м����Ĵ��������� 
	TL = DS18B20_Read_Byte(GPIOx,PINx);                 // LSB   
	TH = DS18B20_Read_Byte(GPIOx,PINx);                 // MSB  
  crc_t[0] = TL;                                      // CRC
  crc_t[1] = TH;                                      // CRC
  crc_t[2] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[3] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[4] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[5] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[6] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[7] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[8] = DS18B20_Read_Byte(GPIOx,PINx);           // CRC
  crc_t[9] = DS18B20_Crc(crc_t,8);
  DS18B20_Rst(GPIOx,PINx);                            // ��λ
	DS18B20_Answer_Check(GPIOx,PINx);                   // ���ܴ�������-�ȴ��ӻ�׼����

  /* �������� */
	if (TH & 0xfc)
	{
		Temperature = (TH << 8) | TL;
		Temperature1 = (~Temperature) + 1;
		Temperature1 *= 0.0625;
	}
	else
	{
      Temperature1 = ((TH << 8) | TL)*0.0625;
	}
	return Temperature1;
}
 
// �Զ�����ROM
void DS18B20_Search_Rom(GPIO_TypeDef * GPIOx,uint16_t PINx, uint8_t channel)
{
	uint8_t k, l, chongtuwei, m, n, num;
	uint8_t zhan[MaxSensorNum-1]={0};
	uint8_t ss[64];
	uint8_t tempp;
	l = 0;
	num = 0;
	do
	{
		DS18B20_Rst(GPIOx,PINx); //ע�⣺��λ����ʱ����
		DS18B20_DELAY_US(480); //480��720
		DS18B20_Write_Byte(GPIOx,PINx,0xf0);
		for (m = 0; m < 8; m++)
		{
			uint8_t s = 0;
			for (n = 0; n < 8; n++)
			{
				k = DS18B20_Read_2Bit(GPIOx,PINx);//����λ����
 
				k = k & 0x03;
				s >>= 1;
				if (k == 0x01)//01����������Ϊ0 д0 ��λΪ0��������Ӧ
				{
					DS18B20_Write_Bit(GPIOx,PINx,0);
					ss[(m * 8 + n)] = 0;
				}
				else if (k == 0x02)//����������Ϊ1 д1 ��λΪ1��������Ӧ
				{
					s = s | 0x80;
					DS18B20_Write_Bit(GPIOx,PINx,1);
					ss[(m * 8 + n)] = 1;
				}
				else if (k == 0x00)//����������Ϊ00 �г�ͻλ �жϳ�ͻλ
				{
					//�����ͻλ����ջ��д0 С��ջ��д��ǰ���� ����ջ��д1
					chongtuwei = m * 8 + n + 1;
					if (chongtuwei > zhan[l])
					{
						DS18B20_Write_Bit(GPIOx,PINx,0);
						ss[(m * 8 + n)] = 0;
						zhan[++l] = chongtuwei;
					}
					else if (chongtuwei < zhan[l])
					{
						s = s | ((ss[(m * 8 + n)] & 0x01) << 7);
						DS18B20_Write_Bit(GPIOx,PINx,ss[(m * 8 + n)]);
					}
					else if (chongtuwei == zhan[l])
					{
						s = s | 0x80;
						DS18B20_Write_Bit(GPIOx,PINx,1);
						ss[(m * 8 + n)] = 1;
						l = l - 1;
					}
				}
				else
				{
					//û��������
				}
			}
			tempp = s;
			DS18B20_ID[channel][num][m] = tempp; // ������������ID
		}
		num = num + 1;// �����������ĸ���
	} while (zhan[l] != 0 && (num < MaxSensorNum));
	DS18B20_SensorNum[channel] = num;
	//printf("DS18B20_SensorNum=%d\r\n",DS18B20_SensorNum);
}


uint8_t DS18B20_Crc(uint8_t *src, uint8_t size)
{
	//crc-8/MAXIM
	//x8 + x5 + x4 + 1
	//����ʽ��31
	//crc��ʼֵ��0
	//���������ֵ��0
	//���������һλ����CRCֵʱ�����0��������������CRC
	/*
	//Test
	uint8_t buf[10] = {0xbd, 0x01, 0x4b, 0x46, 0x7f, 0xff, 0x03, 0x10, 0xff};
	uint8_t buf1[10] = {0xc0, 0x01, 0x4b, 0x46, 0x7f, 0xff, 0x10, 0x10, 0x8f};
	uint8_t result2 = crcCalc(buf1, 8);
	uint8_t result3 = crcCalc(buf, 8);
	printf("result2 is %d %x\r\n", result2, result2);   //0x8f
	printf("result3 is %d %x\r\n", result3, result3);	//0xff
	uint8_t result2 = crcCalc(buf1, 9);
	uint8_t result3 = crcCalc(buf, 9);
	printf("result2 is %d %x\r\n", result2, result2);   //0
	printf("result3 is %d %x\r\n", result3, result3);	//0
	*/

	uint8_t ret = 0;
	uint8_t *p;
	int i = 0;
	uint8_t pBuf = 0;
	p = (uint8_t*)src;
 
	while(size--)
	{
		pBuf = *p ++;
 
		for ( i = 0; i < 8; i ++ )
		{
			if ((ret ^ (pBuf)) & 0x01)
			{
				ret ^= 0x18;
				ret >>= 1;
				ret |= 0x80;
			}
			else
			{
				ret >>= 1;
			}
 
			pBuf >>= 1;
		}
	}
 
	return ret;
}



void GXHT3W_Read_TempHum(GPIO_TypeDef *GPIOx, uint16_t PINx, uint8_t channel, uint8_t idx, float *temp, float *hum)
{
    uint8_t buf[9], j;
    uint16_t tem_raw, hum_raw;

    *temp = 0;
    *hum = 0;

    /* Match ROM + Convert (0x44), wait 3ms */
    DS18B20_Rst(GPIOx, PINx);
    DS18B20_DELAY_US(480);
    DS18B20_Write_Byte(GPIOx, PINx, 0x55);
    for (j = 0; j < 8; j++) DS18B20_Write_Byte(GPIOx, PINx, DS18B20_ID[channel][idx][j]);
    DS18B20_Write_Byte(GPIOx, PINx, 0x44);
    HAL_GPIO_WritePin(GPIOx, PINx, GPIO_PIN_SET);
    DS18B20_DELAY_MS(50);

    /* Match ROM + Read Scratchpad (0xBE) */
    DS18B20_Rst(GPIOx, PINx);
    DS18B20_DELAY_US(480);
    DS18B20_Write_Byte(GPIOx, PINx, 0x55);
    for (j = 0; j < 8; j++) DS18B20_Write_Byte(GPIOx, PINx, DS18B20_ID[channel][idx][j]);
    DS18B20_Write_Byte(GPIOx, PINx, 0xBE);

    for (j = 0; j < 9; j++) {
        buf[j] = DS18B20_Read_Byte(GPIOx, PINx);
    }

    /* CRC check */
    if (DS18B20_Crc(buf, 8) != buf[8]) return;

    /* determine resolution from config register (byte 6) bits[1:0]:
       00=14bit, 01=12bit, 10=15bit, 11=reserved */
    {
        uint8_t res = buf[6] & 0x03;
        float temp_lsb;   /* °C per LSB */

        if      (res == 0x01) temp_lsb = 0.0625f;    /* 12-bit: same as DS18B20 */
        else if (res == 0x02) temp_lsb = 0.0078125f; /* 15-bit */
        else                  temp_lsb = 0.015625f;  /* 14-bit (default 00) */

        /* Temperature: bytes 0-1, DS18B20-compatible format */
        {
            int16_t raw = (int16_t)((buf[1] << 8) | buf[0]);
            *temp = (float)raw * temp_lsb;
        }

        /* Humidity: bytes 2-3, formula: 100 * raw / (2^N - 1) */
        {
            uint16_t divisor;
            if      (res == 0x01) divisor = 4095;
            else if (res == 0x02) divisor = 32767;
            else                  divisor = 16383;

            hum_raw = (buf[3] << 8) | buf[2];
            *hum = 100.0f * (float)hum_raw / (float)divisor;
        }
    }
}
