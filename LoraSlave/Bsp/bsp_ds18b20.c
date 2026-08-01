#include "bsp_ds18b20.h"
#include "core_delay.h" 
#include "stdio.h" 


unsigned char DS18B20_ID[6][MaxSensorNum][8];
unsigned char DS18B20_SensorNum[6];
 
// 配置DS18B20用到的I/O口
void DS18B20_GPIO_Config(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	/*定义一个GPIO_InitTypeDef类型的结构体*/
  GPIO_InitTypeDef GPIO_InitStruct = {0};

	/*gpio.c中已开启GPIOA外设时钟*/
 
  GPIO_InitStruct.Pin = PINx;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
}
 
// 引脚输入
void DS18B20_Mode_IPU(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	/*定义一个GPIO_InitTypeDef类型的结构体*/
  GPIO_InitTypeDef GPIO_InitStruct = {0};
 
  GPIO_InitStruct.Pin = PINx;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}
 
// 引脚输出
void DS18B20_Mode_Out(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	/*定义一个GPIO_InitTypeDef类型的结构体*/
  GPIO_InitTypeDef GPIO_InitStruct = {0};
 
  GPIO_InitStruct.Pin = PINx;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOx, &GPIO_InitStruct);
}
 
// 复位，主机给从机发送复位脉冲
void DS18B20_Rst(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	DS18B20_Mode_Out(GPIOx,PINx);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);		// 产生至少480us的低电平复位信号
	DS18B20_DELAY_US(480);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);;	// 在产生复位信号后，需将总线拉高
	DS18B20_DELAY_US(15);
}
 
// 检测从机给主机返回的应答脉冲。从机接收到主机的复位信号后，会在15~60us后给主机发一个应答脉冲
uint8_t DS18B20_Answer_Check(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	uint8_t delay = 0;
	DS18B20_Mode_IPU(GPIOx,PINx); // 主机设置为上拉输入
	// 等待应答脉冲（一个60~240us的低电平信号 ）的到来
	// 如果100us内，没有应答脉冲，退出函数，注意：从机接收到主机的复位信号后，会在15~60us后给主机发一个存在脉冲
	while (HAL_GPIO_ReadPin(GPIOx,PINx)&&delay < 100)
	{
		delay++;
		DS18B20_DELAY_US(1);
	}
	// 经过100us后，如果没有应答脉冲，退出函数
	if (delay >= 100)//Hu200
		return 1;
	else
		delay = 0;
	// 有应答脉冲，且存在时间不超过240us
	while (!HAL_GPIO_ReadPin(GPIOx,PINx)&&delay < 240)
	{
		delay++;
		DS18B20_DELAY_US(1);
	}
	if (delay >= 240)
		return 1;
	return 0;
}
 
// 从DS18B20读取1个位
uint8_t DS18B20_Read_Bit(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	uint8_t data;
	DS18B20_Mode_Out(GPIOx,PINx);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET); // 读时间的起始：必须由主机产生 >1us <15us 的低电平信号
	DS18B20_DELAY_US(2);
	HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
	DS18B20_DELAY_US(12);
	DS18B20_Mode_IPU(GPIOx,PINx);// 设置成输入，释放总线，由外部上拉电阻将总线拉高
	if (HAL_GPIO_ReadPin(GPIOx,PINx))
		data = 1;
	else
		data = 0;
	DS18B20_DELAY_US(50);
	return data;
}
 
// 从DS18B20读取2个位
uint8_t DS18B20_Read_2Bit(GPIO_TypeDef * GPIOx,uint16_t PINx)//读二位 子程序
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
 
// 从DS18B20读取1个字节
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
 
// 写1位到DS18B20
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
 
// 写1字节到DS18B20
void DS18B20_Write_Byte(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t dat)
{
	uint8_t j;
	uint8_t testb;
	DS18B20_Mode_Out(GPIOx,PINx);
	for (j = 1; j <= 8; j++)
	{
		testb = dat & 0x01;
		dat = dat >> 1;
    HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// 拉低2us，进入写状态
    DS18B20_DELAY_US(9);
		if (testb)
		{
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// 写1
			DS18B20_DELAY_US(10);
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
			DS18B20_DELAY_US(50);
		}
		else
		{
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_RESET);// 写0
			DS18B20_DELAY_US(60);
			HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);// 释放总线
			DS18B20_DELAY_US(2);
		}
	}
    DS18B20_DELAY_US(100);
    HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);
    DS18B20_DELAY_US(4);
}
 
//初始化DS18B20的IO口，同时检测DS的存在
uint8_t DS18B20_Init(GPIO_TypeDef * GPIOx,uint16_t PINx)
{
	DS18B20_GPIO_Config(GPIOx,PINx);
	DS18B20_Rst(GPIOx,PINx);
	return DS18B20_Answer_Check(GPIOx,PINx);
}
 
// 从ds18b20得到温度值，精度：0.1C，返回温度值（-550~1250），Temperature1返回浮点实际温度
uint8_t crc_t[10];
float DS18B20_Get_Temp(GPIO_TypeDef * GPIOx,uint16_t PINx,uint8_t channel, uint8_t i)
{
	uint8_t j;//匹配的字节
	uint8_t TL, TH;
	short Temperature;
	float Temperature1;
  
  /* 寄生电源模式——读取温度 */
	DS18B20_Rst(GPIOx,PINx);                            // 复位
	DS18B20_Answer_Check(GPIOx,PINx);                   // 接受存在脉冲-等待从机准备好
//	DS18B20_Write_Byte(GPIOx,PINx,0xcc);              // 寄生电源供电此处不能使用“跳过 ROM”命令，需要发送“匹配ROM”命令
	DS18B20_Write_Byte(GPIOx,PINx,0x55);                // 发送“搜索ROM”命令——查找指定设备
	for (j = 0; j < 8; j++)DS18B20_Write_Byte(GPIOx,PINx,DS18B20_ID[channel][i][j]);// 发送设备 ID 号
	DS18B20_Write_Byte(GPIOx,PINx,0x44);                // 发送转换命令
  HAL_GPIO_WritePin(GPIOx,PINx,GPIO_PIN_SET);         // 转换命令发送后，寄生供电必须强制拉高数据线至少750000us以提供充分电能供完成温度转换
  DS18B20_DELAY_US(750000);                           // 转换命令发送后，寄生供电必须强制拉高数据线至少750000us以提供充分电能供完成温度转换
	DS18B20_Rst(GPIOx,PINx);                            // 复位
	DS18B20_Answer_Check(GPIOx,PINx);                   // 接受存在脉冲-等待从机准备好
	DS18B20_Write_Byte(GPIOx,PINx,0x55);                // 发送“搜索ROM”命令——查找指定设备
	for (j = 0; j < 8; j++)DS18B20_Write_Byte(GPIOx,PINx,DS18B20_ID[channel][i][j]);// 发送设备 ID 号
	DS18B20_Write_Byte(GPIOx,PINx,0xbe);                // 发送“读取中间结果寄存器”命令 
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
  DS18B20_Rst(GPIOx,PINx);                            // 复位
	DS18B20_Answer_Check(GPIOx,PINx);                   // 接受存在脉冲-等待从机准备好

  /* 处理数据 */
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
 
// 自动搜索ROM
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
		DS18B20_Rst(GPIOx,PINx); //注意：复位的延时不够
		DS18B20_DELAY_US(480); //480、720
		DS18B20_Write_Byte(GPIOx,PINx,0xf0);
		for (m = 0; m < 8; m++)
		{
			uint8_t s = 0;
			for (n = 0; n < 8; n++)
			{
				k = DS18B20_Read_2Bit(GPIOx,PINx);//读两位数据
 
				k = k & 0x03;
				s >>= 1;
				if (k == 0x01)//01读到的数据为0 写0 此位为0的器件响应
				{
					DS18B20_Write_Bit(GPIOx,PINx,0);
					ss[(m * 8 + n)] = 0;
				}
				else if (k == 0x02)//读到的数据为1 写1 此位为1的器件响应
				{
					s = s | 0x80;
					DS18B20_Write_Bit(GPIOx,PINx,1);
					ss[(m * 8 + n)] = 1;
				}
				else if (k == 0x00)//读到的数据为00 有冲突位 判断冲突位
				{
					//如果冲突位大于栈顶写0 小于栈顶写以前数据 等于栈顶写1
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
					//没有搜索到
				}
			}
			tempp = s;
			DS18B20_ID[channel][num][m] = tempp; // 保存搜索到的ID
		}
		num = num + 1;// 保存搜索到的个数
	} while (zhan[l] != 0 && (num < MaxSensorNum));
	DS18B20_SensorNum[channel] = num;
	//printf("DS18B20_SensorNum=%d\r\n",DS18B20_SensorNum);
}


uint8_t DS18B20_Crc(uint8_t *src, uint8_t size)
{
	//crc-8/MAXIM
	//x8 + x5 + x4 + 1
	//多项式：31
	//crc初始值：0
	//计算结果异或值：0
	//当数组最后一位含有CRC值时，输出0，否则输出计算的CRC
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

