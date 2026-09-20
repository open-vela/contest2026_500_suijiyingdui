<!-- SPDX-License-Identifier: Apache-2.0 -->

# OK8MP Cortex-M7 openvela 移植说明

## 1. 移植范围

本项目以飞凌 OK8MP 开发板的 i.MX8M Plus Cortex-M7 实时核为目标，按照路径 A 建立
openvela 板级支持。NuttX 上游未提供该开发板面向 Cortex-M7 的完整 BSP，因此项目在
NuttX 通用 ARM 架构代码的基础上，完成板级启动、DDR 链接、时钟与引脚、串口、GPIO、
定时器、I2C、以太网和软件复位等适配，并将网络、uORB 和 AI Agent 接入同一运行镜像。

比赛演示配置为 `nsh-agent`。该配置在 NSH 基础上启用离线语音模块、语音 uORB 桥接、
ENET1/FEC 网络、Mbed TLS、HTTPS 和 openvela AI Agent。

## 2. 工程组织

主要源码位置如下：

```text
source/
├── nuttx/
│   └── boards/arm/mx8mp/ok8mp-m7/
│       ├── configs/nsh-agent/defconfig
│       ├── scripts/ddr.ld
│       └── src/
│           ├── mx8mp_boot.c
│           ├── mx8mp_bringup.c
│           ├── ok8mp_voice_i2c.c
│           └── ok8mp_fec.c
├── apps/
│   └── examples/ok8mp_voice_uorb/
└── packages/ai_agent/
    ├── src/tools/tool_ok8mp_voice.c
    ├── src/tools/tool_ok8mp_network.c
    └── agent_skills/
```

板级目录通过 Kconfig、Makefile、CMakeLists 和 `nsh`、`nsh-net`、`nsh-agent` 等
defconfig 纳入 NuttX 构建体系。比赛发布配置及已验证镜像位于
`firmware/competition/`。

## 3. 启动与 DDR 内存布局

M7 固件由 U-Boot 的 `bootaux` 命令加载。镜像开头保存 Cortex-M7 启动向量，前两个
32 位字依次为初始栈指针和复位入口；U-Boot 将镜像加载到 `0x80000000` 后读取该向量并
启动 M7。

比赛镜像使用 DDR 链接方案，布局由 `scripts/ddr.ld` 定义：

| 区域 | M7 地址 | 大小 | 用途 |
| --- | --- | --- | --- |
| `flash` | `0x80000000` | 1 MiB | 向量表、代码和只读数据 |
| `sram` | `0x80100000` | 1 MiB | 数据、BSS、任务栈和堆 |
| `ocram` | `0x20200000` | 32 KiB | ENET1/FEC DMA 描述符和收发缓冲区 |

其中 OCRAM 区仅用于 FEC DMA。该区域在启动时由 MPU 配置为非缓存、可读写、不可执行，
避免 CPU 缓存与 DMA 访问同一缓冲区时产生数据不一致。

## 4. 板级初始化与基础外设

板级启动代码依次完成 Cortex-M7 运行环境建立、时钟配置、IOMUX、串口和板级设备注册。
系统启动后进入 NuttShell，可通过 `help`、`free`、`gpio_test`、`timer_test` 等命令完成
基础交互与验证。

### 4.1 串口、GPIO 与定时器

串口作为 NSH 控制台和调试输出通道；GPIO 用于板级指示和外设控制；定时器为系统调度、
语音事件轮询和应用任务提供时基。对应功能均通过板级初始化和 NuttX 标准驱动接口接入，
不依赖 A53 Linux 用户空间。

### 4.2 软件复位

项目在板级层实现 `BOARDIOC_RESET`。NSH 的 `reboot` 命令经 `board_reset()` 路由至
WDOG3，并通过复位控制器使 M7 重新初始化后恢复 NSH。该机制用于软件复位和启动异常
测试，不改变 A53 Linux 的启动流程。

## 5. I2C 离线语音与声光交互

离线语音模块连接到 M7 侧 I2C3，设备地址为 `0x0f`，总线速率为 100 kHz。板级代码独立
配置 I2C3 的时钟、SCL/SDA 引脚复用、SION 输入通路、开漏输出、上拉和施密特触发，
随后注册字符设备：

```text
/dev/voicesensor0
```

`ok8mp_voice_i2c.c` 根据模块通信协议提供版本、忙闲状态、词条数量和识别结果读取，
并支持词条配置、提示音、RGB 灯和蜂鸣器控制。驱动实现标准 `read()`、`write()`、
`ioctl()` 接口，并对总线忙、仲裁丢失和超时采用有限重试，供 NSH、uORB 应用和 Agent
Tool 共同访问。

语音识别在模块内部完成，M7 只读取固定词条对应的识别结果，因此基础语音输入与声光
反馈不依赖网络或云端 LLM。

## 6. ENET1/FEC 直接网络适配

项目未使用 A53 Linux 转发 M7 网络请求，而是在 Cortex-M7 上直接驱动 ENET1/FEC。M7
通过 MDIO 管理 Motorcomm YT8521 PHY，完成 PHY 识别、复位与自动协商；通过 RGMII
完成以太网数据收发；FEC 控制器使用 DMA 描述符和收发缓冲区处理以太网帧。

FEC DMA 与 M7 看到的 OCRAM 地址不同：M7 使用 `0x20200000`，FEC 总线主设备使用
`0x00900000` 别名。驱动对 DMA 地址进行转换，并通过 MPU 配置非缓存缓冲区、通过 RDC
开放 ENET1 TX/RX 总线主设备的 M7 域访问权限。这样可保证 DMA 地址、缓存一致性和资源
权限正确。

网络驱动注册为 NuttX 标准 `net_driver_s` 设备，接入 ARP、IPv4、ICMP、UDP、DNS 和
TCP 协议栈。在其上启用 Mbed TLS、TLS 1.2、可信根证书、X.509 证书链和服务器主机名
校验，使 M7 能够通过 HTTPS 安全访问云端 LLM API。

M7 运行直接网络功能时，ENET1/PHY 由 M7 使用；A53 Linux 仅用于镜像传输和 U-Boot
启动，不作为 M7 网络代理。

## 7. uORB 语音事件链路

`source/apps/examples/ok8mp_voice_uorb/` 中的 `voice_uorb` 后台程序定期读取语音模块
状态，将有效识别结果发布为 `sensor_voice_command` uORB 主题。消息包含时间戳、命令
ID、有效标志、模块忙闲状态和词条数量。

桥接程序仅在识别结果从空闲值变化为有效命令时发布事件，并在结果恢复空闲前保持锁存，
避免轮询过程中重复发布同一指令。上层应用和 Agent Tool 只需订阅主题即可获得语音事件，
无需直接了解 I2C 寄存器和通信过程。

固定离线指令可由本地逻辑直接完成 RGB 灯反馈；联网时，Agent 还可读取事件并结合网络、
外设状态进行进一步分析。

## 8. AI Agent 与平台专用扩展

openvela 官方 `packages/ai_agent` 框架被编译进 `nsh-agent` 镜像，提供 CLI、DeepSeek
后端、TLS 连接、ReAct 多轮推理、Markdown Skill 和 C 语言 Tool 调用能力。

围绕 OK8MP 实际硬件，项目增加以下平台专用 Tool：

- 语音模块状态查询；
- uORB 语音事件等待；
- RGB 灯和蜂鸣器控制；
- 网卡、网关、DNS、TLS/X.509、HTTPS 和 LLM 服务诊断。

`ok8mp-voice-light-control` Skill 规定语音模块、事件与声光控制流程；
`ok8mp-network-guardian` Skill 规定网络巡检与反馈流程。Tool 在 M7 上执行实际设备访问，
LLM 根据中间结果决定后续工具调用并生成诊断结论。

运行、配置 API Key 和 Tool 验证方法见[AI Agent 使用说明](AI_Agent使用说明.md)。

## 9. 构建、启动与验证资料

- 从源码构建、传输镜像和 U-Boot 启动步骤见[编译与上板指南](编译与上板指南.md)。
- AI Agent、Skill、Tool 的运行与安全注意事项见[AI Agent 使用说明](AI_Agent使用说明.md)。
- XTS 分项测试、测试镜像、配置、校验和及原始记录位于 `tests/xts/` 和
  `firmware/xts/`。
- 项目总体设计、功能说明和测试结论见仓库根目录的 `项目说明书.pdf`。

以上文件共同构成从源码、构建配置、发布镜像到实板验证记录的复现材料。
