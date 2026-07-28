#include "modbus.h"
#include "usart.h"
#include "stdio.h"


ModbusTypeDef ModbusType;



/**
  * @brief  Modbus 初始化函数
  * @note   从机地址设定
  * @param  无
  * @retval 无
  */
void ModbusInit(void)
{
    ModbusType.slave_addr=1;  //******从机地址：1
}



/**
  * @brief  Modbus-CRC校验码计算
  * @note   无
  * @param  dat:要生成CRC校验码的数据(数组)
  * @param  len:要生成CRC校验码的数据长度
  * @retval 根据传参生成的CRC检验码
  */
uint16_t GetModbusCRC16_Cal(uint8_t *dat, uint32_t len)
{
	uint8_t temp;
	uint16_t wcrc = 0XFFFF;//16位crc寄存器预置
	uint32_t i = 0, j = 0;//计数
	for (i = 0; i < len; i++)//循环计算每个数据
	{
		temp = dat[i] & 0X00FF;//将八位数据与crc寄存器亦或
		wcrc ^= temp;						//将数据存入crc寄存器
		for (j = 0; j < 8; j++)	//循环计算数据的
		{
			if (wcrc & 0X0001)//判断右移出的是不是1，如果是1则与多项式进行异或。
			{
				wcrc >>= 1;//先将数据右移一位
				wcrc ^= 0XA001;//与上面的多项式进行异或
			}
			else//如果不是1，则直接移出
			{
				wcrc >>= 1;//直接移出
			}
		}
	}

  return ((wcrc << 8)|(wcrc>>8))&0xffff;//高低位置换
}



/**
  * @brief  Modbus RS485发送函数
  * @note   可以读数据，写单个寄存器数据，暂不能多个连续寄存器数据
  * @param  addr：从机地址
  * @param  fun：功能码，参考 Modbus_FunTypeDef
  * @param  reg_addr：要写入/读取的寄存器地址，参考 ModbusReadAddrTypeNum，ModbusWriteAddrTypeNum
  * @param  reg_size：要读取/写入的寄存器数量
  * @retval 无
  */
void ModbusTx(uint8_t addr,uint8_t fun,uint16_t reg_addr,uint16_t reg_size)
{
	uint16_t modbus_crc;        // 用于暂存计算得到的 CRC 校验码
  /* 根据 RS485 通讯格式放置数据 */ 
	ModbusType.TxBuf[0]=addr;       
	ModbusType.TxBuf[1]=fun;
	ModbusType.TxBuf[2]=(reg_addr&0xff00)>>8;
	ModbusType.TxBuf[3]=(reg_addr&0x00ff);
	ModbusType.TxBuf[4]=(reg_size&0xff00)>>8;
	ModbusType.TxBuf[5]=(reg_size&0x00ff);
	modbus_crc=GetModbusCRC16_Cal(ModbusType.TxBuf,6);
	ModbusType.TxBuf[6]=(modbus_crc&0xff00)>>8;
	ModbusType.TxBuf[7]=(modbus_crc&0x00ff);
	for(uint8_t i=0;i<8;i++)    // 通过串口3 发送 RS485 指令
	{
		HAL_UART_Transmit(&huart3,(uint8_t *)&ModbusType.TxBuf[i],1,50);
	}
    
    ModbusType.TxProcFinishFlag=1;  // 发送完毕标志位置1
    HAL_Delay(10);                  // 发完一帧后延时10ms
}



/**
  * @brief  Modbus RS485接受处理函数
  * @note   判断Modbus从机返回的应答信号是否正确，并做响应处理（串口1打印信息）
  * @param  接收到的 Modbus 从机应答数据
  * @retval 无
  */
void ModBusRxProc(void)
{   
    if((ModbusType.RxRcFinishFlag==1)&(ModbusType.TxProcFinishFlag==1))      // 发送数据完成
    {
        uint16_t crc_cal;   // 用于存计算得到的CRC校验码
        
        /*** 打印接收到数据，调试用 ***/
        //printf("\r\nData: %02x %02x %02x %02x %02x %02x %02x %02x \r\n"
                //,ModbusType.RxBuf[0],ModbusType.RxBuf[1],ModbusType.RxBuf[2],ModbusType.RxBuf[3],ModbusType.RxBuf[4],ModbusType.RxBuf[5],ModbusType.RxBuf[6],ModbusType.RxBuf[7]);      
        
        crc_cal=GetModbusCRC16_Cal(ModbusType.RxBuf,sizeof(ModbusType.RxBuf)-2);    // 计算接收数据的校验码
        if(crc_cal==((ModbusType.RxBuf[sizeof(ModbusType.RxBuf)-2]<<8)|(ModbusType.RxBuf[sizeof(ModbusType.RxBuf)-1]))) // 判断校验码
        {
            //printf("CRC Match succeeded!!! \r\n");
            if(ModbusType.RxBuf[0]==ModbusType.slave_addr)  // 判断从机地址
            {
                //printf("Slave addr --> %d \r\n",ModbusType.slave_addr);
                if(ModbusType.RxBuf[1]==3)   // 读功能
                {
                    switch(ModbusType.TxBuf[2]<<8|ModbusType.TxBuf[3])
                    {
                        case 0x2102:            // 读频率命令
                        {
                            //printf(">>>Freq command：%.2f <<<\r\n",(float)(ModbusType.RxBuf[3]<<8|ModbusType.RxBuf[4])/100);
                        }break; 
                    
                        case 0x2103:            // 读输出频率
                        {
                            //printf(">>>Freq output：%.2f <<<\r\n",(float)(ModbusType.RxBuf[3]<<8|ModbusType.RxBuf[4])/100);
                        }break; 
                        
                        case 0x2104:            // 读输出电流
                        {
                            //printf(">>>Current output：%d <<<\r\n",ModbusType.RxBuf[3]<<8|ModbusType.RxBuf[4]);
                        }break; 
                        
                        case 0x2106:            // 读输出电压
                        {
                            //printf(">>>Volt output：%d <<<\r\n",ModbusType.RxBuf[3]<<8|ModbusType.RxBuf[4]);
                        }break; 
                        
                        case 0x210C:            // 读马达实际转速
                        {
                            //printf(">>>RPM：%d <<<\r\n",ModbusType.RxBuf[3]<<8|ModbusType.RxBuf[4]);
                        }break; 
                         
                        case 0x210F:            // 读输出功率
                        {
                            //printf(">>>Power output：%d <<<\r\n",ModbusType.RxBuf[3]<<8|ModbusType.RxBuf[4]);
                        }break; 
                    }
                }
                if(ModbusType.RxBuf[1]==6)  // 写功能
                {
                    switch(ModbusType.RxBuf[2]<<8|ModbusType.RxBuf[3])
                    {
                        case 0x2001:            // 写频率命令
                        {
                            //printf(">>>Freq command write success：%d <<<\r\n",ModbusType.RxBuf[4]<<8|ModbusType.RxBuf[5]);
                        }break; 
                    
                        case 0x2000:            // 写操作命令
                        {
                            switch(ModbusType.RxBuf[4]<<8|ModbusType.RxBuf[5])
                            {
                                case 0x0012:        // 正转开始命令
                                {
                                    //printf(">>> Modbus write forward start <<<\r\n");
                                    SysVariType.vent_open_flag=1;
                                }break; 
                            
                                case 0x0011:        // 正转停止命令
                                {
                                    //printf(">>> Modbus write forward stop <<<\r\n");
                                    SysVariType.vent_open_flag=0;
                                }break; 
                                
                                case 0x0022:        // 反转开始命令
                                {
                                    //printf(">>> Modbus write reverse start <<<\r\n");
                                }break; 
                            
                                case 0x0021:        // 反转停止命令
                                {
                                    //printf(">>> Modbus write reverse stop <<<\r\n");
                                }break; 
                            }
                        }break; 
                        
                    }
                }
            }
        }
        else 
            //printf("CRC March Failed!!! \r\n");   /* 接受数据 CRC 匹配错误 */
    
        ModbusType.TxProcFinishFlag=0;    /*******必须在此处归零，否则接收数据极不稳定******/
        ModbusType.RxRcFinishFlag=0;    /******* 数据接受完成标志位复位 *******/ 
        ModbusType.RxPointer=0;     /******* 接受数据的指针归零 *******/ 
    }
}

