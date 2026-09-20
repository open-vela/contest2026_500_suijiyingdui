# OK8MP i.MX8M Plus Cortex-M7 板级适配

该目录保存飞凌 OK8MP 的 Cortex-M7 板级代码：启动、DDR 链接、时钟与 IOMUX、串口、GPIO、定时器、I2C 语音模块、ENET1/FEC 与 FlexCAN1 初始化。`configs/nsh`、`configs/nsh-net` 与 `configs/nsh-agent` 分别对应基础 NSH、网络和 AI Agent 配置。

`port_snapshot/` 中保存与本板级目录配套的 i.MX8MP 芯片端口快照。该快照用于代码审查与复现；公共 NuttX 代码的长期合入应采用独立 PR。
