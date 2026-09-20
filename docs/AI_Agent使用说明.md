<!-- SPDX-License-Identifier: Apache-2.0 -->

# OK8MP Cortex-M7 openvela AI Agent 使用说明

## 1. 适用范围

本文说明比赛演示固件中的 openvela AI Agent 如何完成网络检查、LLM 配置、基础对话、
语音事件读取和声光控制。使用前应按[编译与上板指南](编译与上板指南.md)启动已发布的
`firmware/competition/nuttx.bin`，或启动从源码构建生成的 `source/nuttx/nuttx.bin`，并进入
`nsh>` 提示符。

该 Agent 运行在 OK8MP 的 Cortex-M7 上。M7 负责字符设备访问、uORB 事件订阅、网络
状态采集和 RGB 灯、蜂鸣器控制；云端 LLM 负责自然语言理解、任务规划和结果汇总。

## 2. 网络与时间前置条件

首先确认 M7 侧网络接口、默认路由和 DNS 可用：

```text
ifconfig eth0
route ipv4
ping -c 3 1.1.1.1
nslookup api.deepseek.com
```

TLS 证书校验依赖有效系统时间。优先使用 NTP 校时：

```text
ntpcstart
sleep 15
ntpcstatus
date -u
```

若当前网络环境无法获得 NTP 样本，可在主机获取当前 UTC 时间后，在 NSH 中按下列格式
设置：

```text
date -u -s "<Mon> <DD> <HH:MM:SS> <YYYY>"
date -u
```

完成校时后，可执行 HTTPS 检查：

```text
https_client get example.com /
```

成功时应显示 TLS 已建立、证书链和主机名校验通过，以及 `HTTP status=200`。系统保持
严格的 X.509 证书和主机名校验；不应通过关闭证书验证来绕过时间、DNS 或根证书问题。

## 3. 启动与配置 Agent

在 NSH 中启动 Agent：

```text
ai_agent
```

启动后进入 `vela>` CLI。先查看网络状态和当前配置：

```text
config_show
```

确认 `eth0` 已获得地址且网络状态为 connected 后，使用现场 API Key 配置 DeepSeek：

```text
set_llm deepseek <API_KEY>
```

API Key 只能在设备现场输入，不应写入源码、固件、文档、截图或仓库。当前 `/data` 为
tmpfs，设备重启或断电后需重新配置 Key。

## 4. 基础对话验证

在 `vela>` 中输入：

```text
ask BASIC_DIALOG_TAKE_01: Reply exactly OK8MP_AGENT_OK
```

若 Agent 完成 TLS 连接并返回 `OK8MP_AGENT_OK`，说明 CLI 交互、LLM 后端、M7 网络访问、
HTTPS 请求和结果回传均正常。一次 `ask` 请求完成前应等待最终 `[Agent]` 输出后再输入
下一条，避免串口输出交错。

## 5. 语音模块与 uORB 事件

语音模块注册为 `/dev/voicesensor0`。在 NSH 中可先查询模块状态：

```text
voice
```

启动语音事件桥接程序并查看运行状态：

```text
voice_uorb start
voice_uorb status
voice_uorb listen 1
```

`voice_uorb` 将离线识别结果发布为 `sensor_voice_command` 主题。执行 `listen 1` 后，
在等待时间内说出已配置的固定词条；收到新事件时应显示命令 ID、有效标志、模块状态和
词条数量。监听超时只表示规定时间内未收到新的语音事件。

## 6. Skill 与硬件 Tool

执行下列命令可查看已加载 Skill：

```text
ask /skill
```

其中平台专用 Skill 为：

- `OK8MP Voice Light Control`：语音模块状态查询、语音事件等待、RGB 灯和蜂鸣器控制；
- `OK8MP Network Guardian`：网卡、网关、DNS、TLS/X.509、HTTPS 和 LLM 服务巡检。

Agent 已注册语音状态查询、语音事件等待、RGB 灯控制、蜂鸣器控制和网络诊断等 C 语言
Tool。Tool 会直接访问 M7 字符设备、uORB 事件或网络状态，而不是返回模拟结果。

例如，可以使用自然语言完成语音模块检查和灯光控制：

```text
ask Check the voice module, then set its light to purple.
```

也可以要求 Agent 检查终端是否能够安全访问外网，并根据结果给出文字和声光反馈。具体
测试记录见 `tests/xts/` 与项目说明书。

## 7. 使用注意事项

- 网络诊断和云端 LLM 调用依赖以太网、DNS、系统时间和有效证书；离线语音、本地 uORB
  事件和固定声光反馈不依赖云端服务。
- 语音模块通过 M7 侧 I2C3 连接，地址为 `0x0f`。若 `voice` 无法读取状态，应优先检查
  模块供电、地线、SCL/SDA 接线和 I2C 总线状态。
- 使用 `voice_uorb` 时，后台桥接程序会访问语音模块。进行底层 I2C 调试前应先执行
  `voice_uorb stop`，避免多个程序同时访问同一外设。
- 修改源码后，应通过 `sh scripts/build.sh competition` 进行串行重建。该脚本会使用仓库
  内兼容脚本完成构建所需的锁操作。
- Agent 的网络请求、Skill 解析和 Tool 调用均会输出串口日志；日志用于现场定位，真实
  API Key 不应出现在保存的日志或截图中。
