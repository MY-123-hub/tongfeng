# 主机新架构说明

本目录是主机的协议层和业务层。硬件串口、LoRa、TD710 和片内 Flash 适配器位于 `Bsp/`；中断和 FreeRTOS 任务入口位于 `Core/`。

## 1. 主数据流

```text
USART2 ISR -> 256B环形缓冲 -> LoRaTask拆帧 -> q_master_event
                                                    |
                                                    v
                                              MasterRuntime
                                          /         |         \
                                  q_lora_tx    q_vfd_job    q_ui_snapshot
                                      |            |              |
                                  LoRaTask      ModBusTask      DGUSTask
                                      |            |
                                   USART2        USART3/TD710
```

`MasterRuntime` 是业务状态的唯一写入者。LoRa 和 Modbus 任务只负责收发与产生结构化事件，不直接改控制模式、风机状态或温度缓存。

## 2. 静态队列布局

| 队列 | 深度 | 元素 | 生产者 -> 消费者 | 满队列处理 |
| --- | ---: | --- | --- | --- |
| `q_master_event` | 4 | `MasterEvent` | LoRaTask/ModBusTask -> MasterRuntime | 计数并拒绝新事件，绝不覆盖旧事件 |
| `q_lora_tx` | 4 | `LoRaMessage` | MasterRuntime -> LoRaTask | 业务状态保持待发，后续周期重试 |
| `q_vfd_job` | 3 | `VfdJob` | MasterRuntime -> ModBusTask | 命令返回忙；手动停机使用队首插入 |
| `q_ui_snapshot` | 1 | `MasterUiSnapshot` | MasterRuntime -> DGUSTask | `xQueueOverwrite`，始终保留最新快照 |

四个队列均由 `xQueueCreateStatic` 创建，元素整体深拷贝，不在队列中传递栈指针。

## 3. 任务布局

| 任务 | 优先级 | 栈配置 | 职责 |
| --- | --- | ---: | --- |
| `LoRaTask` | AboveNormal | 256 | USART2 二进制收发、流式拆帧、深拷贝入队 |
| `ModBusTask` | AboveNormal | 256 | TD710 单事务异步状态机、结果入队 |
| `defaultTask` | Normal | 256 | 主控状态机、命令去重、自动通风、Flash保存 |
| `DGUSTask` | Low | 128 | 本地屏触摸和最新快照显示 |

栈数值为 CubeMX/CMSIS-RTOS 配置值。真实栈高水位必须在上板长时运行后再确认。

## 4. 模块边界

- `lora_protocol`：公共帧、CRC-16/MODBUS、方向/长度/枚举值校验。
- `lora_stream_parser` + `lora_rx_ring`：粘包、拆包、噪声重同步和 ISR 环形缓冲。
- `master_ingress` + `master_identity`：上电冻结拨码组号，本机寻址与同组过滤。
- `master_temperature`：36点缓存、单在途流水号、3s从机超时和待发回复。
- `command_service`：1个pending + 4条completed历史，按“流水号+类型+长度+数据”去重。
- `auto_control`：任意有效点高于目标立即启动；36点全部有效且不高于“目标-0.5℃”连续60s才停机。
- `vfd_modbus_codec`：TD710 功能码 `10`、寄存器 `2000`的纯C编解码。
- `parameter_record`：32B、CRC32、generation反码和双提交标志。
- `master_runtime`：组合上述纯逻辑，是模式、参数、温度和风机确认状态的唯一写入者。

## 5. 集中可调参数

`master_config.h` 是唯一配置入口：

- 温度缓存新鲜期：5s；
- 从机响应超时：3s；
- 默认频率：30.00Hz；范围0~50.00Hz；
- 默认目标：26.0℃；范围-55.0~125.0℃；
- 自动停机回差：0.5℃；低温连续时间：60s。

这些默认值是当前工程假设，现场联调确认后只改本文件，不在各模块散落魔法数。
