# OK8MP-M7 服务与 ECU 模拟器

`overlays/apps/examples/` 包含项目新增的语音、网络、CAN service、CAN monitor、CAN diagnostic 和 CAN ECU 命令源码。`overlays/packages/ai_agent/` 保存 OK8MP 专用 AI Agent Tool、注册表修改和 Markdown Skill。`esp32_can_ecu/` 是基于 PlatformIO 的 ESP32-S3 模拟 ECU 程序，通过 TWAI 和外接收发器与 OK8MP P29 CAN1 接口通信。

该目录在 manifest 中映射到 `packages/demos/contest2026_500_ok8mp_m7_services/`，方便评委在一个队伍仓库中检查全部自定义服务源码；其与公共 `apps`、`packages/ai_agent` 的对应关系见 `docs/源码集成说明.md`。
