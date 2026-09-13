#ifndef __DGUS_H__
#define __DGUS_H__

#include "main.h"
#include "stdio.h"
#include "string.h"
#include "usart.h"
#include "lora.h"




/**
  * @brief  基础功能码定义
  */
#define REV_WAIT    1   // 接受未完成标志
#define REV_OK      0   // 接受完成标志



/**
  * @brief  DGUS 串口屏地址枚举
  */
typedef enum
{
    DGUS_GrainHumi             	= 0x3100,    	/*!< 粮面湿度   */
    DGUS_GrainTemp              = 0x3102,   	/*!< 粮面温度   */
    DGUS_GrainSpeed             = 0x3104,    	/*!< 粮面风速   */
    DGUS_EnvirHumi              = 0x3106,    	/*!< 环境湿度   */
    DGUS_EnvirTemp              = 0x3108,    	/*!< 环境温度   */
}DGUSWriteAddrTypeNum;



/* 函数声明 */
void DGUS_WriteSingleData(int S_Addr,int Data);
void DGUS_TouchAck(void);



/* 变量声明 */



#endif /*__DGUS_H__*/
