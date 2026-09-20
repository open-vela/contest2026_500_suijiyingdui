# 面向车辆与工程机械的 OK8MP-M7 openvela 智能终端与边云协同诊断系统

## 作品简介

作品运行在飞凌 OK8MP 的 i.MX8M Plus Cortex-M7 实时核上，按路径 A 完成 openvela 的新硬件适配。M7 可由 U-Boot 独立加载并进入 NSH，完成串口交互、I2C 离线语音模块访问、RGB 灯和蜂鸣器控制、uORB 语音事件传递、以太网安全通信，以及基于 FlexCAN1 的状态监测和 ECU 诊断。

项目使用 ESP32-S3 作为模拟 ECU。OK8MP 通过 P29 的 CAN1_H/CAN1_L 与 ESP32-S3 建立 500 kbit/s Classic CAN 通信。实板已完成 `/dev/can0` 设备访问、周期状态帧监测、主动链路诊断、ISO-TP 多帧传输、UDS 状态与 DTC 查询，以及 OBD-II PID、故障码和 VIN 查询。已验证镜像、校验和、接线和测试记录均包含在本仓。

网络可用时，M7 通过 TCP/IP、DNS、TLS 1.2、X.509 校验和 HTTPS 访问云端模型。openvela AI Agent 已用于语音模块和网络状态的 Tool 调用。CAN 的底层通信和诊断功能以 NSH 实板测试为验收依据；本提交不将仍在单独回归的 Agent CAN 诊断机制写入完成项。

## 选题方向

新硬件适配。本作品围绕 OK8MP Cortex-M7 建立启动、DDR 链接、时钟、IOMUX、串口、GPIO、定时器、I2C、ENET1/FEC 与 FlexCAN1 的板级能力，并在此基础上运行 openvela 的网络、事件和智能应用组件。

## 目录结构

```text
board/ok8mp_m7_openvela/          OK8MP-M7 板级代码、配置、链接脚本和芯片端口快照
app/ok8mp_m7_services/            项目应用覆盖代码、Agent 覆盖代码和 ESP32-S3 模拟 ECU 工程
docs/                             平台移植、源码集成、构建上板和 CAN 实板验证文档
test-results/nsh-can/             本次 NSH/CAN 功能验收记录
tests/xts/                        XTS 测试记录
firmware/                         已验证的 CAN/ISO-TP/UDS/OBD-II 镜像及 SHA-256
videos/s+演示视频说明与下载链接
submission/                       《随机应队_2026 首届 openvela AI 硬件开发者大赛》项目说明书
logs/                             官方 AI Coding 日志目录，保留给按组委会工具导出的真实记录
```

## 运行方式

1. 按大赛说明在 openvela 工作区根目录完成 `repo init` 和 `repo sync`。本仓的 manifest 将 `board/ok8mp_m7_openvela` 映射到 `vendor/openvela/boards/contest2026_500_ok8mp_m7_openvela`，将 `app/ok8mp_m7_services` 映射到 `packages/demos/contest2026_500_ok8mp_m7_services`。
2. 阅读 [docs/源码集成说明.md](docs/源码集成说明.md)，将 `port_snapshot` 和 `overlays` 中的快照按原始相对路径集成到完整 openvela 工作树。该步骤保留了队伍仓和公共仓的边界。
3. 已验证发布镜像为 `firmware/nuttx-ok8mp-m7-can-uds-obd-tested.bin`，SHA-256 为 `1c3a6baaf3b4fb7ddf00fe9b5c345f6971704a77380eb6a8dd464f77a1a592a5`。传输、U-Boot 启动和 NSH 验收命令见 [docs/操作手册.md](docs/操作手册.md)。
4. CAN 台架的 ESP32-S3 固件位于 `app/ok8mp_m7_services/esp32_can_ecu/`。完整接线、PlatformIO 烧录和诊断协议见 [docs/OK8MP_M7_CAN通信与实板验证.md](docs/OK8MP_M7_CAN通信与实板验证.md)。

## AI Coding 使用说明

开发过程中，AI 用于检索 i.MX8MP 资料、分析启动和外设配置、梳理 CAN 报文与诊断流程、辅助阅读实板日志和形成测试文档。涉及镜像加载、CAN 收发、协议响应和网络通信的结论均以 U-Boot、NSH、ESP32 串口或 Linux `candump` 的实际输出复核。

`logs/` 只用于存放按组委会归集工具导出的真实会话日志。本次提交不使用示例日志或手工生成的日志文件。
