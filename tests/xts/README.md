# OK8MP Cortex-M7 XTS 测试证据

本目录按 openvela XTS 用例编号保存 OK8MP Cortex-M7 实机测试证据。

- `cases/`：每项用例的测试条件、镜像、命令、判定和串口原始输出；
- `raw_logs/`：补充原始日志；
- `tools/`：UART/YMODEM 等 Mac 主机辅助脚本；
- `00_XTS测试记录索引.txt`：测试状态和镜像索引；
- `firmware/xts/`：按 XTS 配置分别保存的可复现镜像、`defconfig` 与校验和。

`firmware/competition/nuttx.bin` 是比赛最终演示镜像。时间一致性测试使用该镜像；其余 XTS 测试使用 `firmware/xts/` 中与记录文件声明一致的专用镜像。所有记录均来自 OK8MP Cortex-M7 实机串口输出，Mac 主机仅承担串口连接、文件传输或日志保存。
