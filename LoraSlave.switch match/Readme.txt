V1.0

* 测试芯片、晶振、串口
* 多通道读取DS18B20、读DHT11 完成



V1.1

* 文件 lora.c
	LORA_Clear（）
	LORA_WaitRecive（）
	LORA_SendCmd（）
	LORA_SendData（）
	LORA_Init（）
	LoraP2PTrans（）测试成功
* 功能：添加 DS18B20 数据过滤

2024/9/9
	添加功能：Lora增加传输DHT11环境数据：DHT11_H，DHT11_T

v 1.2

*添加功能：IIC读取风速

v1.3

*测温电缆
	*改为寄生供电模式