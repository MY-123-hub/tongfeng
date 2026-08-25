#include "iic.h"


/**
 @brief 单次模式读取压力数据
 @param pPressure -[out] 压力值
 @return 无
*/
void GZP6859D_ReadSingleModePressureData(uint32_t *pPressure)
{		
    uint8_t cmd = GZP6859D_ONE_PRESS;
    uint8_t result = 0;
    uint8_t pressArr[4] = {0};
    int32_t press = 0;
    
    // 进行单次传感器压力信号采集模式
    HAL_I2C_Mem_Write(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_WRITE_BIT,
                    GZP6859D_CMD_ADDR, I2C_MEMADD_SIZE_8BIT, &cmd, 1, 10);
    
    // 采集结束
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_CMD_ADDR, I2C_MEMADD_SIZE_8BIT, &result, 1, 10);
    printf("0x%02X  ", result);
    
    // 获取压力数据AD值
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_DATA_MSB_ADDR, I2C_MEMADD_SIZE_8BIT, &pressArr[1], 1, 10);
    printf("0x%02X  ", pressArr[1]);
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_DATA_CSB_ADDR, I2C_MEMADD_SIZE_8BIT, &pressArr[2], 1, 10);
    printf("0x%02X  ", pressArr[2]);
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_DATA_LSB_ADDR, I2C_MEMADD_SIZE_8BIT, &pressArr[3], 1, 10);
    printf("0x%02X  ", pressArr[3]);
    
    // 压力计算公式
    press = (pressArr[0] << 24) + (pressArr[1] << 16) + (pressArr[2] << 8) + pressArr[3];
    if(press > 8388607)
    {
        press = press - 16777216;
    }
    press = press / GZP6859D_K_VALUE;   // 单位为Pa
    *pPressure = press;
    printf("press:%d\r\n", press);
}


void GZP6859D_ReadCombinedModeData(uint8_t *pTemperature, uint8_t *pPressure)
{
  uint8_t cmd = GZP6859D_COM;   // 命令：0x0A->010组合采集模式
    
    uint8_t result = 0;   // 记录命令寄存器
    uint8_t tempArr[2],pressArr[3];   // 记录数据寄存器
    int32_t temp,press;     // 记录计算后的数据
    
    // 进行组合模式读取数据
    HAL_I2C_Mem_Write(&hi2c2, (GZP6859D_SLAVE_ADDR << 1)| GZP6859D_WRITE_BIT,
                    GZP6859D_CMD_ADDR, I2C_MEMADD_SIZE_8BIT, &cmd, 1, 10);
    // 采集结束
    HAL_Delay(10);
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_CMD_ADDR, I2C_MEMADD_SIZE_8BIT, &result, 1, 10);
//    printf("命令地址：0x%02X  \r\n", result);   //——调试
    
    // 获取温度数据AD值
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_TEMP_MSB_ADDR, I2C_MEMADD_SIZE_8BIT, &tempArr[0], 1, 10);
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_TEMP_LSB_ADDR, I2C_MEMADD_SIZE_8BIT, &tempArr[1], 1, 10);
//    printf("TEMP:0x%02X 0x%02X ", tempArr[0],tempArr[1]);   //——调试
    
    // 温度计算公式
    temp = (tempArr[0] << 8) + tempArr[1];
    if(temp >> 15)temp = temp - 65536;  // 最高位为"1"，代表负温度
    temp = temp / 256;                  // 单位为℃
    *pTemperature = temp;
//    printf("温度:%d \r\n", temp);   //——调试
    
    // 获取压力数据AD值
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_DATA_MSB_ADDR, I2C_MEMADD_SIZE_8BIT, &pressArr[0], 1, 10);
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_DATA_CSB_ADDR, I2C_MEMADD_SIZE_8BIT, &pressArr[1], 1, 10);
    HAL_I2C_Mem_Read(&hi2c2, (GZP6859D_SLAVE_ADDR << 1) | GZP6859D_READ_BIT,
                    GZP6859D_DATA_LSB_ADDR, I2C_MEMADD_SIZE_8BIT, &pressArr[2], 1, 10);
//    printf("DATA:0x%02X 0x%02X 0x%02X ", pressArr[0], pressArr[1], pressArr[2]);   //——调试
    
    // 压力计算公式
    press = (pressArr[0] << 16) + (pressArr[1] << 8) + pressArr[2];
    if(press>> 23)press = press - 16777216;  // 最高位为"1"，代表负压
    press = press / GZP6859D_K_VALUE;   // 单位为Pa
    *pPressure = press;
//    printf("压力:%d pa \r\n", press);   //——调试
}




