# 主机代码测试记录

## 测试边界

- 当前完成范围：协议文档、完整帧编解码、CRC、流式拆包、USART2 接收环形缓冲、LoRa 传输层接入。
- 当前未完成范围：任务队列、36点业务缓存、控制命令、TD710异步结果、自动通风、Flash保存。
- 当前验证方式：Windows主机单元测试、协议向量一致性检查、Keil ARMCC全量构建。
- 当前未执行：真实主机、LoRa模块和示波器/逻辑分析仪上板测试。

## 1. 改造前构建基线

构建入口：

```text
LoraMaster_TD710/MDK-ARM/LoraSlaveV1.0.uvprojx
```

结果：

```text
0 Error(s), 0 Warning(s)
Code=16036, RO-data=832, RW-data=156, ZI-data=5836
```

## 2. 协议测试向量

检查内容：从协议Markdown提取4条标准报文，验证总长度、数据长度和CRC，并与C测试数组逐字节比较。

结果：

```text
G_TO_M1_READ：14字节，通过
M1_TO_S1_READ：14字节，通过
S1_TO_M1_TEMP：85字节，通过
M1_TO_G_TEMP：85字节，通过
AllVectorsMatch=True
```

## 3. Windows主机单元测试

统一命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\host\run_tests.ps1
```

脚本分别在 `-O0` 和 `-O2` 下启用以下严格检查：

```text
-Wall -Wextra -Werror -Wpedantic
-Wconversion -Wsign-conversion -Wshadow
```

单个优化级别结果：

| 模块 | 检查数 | 结果 |
| --- | ---: | --- |
| CRC和完整帧编解码 | 1700 | 通过 |
| 流式拆包状态机 | 5432 | 通过 |
| 256字节接收环形缓冲 | 100982 | 通过 |
| LoRa配置/应用模式传输层 | 775 | 通过 |
| ISR→环形缓冲→拆包集成链路 | 695 | 通过 |

`-O0` 和 `-O2` 两轮全部通过。

覆盖场景包括：

- 4条标准报文解码和重新编码；
- CRC-16/MODBUS标准值 `123456789 → 0x4B37`；
- 4条标准报文所有单比特破坏均不能通过；
- 错误帧头、版本、长度、CRC、角色、组号、方向、类型和枚举值；
- 逐字节输入、连续多帧、长噪声、重复 `AA`、坏帧后恢复和半帧中止；
- 255字节满载、缓冲区满拒绝、10000次回绕和突发收发；
- AT配置模式与二进制应用模式隔离；
- 环形缓冲区溢出或UART错误后的拆包恢复。

## 4. Keil ARMCC阶段3全量构建

工具：

```text
Keil MDK V5.24a
ARMCC V5.06 update 5 build 528
```

结果：

```text
0 Error(s), 0 Warning(s)
Code=15660, RO-data=668, RW-data=172, ZI-data=6356
```

相对基线：

```text
Code：-376字节
RO-data：-164字节
RW-data：+16字节
ZI-data：+520字节
```

代码减少主要来自移除旧 `sscanf` ASCII温度解析和旧直接控制逻辑；ZI增加来自256字节环形缓冲、109字节拆包缓冲、结构化消息和诊断状态。

## 5. 尚未通过的测试

内存/未定义行为消毒测试未执行成功，原因是当前MinGW工具链缺少：

```text
libasan
libubsan
```

这属于测试工具缺失，不能记录为通过。当前使用严格编译、边界测试、单比特破坏测试和ARMCC全量构建作为替代证据。

阶段3仍需在硬件联调阶段补充：

- USART2实际115200波形；
- 一条85字节 `TEMP_36` 连续接收；
- 两帧连续到达；
- 人工制造CRC错误；
- Keil Watch中 `LoRaDiag` 计数变化；
- 长时间接收时无串口溢出和HardFault。

