V1.0

* 测试芯片、晶振、串口
* 多通道读取DS18B20、读DHT11 完成



V1.1

* 文件 lora.c
	LORA_Clear（）
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

v1.4

* 从机仅响应同组主机的正式二进制 `READ_TEMP`，返回固定 85 字节 `TEMP_36`。
* 每次温度请求扫描 6 个 Port；未接入、读取/CRC 失败和 0.0℃ 均发送 `00 00` 无效值。
* LoRa 模块统一使用 `NODE + TRANS + CH4700 + MTU128 + ADDR0 + 115200/8N1`。
* 拨码仅作为应用层组号，允许 01~04；不再通过 LoRa 发送 ASCII 拨码文本。
