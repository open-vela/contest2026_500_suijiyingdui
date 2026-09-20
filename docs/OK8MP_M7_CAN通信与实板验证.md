# OK8MP-M7 CAN 通信与实板验证

## 1. 台架组成

测试台架由飞凌 OK8MP、ESP32-S3、CAN 收发器模块和一对 CAN 双绞线组成。OK8MP 使用 P29 的 CAN1_H、CAN1_L 和 ISOGND，M7 直接使用 FlexCAN1；ESP32-S3 使用 TWAI 外设，GPIO4 连接收发器 RX，GPIO5 连接收发器 TX。两端以 500 kbit/s Classic CAN 通信。

CANH 与 CANL 必须对应连接，并与 ISOGND 共地。终端电阻根据总线两端既有硬件确定。本次台架已经具备终端匹配，不额外并联 120 Ω 电阻。

## 2. 软件组成

M7 启动后，FlexCAN1 注册为 `/dev/can0`。`can_service` 负责控制器状态、收发排队和恢复；`can_monitor` 监视 ESP32 的 `0x180` 周期状态帧；`can_diagnostic` 发送项目定义的主动诊断帧。ESP32-S3 工程位于 `app/ok8mp_m7_services/esp32_can_ecu/`，通过 PlatformIO 构建和烧录。

主动诊断报文如下：

```text
OK8MP M7 -> ESP32-S3: 0x700  [3]  A5 01 5A
ESP32-S3 -> OK8MP M7: 0x708  [4]  5A 01 5A 01
```

状态帧使用标准 ID `0x180`、DLC 8。帧内包含 ECU 状态、16 位递增序号和冷却液温度。监视器检查 ID、DLC、帧间隔、序号和接收超时。

## 3. 验收步骤

1. 烧录 ESP32-S3 ECU 程序，在串口输入 `t` 启动周期状态帧。
2. 启动 M7，确认 `/dev/can0` 存在，依次执行 `can_service start`、`can_service recover` 和 `can_monitor start`。
3. 使用 `can_monitor health` 检查状态。收到有效 `0x180` 帧时返回 `healthy`。
4. 执行 `can_diagnostic`。ESP32 串口应记录 `RX id=0x700 dlc=3 data=A5 01 5A`，M7 应校验 `0x708` 响应并返回 `"ok":true`。
5. 在 ESP32 串口分别输入 `p`、`j`、`r`，检查 M7 对周期异常、序号异常和超时的报告；恢复 `t` 后状态应恢复为 `healthy`。
6. 依次运行 `can_ecu isotp-test`、UDS、DTC 和 OBD-II 命令，验证 ISO-TP、UDS 和 OBD-II 诊断功能。

具体命令和预期输出见 [操作手册](操作手册.md) 与 [NSH/CAN 实测记录](../test-results/nsh-can/实测记录.md)。

## 4. 已验证范围

已在实板验证 `/dev/can0`、CAN 服务恢复、周期状态监测、物理断线超时、主动请求与应答、ISO-TP 多帧传输、UDS 状态与故障码、OBD-II PID、故障码和 VIN。验证镜像及 SHA-256 位于 `firmware/`。
