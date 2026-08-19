#ifndef _modbus_h_
#define _modbus_h_


#include "main.h"

/* TD710 目标频率：单位 0.01 Hz（30.00 Hz = 3000）——占位值，实际转速确认后只改此处 */
#define VFD_TARGET_FREQ_X100   (3000U)


/**
  * @brief  Modbus 变量结构体定义
  */
typedef struct
{
    uint8_t slave_addr;             // 从机地址
    
    uint8_t TxBuf[100];             // 主机发送数据缓存区
    volatile uint8_t TxProcFinishFlag; // 发送并等待从机应答标志位
    volatile uint16_t TxWaitTime;   // 等待应答时间，单位：ms
    
    uint8_t RxBuf[100];             // 接受从机应答数据缓存区
    volatile uint8_t RxPointer;     // 接受数据指针
    volatile uint8_t RxTimRun;      // 判断接收是否正在进行
    volatile uint8_t RxWaitTime;    // 帧间静默计时，单位：ms
    volatile uint8_t RxRcFinishFlag;// 接收数据完成标志位，
    volatile uint8_t RxOverflow;    // 接收缓冲区溢出标志位
    
    uint16_t Tx_1s;         // 每1s发送一次数据
    uint16_t Tx_500ms;         // 每1s发送一次数据

    volatile uint8_t TxAction;      // 最近一次发送的运行命令（16 回显帧不含数据值，需自己记录）
}ModbusTypeDef;


/**
  * @brief  Modbus 功能码枚举
  */
typedef enum
{
    MudbusFun_read             	   = 0x03,    	/*!< 功能码 03，读数据  */
    MudbusFun_writeSingle          = 0x06,   	/*!< 功能码 06，写单个寄存器数据    */
    MudbusFun_writeMulti           = 0x10,    	/*!< 功能码 10，写多个连续的寄存器的数据   */
} ModbusFunNumTypeNum;


/**
  * @brief  Modbus 读取地址枚举
  */
typedef enum
{
    modbusread_freqcom             	= 0x2102,    	/*!< 读频率命令   */
    modbusread_freqout              = 0x2103,   	/*!< 读输出频率   */
    modbusread_currentout           = 0x2104,    	/*!< 读输出电流   */
    modbusread_voltout              = 0x2106,    	/*!< 读输出电压   */
    modbusread_rpm                  = 0x210C,    	/*!< 读马达转速   */
    modbusread_powerout             = 0x210F,    	/*!< 读输出功率   */
} ModbusReadAddrTypeNum;


/**
  * @brief  Modbus 写入地址枚举
  */
typedef enum
{
    modbuswrite_Actioncom             = 0x2000,    	/*!< 写操作命令   */
    modbuswrite_freqcom             	= 0x2001,    	/*!< 写频率命令   */
} ModbusWriteAddrTypeNum;


/**
  * @brief  Modbus 写操作命令枚举
  */
typedef enum
{
    modbuswrite_RunFwd                 = 0x0001,   	/*!< 正转运行   */
    modbuswrite_RunRev                 = 0x0009,   	/*!< 反转运行   */
    modbuswrite_StopDec                  = 0x0003,   	/*!< 减速停机   */
    modbuswrite_StopFree                  = 0x0004,   	/*!< 自由停机   */
} ModbusWriteActionCmdTypeNum;



/* 函数声明 */
void ModbusTx(uint8_t addr,uint8_t fun,uint16_t reg_addr,uint16_t reg_size);
void ModbusTxVfdCmd(uint8_t action, uint16_t freq_x100);
void ModBusRxProc(void);
void ModbusInit(void);



/* 变量声明 */
extern ModbusTypeDef ModbusType;

#endif

