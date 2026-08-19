#include "DGUS.h"


uint16_t DGUS_cntPre = 0;


/*****************************************************
  * 函数名称：	DGUS_Clear
  * 函数功能：	清空缓存
  * 入口参数：	无
  * 返回参数：	无
  * 说    明：  无
*****************************************************/
void DGUS_Clear(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	memset(Rx1Buffer, 0, sizeof(Rx1Buffer));
	rx1_pointer = 0;
	rx1_frame_ready = 0;
	rx1_overflow = 0;
	DGUS_cntPre = 0;
	if (primask == 0U)
	{
		__enable_irq();
	}
}


/*****************************************************
  * 函数名称：	DGUS_WaitRecive
  * 函数功能：	等待接收完成
  * 入口参数：	无
  * 返回参数：	REV_OK-接收完成		REV_WAIT-接收超时未完成
  * 说    明：  循环调用检测是否接收完成
*****************************************************/
_Bool DGUS_WaitRecive(void)
{
	if (rx1_overflow != 0U)
	{
		DGUS_Clear();
		return REV_WAIT;
	}
	if (rx1_frame_ready != 0U)
	{
		return REV_OK;
	}
	if(rx1_pointer == 0) 							//如果接收计数为0 则说明没有处于接收数据中，所以直接跳出，结束函数
		return REV_WAIT;
	if(rx1_pointer == DGUS_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
	{
    HAL_Delay(5);   // 确保接受数据完整性
    if(rx1_pointer == DGUS_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
    {
	  rx1_frame_ready = 1;						// 冻结当前帧，防止解析期间被中断覆盖
      return REV_OK;								//返回接收完成标志
    }
	}
	DGUS_cntPre = rx1_pointer;					//置为相同
    return REV_WAIT;								//返回接收未完成标志
}


/*****************************************************
  * 函数名称：	DGUS_SendCmd
  * 函数功能：	向 DGUS 某个变量空间（0x0000-0xFFFF）写入数据
  * 入口参数：	Addr：写入地址（2字节）
                Data：数据（2字节）
  * 返回参数：	0-成功	1-失败
  * 说明：		
*****************************************************/
void DGUS_WriteSingleData(int S_Addr,int Data)
{
  char Com_all[16]={0x5A,0xA5,0x05,0x82,S_Addr/256,S_Addr%256,Data/256,Data%256};

  Usart_SendString(huart1, (unsigned char *)Com_all, 8);
}


/*****************************************************
  * 函数名称：	DGUS_TouchAck
  * 函数功能：	DGUS 触摸数据应答
  * 入口参数：	无
  * 返回参数：	无
  * 说    明：	无
*****************************************************/
void DGUS_TouchAck(void)
{
  if ((DGUS_WaitRecive() == REV_OK) && (rx1_pointer >= 6U) &&
      ((uint16_t)rx1_pointer >= ((uint16_t)Rx1Buffer[2] + 3U)) &&
      (Rx1Buffer[0] == 0x5A) && (Rx1Buffer[1] == 0xA5) &&
      (Rx1Buffer[3] == 0x83))
  {
    switch ((uint16_t)((Rx1Buffer[4] << 8) | Rx1Buffer[5]))
    {
      case 0x2900:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x09,0x82,0x29,0x00,0x00,0x01,0x00,0x00,0x00,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 13);
      } break;
      case 0x2901:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x09,0x82,0x29,0x00,0x00,0x00,0x00,0x01,0x00,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 13);
      } break;
      case 0x2902:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x09,0x82,0x29,0x00,0x00,0x00,0x00,0x00,0x00,0x01};
        Usart_SendString(huart1, (unsigned char *)Com_all, 13);
      } break;
      case 0x2903:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x0D,0x82,0x29,0x03,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 16);
      } break;
      case 0x2904:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x0D,0x82,0x29,0x03,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 16);
      } break;
      case 0x2905:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x0D,0x82,0x29,0x03,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 16);
      } break;
      case 0x2906:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x0D,0x82,0x29,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 16);
      } break;
      case 0x2907:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x0D,0x82,0x29,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01};
        Usart_SendString(huart1, (unsigned char *)Com_all, 16);
      } break;
      case 0x2908:
      {
        DGUS_Clear();
        char Com_all[20]={0x5A,0xA5,0x07,0x82,0x29,0x08,0x00,0x01,0x00};
        Usart_SendString(huart1, (unsigned char *)Com_all, 10);
      } break;
      default:
        break;
    }
  }

  if (rx1_frame_ready != 0U)
  {
    DGUS_Clear();
  }
}







