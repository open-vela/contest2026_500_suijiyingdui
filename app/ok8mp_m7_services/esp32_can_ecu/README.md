# ESP32-S3 CAN 模拟 ECU

本工程将 ESP32-S3 N16R8 配置为 500 kbit/s 的 Classic CAN 节点，用于与飞凌
OK8MP Cortex-M7 的板载 FlexCAN1（P29 `CAN1_H/CAN1_L`）进行台架收发验证。

## 当前接线

| 连接 | 线材 | 说明 |
| --- | --- | --- |
| ESP32 GPIO4 → 收发器 `RX` | 短杜邦线 | ESP32 发送到 CAN 收发器 |
| ESP32 GPIO5 ← 收发器 `TX` | 短杜邦线 | CAN 收发器接收至 ESP32 |
| ESP32 `3V3` → 收发器 `3.3V` | 短杜邦线 | 仅使用 3.3 V 逻辑与电源 |
| ESP32 `GND` → 收发器 `GND` → P29 `ISOGND` | 杜邦线/普通导线 | 为隔离 CAN 侧提供参考地 |
| 收发器 `CANH` → P29 `CAN1_H` | 黑色双绞线芯 | 保持 CANH 对应 CANH |
| 收发器 `CANL` → P29 `CAN1_L` | 白色双绞线芯 | 保持 CANL 对应 CANL |

收发器模块的 `120R` 跳帽当前保持接通。该接法适用于短距离台架调试；正式两端
CAN 总线应在两端各配置一个 120 Ω 终端电阻，并使用可靠的接线端子。

## 固件行为

CAN 使用标准帧、500 kbit/s、`GPIO4` 为 TX、`GPIO5` 为 RX。烧录后进入**正常
ACK 模式**，默认不周期发送报文，因此可稳定验证 OK8MP → ESP32，同时可响应 M7
主动诊断请求：

```text
ESP32 CAN simulated ECU v3: NORMAL ACK mode, 500 kbit/s, TX=GPIO4, RX=GPIO5
```

串口命令：

| 输入 | 行为 |
| --- | --- |
| `t` | 开始每秒发送一帧模拟 ECU 状态报文 |
| `r` | 停止周期发送，但保持 ACK 和诊断响应能力 |
| `p` | 仅延迟下一状态帧 1500 ms，用于 M7 周期异常测试 |
| `j` | 仅跳变下一状态帧的计数器，用于 M7 计数器异常测试 |
| `x` | 仅损坏下一条诊断响应的 ECU 状态字节，用于 M7 响应异常测试 |

模拟 ECU 状态报文定义如下：

| 字段 | 值 |
| --- | --- |
| CAN ID | `0x180`（标准帧） |
| DLC | `8` |
| Byte 0 | `0x01`，ECU 正常状态 |
| Byte 1–2 | 递增序号，低字节在前 |
| Byte 3 | `60`，模拟冷却液温度（°C） |
| Byte 4–7 | `0x00` |

接收到 OK8MP 的报文时，USB CDC 串口输出格式为：

```text
RX id=0x001 dlc=1 data=00
```

其中 `0x001`、DLC `1`、数据 `00` 对应 NSH 命令 `can -n 1 -s` 的首帧。

主动诊断协议为项目台架协议，不是 UDS/ISO-TP：

| 方向 | 帧 |
| --- | --- |
| M7 → ESP32 | `0x700 [3] A5 01 5A` |
| ESP32 → M7（正常） | `0x708 [4] 5A 01 5A 01` |
| ESP32 → M7（输入 `x` 后） | `0x708 [4] 5A 01 5A 00` |

## 构建、烧录和串口监视

```sh
cd "/Users/qiugao/Desktop/Graduate_Documents/操作系统大赛/OK8MP_M7_决赛开发/CAN开发/esp32-can-ecu"
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem11201
~/.platformio/penv/bin/pio device monitor --port /dev/cu.usbmodem11201 --baud 115200
```

USB 重连后串口编号可能改变；先运行 `ls /dev/cu.usbmodem*`，再将命令中的端口改为
实际值。上传前需要按 `Ctrl+C` 关闭 monitor 以释放串口。

如上传时 ESP32-S3 未自动进入下载模式，按住 `BOOT`，短按一次 `RST`，继续按住
`BOOT` 约一秒后释放，再重试上传。

## 台架收发步骤

### OK8MP → ESP32

1. ESP32 保持默认正常 ACK、非周期发送模式；
2. 启动当前 `nuttx-can-diagnostic-test.bin`，或仅为历史单帧验证启动
   `nuttx-can-test.bin`；
3. 在 OK8MP NSH 执行：

   ```text
   nsh> can -n 1 -s
   ```

4. ESP32 串口出现 `RX id=0x001 dlc=1 data=00` 即表示 ESP32 收到 M7 标准 CAN 帧。

### ESP32 → OK8MP

1. 在 ESP32 串口监视器输入 `t`；
2. ESP32 输出 `TX id=0x180 seq=<N> result=0`；
3. `result=0` 表示本次报文得到另一 CAN 节点 ACK。后续由 OK8MP 的常驻 CAN
   监测程序读取 `0x180` 载荷并进行超时诊断。

## 当前实测状态

- 已完成 M7 → ESP32：ESP32 记录 `RX id=0x001 dlc=1 data=00`。
- 已完成 ESP32 → M7：M7 收到 `0x180` 的完整 8 字节状态帧，并由常驻 `can_monitor`
  验证 `healthy`、`period_error`、`counter_error` 与 `timeout`。
- 已完成主动诊断：ESP32 实际记录 `RX id=0x700 dlc=3 data=A5 01 5A`，并在正常和
  输入 `x` 的异常响应两种情况下，由 M7 分别得出 `healthy` 与 `response_error` JSON。
- 拔掉 CANH 后，ESP32 出现 `result=263` 与 `rxerr=129`，M7 同时得出
  `can_monitor: state=timeout` 和 `can_diagnostic: ecu_no_response`；接回后总线恢复。
