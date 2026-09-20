# XTS 专用镜像说明

每个目录均包含相应测试配置的 `nuttx.bin` 与 `defconfig`。烧录或加载前请执行所在目录的 `shasum -a 256 -c SHA256SUMS` 校验文件完整性。

| 配置目录 | 主要用途 |
| --- | --- |
| `xts-basic` | 基础 NSH、文件系统、GPIO、定时器等测试 |
| `xts-cmocka` | CMocka 内存、调度、系统调用及 I2C 驱动测试 |
| `xts-cxx` | C++ 基础与功能测试 |
| `xts-nist` | RNG/NIST STS 测试 |
| `xts-remaining` | MD5、RAM 块设备、UART、重启等补充测试 |
| `xts-crypto` | XTS 1.3.17 Crypto 功能测试 |

1.3.14 时间一致性测试使用 `../../competition/nuttx.bin`，对应记录见 `../../tests/xts/cases/1.3.14_时间一致性测试.txt`。
