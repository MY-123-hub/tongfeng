# 主机代码测试记录

> V1历史记录保留在下文，不能再作为当前协议依据。当前实现已升级为LoRa协议V2：温度帧为89 B、环境帧为99 B，版本号为`0x02`。

## V2 当前验证结果（2026-09-10）

- `tests/host/run_tests.ps1` 已在本机使用 Visual Studio C 编译器完整执行；脚本在有GCC时仍优先使用GCC，无GCC时自动回退到MSVC。
- `O0` 与 `O2` 下共17组主机测试全部通过，覆盖从机温度/环境帧、主机BME字段补齐、控制室四组串行轮询、超时隔离及温度与环境整链路。
- 上位机Python协议测试共12项通过，覆盖V2长度、BME偏移、雨滴`FFFF`占位、CRC、串口跨分片帧头和人工流水号范围。
- 未执行ARM目标板全量构建和实机LoRa/BME/GXHT3W测试：本机没有可用的Keil ARM构建器；这些步骤仍需在烧录前完成。

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

## 6. 2026-08-23 单组统一协议验证

本轮把当前一套控制室、主机、从机固件统一为 Git 二进制协议，现场参数固定为
`AT+ADDR=88`、应用层 `group=1`，暂不读取拨码开关。控制室成为温度事务的唯一
发起者，端到端保留同一 `flow_id`，三个节点均采用50ms LoRa收发保护间隔。

ARMCC V5.06 update 7 全量构建结果：

| 工程 | Code | RO-data | RW-data | ZI-data | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| 主机 | 25268 | 668 | 260 | 12268 | 0 Error，0 Warning |
| 从机 | 16224 | 656 | 128 | 2432 | 0 Error，0 Warning |
| 控制室 | 12732 | 696 | 176 | 6896 | 0 Error，0 Warning |

Windows主机测试在 `-O0`、`-O2` 和严格警告下全部通过。新增
`unified_temperature_chain` 集成测试，实际串接以下软件状态机：

```text
控制室READ_TEMP → 主机转发 → 从机TEMP_36 → 主机转发 → 控制室上位机口
```

集成测试同时检查：固定组1、CRC不含`AA 55`、从机50ms最小应答延迟、36点温度
内容和 `flow_id=0x8000` 端到端不变。上位机Python协议测试6项通过。

尚未执行真实三板烧录和空口长时间测试；该项必须在现场联调后补记，不能以主机
测试和Keil构建结果替代。

## 7. 2026-08-23 控制命令可靠性修正

本轮修正上电自动恢复运行、拒绝ACK继续等待、控制室静默丢弃、上位机无限等待和
频率逐次擦写Flash。主机上电固定进入手动停机；频率保存采用3秒合并、失败5秒重试。

当前源码使用MSVC `/std:c11 /W4 /WX /utf-8` 重新编译并通过：

| 测试 | 结果 |
| --- | --- |
| `master_commands` | 164项检查通过 |
| `master_runtime` | 232项检查通过 |
| `control_room_gateway_runtime` | 通过 |
| `unified_temperature_chain` | 通过 |
| 上位机Python协议 | 8项通过 |

Keil ARMCC V5.06 update 7 构建结果：

| 工程 | Code | RO-data | RW-data | ZI-data | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| 主机 | 25424 | 668 | 272 | 12264 | 0 Error，0 Warning |
| 控制室 | 12848 | 696 | 176 | 6896 | 0 Error，0 Warning |

本轮只完成软件构建和宿主测试，尚未烧录。现场无风机联调应使用Modbus从站模拟器给
STM32主站返回合法写应答，或使用独立串口工具被动抓包；不能把两个Modbus主站并接后
当作可靠监听结果。
