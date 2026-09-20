# 已验证镜像

`nuttx-ok8mp-m7-openvela-baseline-tested.bin` 是原项目的基线镜像，用于验证 OK8MP Cortex-M7 的启动、I2C 离线语音模块、uORB、安全网络和 AI Agent 基础能力。该镜像不包含后续 CAN 功能。

`nuttx-ok8mp-m7-can-uds-obd-tested.bin` 用于 CAN 台架验证，覆盖 FlexCAN1、CAN service、周期帧监测、主动链路诊断、ISO-TP、UDS 和 OBD-II。

两个文件的 SHA-256 校验和见 `SHA256SUMS`。上板命令与验收流程见 `../docs/操作手册.md`。

## XTS 验证产物

`xts/` 保存 XTS 测试记录所引用的专用镜像、`defconfig`、运行配置和各自的 SHA-256 校验文件。`competition/nuttx.bin` 保留了原测试记录使用的兼容路径，内容与基线镜像一致。它们用于对应 `../tests/xts/` 中的实机测试记录，不作为额外的发布镜像。
