#!/usr/bin/env python3
############################################################################
# tools/split_xts_records.py
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

"""Split the OK8MP XTS console log into one UTF-8 text file per test."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class Image:
    name: str
    relative_path: str
    size: int
    sha256: str
    note: str = ""


IMAGES = {
    "basic": Image(
        "xts-basic",
        "firmware/xts/xts-basic/nuttx.bin",
        320492,
        "a281ebe53bf6e1abe5b702d8429866ff7bf5e7b8f5739b6fc86fd63e7676bbf0",
        "基础NSH、libc、文件系统、GPIO、Timer及I2C测试镜像",
    ),
    "cmocka": Image(
        "xts-cmocka",
        "firmware/xts/xts-cmocka/nuttx.bin",
        393504,
        "4389049a66365c02643ddf51086f2bc896bd6add5eaa9e11fda3ab747c4a2beb",
        "CMocka内存、调度、系统调用及语音驱动测试镜像",
    ),
    "cxx": Image(
        "xts-cxx",
        "firmware/xts/xts-cxx/nuttx.bin",
        370012,
        "961e18482a6df94ee54744e26b95bb22ecca561b229bda2799b1d500714086ec",
        "C++基础与容器/RTTI测试镜像",
    ),
    "nist": Image(
        "xts-nist",
        "firmware/xts/xts-nist/nuttx.bin",
        371332,
        "f57ce4018614714eaad2295d9e2e7104be766ac13888c3ed84c550497142bcb3",
        "NIST STS随机数统计测试镜像",
    ),
    "remaining": Image(
        "xts-remaining",
        "firmware/xts/xts-remaining/nuttx.bin",
        410928,
        "26976a902227fc054982c991395fe024d6a1d696ea8cb83f5e24ecd12306476c",
        "mm、MD5、RAM块设备、UART、YMODEM、Reboot及稳定性补充测试镜像",
    ),
    "final": Image(
        "比赛最终版本",
        "firmware/competition/nuttx.bin",
        544128,
        "02ffb2a7ad8ee94da7542b1ca41b369f566e047fe90051331de3078667f73a2d",
        "网络、TLS/X.509、HTTPS及AI Agent实机演示镜像（现存归档版本）",
    ),
}


@dataclass
class Record:
    number: str
    title: str
    status: str
    result: str
    images: list[str]
    commands: list[str] = field(default_factory=list)
    dependencies: list[str] = field(default_factory=list)
    sources: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    ranges: list[tuple[int, int]] = field(default_factory=list)
    extra_file: str | None = None
    fixed_evidence: str = ""


def common_board_files() -> list[str]:
    return [
        "nuttx/boards/arm/mx8mp/ok8mp-m7/（BSP、Kconfig、defconfig、链接脚本及板级初始化）",
        "对应测试镜像nuttx.bin（发布/复现附件，不替代源码）",
    ]


RECORDS = [
    Record(
        "1.1.1", "内存管理", "完成 / PASS", "CMocka 8项全部通过",
        ["cmocka"], ["cmocka --suite mm_test"],
        ["apps/testing/cmocka/", "apps/testing/cmocka/cmocka/（CMocka 1.1.5源码）"],
        ["xts测试.txt第2232～2252行"], ranges=[(2232, 2252)],
    ),
    Record(
        "1.1.2", "系统调度", "完成 / PASS", "CMocka 16项全部通过",
        ["cmocka"], ["cmocka --suite sched_test"],
        ["apps/testing/cmocka/", "apps/testing/cmocka/cmocka/（CMocka 1.1.5源码）"],
        ["xts测试.txt第2257～2294行"], ranges=[(2257, 2294)],
    ),
    Record(
        "1.1.3", "系统调用", "完成 / PASS", "CMocka 83项全部通过",
        ["cmocka"], ["cmocka --suite syscall_test"],
        ["apps/testing/cmocka/", "apps/testing/cmocka/cmocka/（CMocka 1.1.5源码）"],
        ["xts测试.txt第2297～2478行"],
        ["日志中的非法文件描述符、非法协议参数属于负向用例；最终[OK]表示内核正确拒绝。"],
        ranges=[(2297, 2478)],
    ),
    Record(
        "1.1.4", "ostest", "完成 / PASS", "完整运行并以Exiting with status 0结束",
        ["basic"], ["ostest"], ["apps/testing/ostest/"],
        ["xts测试.txt第46～1384行"], ranges=[(46, 1384)],
    ),
    Record(
        "1.1.5", "getprime", "完成 / PASS", "线程正常结束，记录耗时330 ms",
        ["basic"], ["getprime"], ["apps/testing/sched/getprime/"],
        ["xts测试.txt第1387～1395行"], ranges=[(1387, 1395)],
    ),
    Record(
        "1.1.6", "Kernel-mm内存测试", "完成 / PASS",
        "随机分配、释放及mallinfo检查完成，最终输出TEST COMPLETE",
        ["remaining"], ["mm"], ["apps/testing/mm/"],
        ["mm测试.txt完整记录"], extra_file="mm测试.txt",
    ),
    Record(
        "1.1.7", "scanftest", "完成 / PASS", "164项通过、0项失败",
        ["basic"], ["mkdir /tmp", "mount -t tmpfs /tmp", "scanftest"],
        ["apps/testing/libc/scanftest/"], ["xts测试.txt第1397～1575行"],
        ranges=[(1397, 1575)],
    ),
    Record(
        "1.1.8", "C语言基础测试", "完成 / PASS", "hello输出Hello, World!!",
        ["basic"], ["hello"], ["apps/examples/hello/"],
        ["xts测试.txt第44～45行"], ranges=[(44, 45)],
    ),
    Record(
        "1.1.9", "C++基础测试", "完成 / PASS",
        "helloxx完成动态、实例及静态HelloWorld测试",
        ["cxx"], ["helloxx"], ["apps/examples/helloxx/"],
        ["xts测试.txt第1902～1916行"], ranges=[(1902, 1916)],
    ),
    Record(
        "1.1.10", "popen", "完成 / PASS", "popen(help)读取完成并正常执行pclose()",
        ["basic"], ["popen"], ["apps/testing/libc/popen/", "apps/system/popen/"],
        ["xts测试.txt第1758～1780行"], ranges=[(1758, 1780)],
    ),
    Record(
        "1.1.11", "pipe", "完成 / PASS", "FIFO、互锁、pipe及重定向测试全部通过",
        ["basic"], ["pipe"], ["apps/examples/pipe/"],
        ["xts测试.txt第1578～1755行"], ranges=[(1578, 1755)],
    ),
    Record(
        "1.1.12", "Kernel-md5值测试", "完成 / PASS",
        "同一文件连续计算100次，100个MD5摘要完全一致",
        ["remaining"],
        ["echo OK8MP_XTS_MD5_TEST > /tmp/1.txt", "md5_test -f /tmp/1.txt -c 100"],
        ["apps/testing/libc/md5_test/"],
        ["1.1.12__Kernel-md5值测试.txt完整记录"],
        extra_file="1.1.12__Kernel-md5值测试.txt",
    ),
    Record(
        "1.1.13", "C++功能测试", "完成 / PASS",
        "std::vector、std::map、RTTI和继承测试正常",
        ["cxx"], ["cxxtest"], ["apps/testing/cxx/cxxtest/"],
        ["xts测试.txt第1917～1923行"], ranges=[(1917, 1923)],
    ),
    Record(
        "1.2.1", "Reboot启动异常测试", "待完成（当前镜像复位接口不可用）",
        "实机执行reboot返回“boardctl failed: 2”，没有发生复位",
        ["remaining"], ["reboot"], common_board_files(),
        ["2026-07-26实机记录：nsh> reboot / boardctl failed: 2"],
        [
            "reboot命令已编译，但OK8MP板级代码尚未提供可用的board_reset()实现。",
            "需要实现板级复位、重新编译镜像并验证自动加载M7后，才能完成该项。",
        ],
    ),
    Record(
        "1.2.2", "Cold boot启动异常测试", "完成（汇总结论）",
        "完全断电再上电后M7正常进入NSH，串口日志无异常",
        ["remaining"], ["完全断电3～5秒", "重新上电并保存完整启动日志"], common_board_files(),
        ["原汇总表；完整cold boot串口原始日志需随最终证据一并归档"],
    ),
    Record(
        "1.2.3", "RAM统计", "完成", "free命令记录总量、已用、空闲及峰值",
        ["basic"], ["free"], common_board_files(),
        ["xts测试.txt第23～28行"], ranges=[(23, 28)],
    ),
    Record(
        "1.2.4", "Flash资源统计", "部分完成（需补硬件Flash占用说明）",
        "df记录了M7当前可见文件系统，但尚需补充nuttx.bin占用及eMMC分区容量",
        ["basic"], ["df"], common_board_files(),
        ["xts测试.txt第28～43行"],
        [
            "官方要求同时提供厂商硬件Flash资源占用。当前M7未挂载eMMC，单独df不能反映固件所在分区。",
            "应补充nuttx.bin字节数、eMMC启动分区总容量及占用比例。",
            "该项是资源统计；不要与1.3.5 Flash设备功能测试混为同一项。",
        ],
        ranges=[(28, 43)],
    ),
    Record(
        "1.3.1", "烧写测试", "完成", "nuttx.bin可由U-Boot加载并启动，正常进入NSH",
        ["basic"],
        ["mmc dev 2", "fatload mmc 2:1 0x80000000 nuttx.bin", "bootaux 0x80000000"],
        common_board_files(), ["xts测试.txt开头的NSH、uname及设备节点记录"],
        ranges=[(1, 44)],
    ),
    Record(
        "1.3.2", "RAM文件系统读写", "完成 / PASS", "fstest共20项通过、0项失败",
        ["basic"], ["fstest -n 10 -m /tmp"], ["apps/testing/fs/fstest/"],
        ["xts测试.txt第1781～1792行"], ranges=[(1781, 1792)],
    ),
    Record(
        "1.3.3", "RAM读写测试", "完成 / PASS",
        "ramtest完成Marching ones/zeroes、固定模式及地址模式检查",
        ["basic"], ["ramtest -w -s 65536"], ["apps/testing/mm/ramtest/"],
        ["xts测试.txt第1793～1801行"], ranges=[(1793, 1801)],
    ),
    Record(
        "1.3.4", "RAM随机块设备读写", "完成 / PASS",
        "1.024 MB RAM块设备的stress、single_write、cache_write三项全部通过",
        ["remaining"],
        ["mkrd -m 10 -s 1000 1024", "cmocka_driver_block -m /dev/ram10"],
        ["apps/testing/drivers/drivertest/", "nuttx/drivers/ramdisk.c"],
        ["RAM随机块设备读写.txt完整记录"],
        ["记录中的一次“cmocka_driver_block -m”错误仅因缺少路径参数，不是测试失败。"],
        extra_file="RAM随机块设备读写.txt",
    ),
    Record(
        "1.3.5", "Flash功能测试", "N/A申请待官方确认",
        "未执行独占Flash块设备功能测试",
        [], [],
        ["若后续补测：nuttx/drivers/mmcsd/及OK8MP uSDHC板级适配源码"],
        ["N/A说明文件"],
        ["i.MX8M Plus和OK8MP具备uSDHC/eMMC硬件；N/A原因是当前M7 BSP尚未适配USDHC/MMCSD并注册独占块设备节点，不应表述为硬件不支持。"],
    ),
    Record(
        "1.3.6", "GPIO功能测试", "等效硬件验证完成；官方CMocka回环待补或确认替代",
        "4路用户LED依次亮灭两轮，串口输出与实物变化一致；未执行官方双GPIO回环CMocka命令",
        ["basic"], ["gpio_test 8 250"],
        ["apps/examples/ok8mp_gpio_test/", "nuttx/boards/arm/mx8mp/ok8mp-m7/"],
        ["xts测试.txt第1802～1821行；配套实物视频"],
        ["官方步骤为cmocka_driver_gpio连接两个GPIO设备节点。当前自定义LED测试可证明输出功能，但若严格按官方命令验收，需要补GPIO输入回环或取得替代认可。"],
        ranges=[(1802, 1821)],
    ),
    Record(
        "1.3.7", "I2C功能测试及语音外设", "完成 / PASS",
        "I2C3读取0x0f设备成功，语音驱动CMocka 2项通过",
        ["basic", "cmocka"],
        ["i2c bus", "i2c get -b 3 -a 0x0f -r 0x0b", "cmocka --suite ok8mp_voice"],
        [
            "OK8MP离线语音模块NuttX I2C驱动源码",
            "语音模块NSH测试应用源码",
            "apps/testing/cmocka/中的ok8mp_voice测试用例",
            "nuttx/boards/arm/mx8mp/ok8mp-m7/中的I2C3时钟及IOMUX配置",
        ],
        ["xts测试.txt第1837～1878行及2216～2227行；配套蓝灯视频"],
        ranges=[(1837, 1878), (2216, 2227)],
    ),
    Record(
        "1.3.10", "UART串口功能测试", "完成 / PASS",
        "115200波特率下10轮随机长度双向收发全部一致，CMocka 1项通过",
        ["remaining"],
        ["python tools/ok8mp_uart_cmocka.py /dev/cu.usbmodem58BE0307013 -b 115200 -t 10 -l 100"],
        [
            "tools/ok8mp_uart_cmocka.py（必须提交）",
            "apps/testing/drivers/drivertest/中的UART测试源码",
            "Python依赖：pyserial（建议记录requirements或虚拟环境安装命令，不提交.venv目录）",
        ],
        ["UART字符收发测试.txt完整记录"],
        extra_file="UART字符收发测试.txt",
    ),
    Record(
        "1.3.11", "UART文件传输功能测试", "部分完成；M7→Mac待完成",
        "YMODEM Mac→M7传输4096字节成功；M7→Mac当前卡在旧版脚本等待NSH命令回显",
        ["remaining"],
        [
            "python apps/system/ymodem/sbrb.py -t /dev/cu.usbmodem58BE0307013 -b 115200 --packet128 -s /tmp /tmp/ok8mp_uart_host_to_m7.bin",
            "python tools/run_sbrb_timeout.py -t /dev/cu.usbmodem58BE0307013 -b 115200 -r /tmp/ok8mp_uart_host_to_m7.bin",
        ],
        [
            "apps/system/ymodem/sbrb.py（包含--packet128补丁，必须提交）",
            "tools/run_sbrb_timeout.py（为旧版sbrb.py设置有限串口读取超时）",
            "apps/system/ymodem/ymodem.c、ymodem.h、rb_main.c、sb_main.c",
            "测试文件ok8mp_uart_host_to_m7.bin（可选证据附件，4096字节）",
            "Python依赖：pyserial（不提交.venv目录）",
        ],
        ["UART文件传输测试.txt；当前文件只保存Mac→M7原始输出"],
        [
            "原始文件MD5：8c812eec1cf931487b4584b82f4bf690。",
            "原始文件SHA-256：04ea459ab94054c99b6438d6ef2f1a53c2f1281ce0afe9ec61821080539817c5。",
            "提交前还应把M7端MD5及M7→Mac回传后的SHA-256比较原始输出追加到本文件。",
            "使用--packet128是因为当前UART4 RX环形缓冲区为256字节，默认1KB突发包会产生NAK。",
        ],
        extra_file="UART文件传输测试.txt",
    ),
    Record(
        "1.3.12", "RTC时钟功能测试", "N/A申请待官方确认",
        "当前无/dev/rtc0，未执行独立RTC设备读写测试",
        [], [],
        ["若后续补测：i.MX8MP SNVS/RTC lower-half、board bring-up及/dev/rtc0注册源码"],
        ["N/A说明文件"],
        ["i.MX8M Plus具备相关RTC/SNVS硬件资源；当前结论是BSP未适配，不应写成M7或芯片没有RTC硬件。1.3.14系统时间一致性不能完全替代RTC设备接口测试。"],
    ),
    Record(
        "1.3.13", "Timer定时器功能测试", "基础功能完成；官方驱动CMocka待补",
        "自定义timer_test的10次定时事件全部触发；未执行官方oneshot/timer设备节点CMocka测试",
        ["basic"], ["timer_test 10 1000"],
        ["apps/examples/ok8mp_timer_test/", "nuttx/boards/arm/mx8mp/ok8mp-m7/定时器配置"],
        ["xts测试.txt第1823～1836行"],
        ["官方要求根据arch alarm或arch timer执行cmocka_driver_oneshot/对应timer设备测试；当前timer_test不能完全替代该项。"],
        ranges=[(1823, 1836)],
    ),
    Record(
        "1.3.14", "时间一致性测试", "完成（汇总结论，证据需独立归档）",
        "汇总表记录断网静置24小时、每6小时记录一次，最终误差不超过2秒",
        ["final"], ["date -u（起点及每6小时记录）"],
        ["完整24小时时间戳日志（应作为提交附件）"],
        ["原汇总表；stability_12h.log中仅能看到部分时间点"],
        ["当前目录未找到覆盖完整24小时且包含基准时钟对照的独立日志，提交前应补入，避免只凭汇总结论判定。"],
    ),
    Record(
        "1.3.15", "Watchdog测试", "N/A申请待官方确认",
        "未注册/dev/watchdog0，未执行Watchdog超时复位测试",
        [], [],
        ["若后续补测：i.MX8MP WDOG lower-half、复位原因读取及/dev/watchdog0注册源码"],
        ["N/A说明文件"],
        ["i.MX8M Plus具备WDOG硬件；N/A原因是当前BSP驱动尚未适配，不应表述为M核硬件不支持。"],
    ),
    Record(
        "1.3.16", "RNG功能测试", "完成 / PASS",
        "NIST STS以400000 bit、10组数据运行15类统计测试，结果满足记录中的通过条件",
        ["nist"], ["nist_sts 400000"],
        [
            "apps/testing/drivers/rng/nist-sts/（NIST STS 2.1.2移植源码）",
            "NIST模板文件template9及experiments目录结构说明",
            "/dev/urandom对应的NuttX随机池实现及配置",
        ],
        ["RNG功能测试.txt完整记录"], extra_file="RNG功能测试.txt",
    ),
    Record(
        "1.3.17", "Crypto功能测试", "未完成官方用例（TLS链路已验证）",
        "已证明Mbed TLS、TLS 1.2和X.509链路可用，但未运行官方Crypto算法测试应用",
        ["final"],
        [
            "已完成：https_client get api.deepseek.com /",
            "待完成：按当前实现算法分别运行des3cbc/aescbc/aesctr/aesxts/hmac/hash/crc32/ecdsa测试应用",
        ],
        [
            "HTTPS测试客户端源码及CA证书集合",
            "Mbed TLS配置文件",
            "NuttX随机池及/dev/urandom配置",
            "网络/FEC驱动和OK8MP BSP配置",
            "补测时需提交apps/testing/crypto/及启用的CONFIG_TESTING_CRYPTO_*配置",
        ],
        ["OK8MP_M7_NuttX_操作手册.md中的TLS/X.509实机记录"],
        [
            "HTTP 401仅表示未携带API认证信息；TLS established且证书flags为0可证明安全网络链路。",
            "官方1.3.17要求Crypto框架算法应用正常结束并显示ok，因此HTTPS握手不能替代该项。",
        ],
    ),
    Record(
        "2.1.3", "Cold Boot启动时间测试", "完成 / PASS",
        "完全断电3～5秒后重新上电，10次启动平均3.67秒到达NSH，低于4秒门槛",
        ["remaining"],
        ["完全断电3～5秒后上电，记录首条启动日志到NuttShell(NSH)的时间，重复10次"],
        common_board_files(),
        ["2026-07-27 Cold Boot 10次测试平均值记录"],
        [
            "10次平均3.67秒满足不超过4秒的时间要求。",
            "提交附件仍建议保留10次逐次时长、最大值、最小值和带时间戳串口日志；人工输入U-Boot命令的时间不能计入。",
        ],
        fixed_evidence="""2026-07-27 Cold Boot启动时间测试：
启动方式：完全断电3～5秒后重新上电
计时终点：出现NuttShell(NSH)提示符
测试次数：10次
平均时间：3.67 s
门槛：<= 4 s
最终结论：PASS""",
    ),
    Record(
        "2.1.4", "Reboot启动时间测试",
        "U-Boot reset 10次平均达标；官方NSH reboot待完成",
        "U-Boot reset重复10次平均3.4秒，低于6秒门槛；但当前NSH reboot仍返回boardctl failed: 2",
        ["remaining"],
        ["正式测试：在NSH执行reboot，重复10次并使用串口时间戳"],
        common_board_files(),
        ["2026-07-27 U-Boot reset 10次测试平均值记录；待补NSH reboot测试"],
        [
            "U-Boot的forlinx=> reset不等同于官方要求的nsh> reboot，因此3.4秒证明热重启时间数值达标，但不能替代NSH复位接口测试。",
            "当前xts-remaining的nsh> reboot不能完成复位，需先适配i.MX8MP M7板级复位。",
            "提交附件仍建议保留10次逐次时长、最大值、最小值和带时间戳串口日志。",
        ],
        fixed_evidence="""2026-07-27 U-Boot热重启时间测试：
启动方式：U-Boot执行forlinx=> reset
计时终点：出现NuttShell(NSH)提示符
测试次数：10次
平均时间：3.4 s
门槛：<= 6 s
数值结论：PASS
官方方法结论：未完成（需要在NSH执行reboot并重复10次）""",
    ),
    Record(
        "3.1.1", "12小时待机稳定性测试", "已有长时间日志；需按官方条件重测或申请偏差",
        "已有长时间运行记录，但当前日志出现NTP_daemon，且补测配置未启用KASAN/show_info",
        ["remaining"],
        ["测试起点记录date -u、uname -a、free、ps；之后定期重复；12小时后记录结束状态"],
        ["stability_12h.log（必须作为测试附件提交）"],
        ["操作系统大赛/stability_12h.log完整记录"],
        [
            "官方条件是未配网、开启CONFIG_MM_KASAN和LOW_RESOURCE_TEST/show_info后静置12小时。",
            "现有日志出现NTP_daemon，说明不是严格未配网；xts-remaining配置中也未发现CONFIG_MM_KASAN。",
            "日志后部混入mm、MD5、RAM块设备等后续测试输出，应重测并保存纯净起止区间，或向官方申请测试偏差。",
        ],
    ),
]


def get_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def extract_ranges(lines: list[str], ranges: list[tuple[int, int]]) -> str:
    blocks = []
    for start, end in ranges:
        blocks.append("\n".join(lines[start - 1 : end]).rstrip())
    return "\n\n".join(blocks)


def format_record(record: Record, raw: str) -> str:
    output = [
        "openvela OK8MP Cortex-M7 XTS测试记录",
        "=" * 64,
        f"官方编号：{record.number}",
        f"测试名称：{record.title}",
        f"当前状态：{record.status}",
        f"结果摘要：{record.result}",
        "",
        "一、使用镜像",
    ]

    if record.images:
        for key in record.images:
            image = IMAGES[key]
            output.extend(
                [
                    f"- 镜像名称：{image.name}",
                    f"  文件路径：{image.relative_path}",
                    f"  文件大小：{image.size} bytes",
                    f"  SHA-256：{image.sha256}",
                    f"  用途说明：{image.note}",
                ]
            )
    else:
        output.append("- N/A：该项未运行测试镜像，原因见注意事项。")

    output.extend(["", "二、复现命令"])
    if record.commands:
        output.extend(f"- {command}" for command in record.commands)
    else:
        output.append("- N/A")

    output.extend(["", "三、需要随源码提交或随测试归档的其他文件"])
    if record.dependencies:
        output.extend(f"- {item}" for item in record.dependencies)
    else:
        output.append("- 无额外文件。")

    output.extend(["", "四、原始证据来源"])
    output.extend(f"- {item}" for item in record.sources)

    if record.notes:
        output.extend(["", "五、注意事项"])
        output.extend(f"- {item}" for item in record.notes)

    output.extend(["", "六、原始测试记录"])
    output.append(raw.rstrip() if raw.strip() else "当前没有可抽取的逐行原始日志，详见状态及注意事项。")
    output.append("")
    return "\n".join(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    output_dir = args.output_dir.resolve()
    main_log = get_lines(source_dir / "xts测试.txt")

    output_dir.mkdir(parents=True, exist_ok=True)
    generated = []
    for record in RECORDS:
        raw_parts = []
        if record.ranges:
            raw_parts.append(extract_ranges(main_log, record.ranges))
        if record.extra_file:
            extra_path = (source_dir / record.extra_file).resolve()
            if extra_path.exists():
                raw_parts.append(extra_path.read_text(encoding="utf-8", errors="replace"))
            else:
                raw_parts.append(f"[缺失证据文件] {extra_path}")
        if record.fixed_evidence:
            raw_parts.append(record.fixed_evidence)

        filename = f"{record.number}_{record.title}.txt"
        target = output_dir / filename
        if target.exists() and not args.force:
            raise FileExistsError(f"{target} exists; pass --force to replace")
        target.write_text(
            format_record(record, "\n\n".join(raw_parts)),
            encoding="utf-8",
        )
        generated.append((record, filename))

    index_lines = [
        "openvela OK8MP Cortex-M7 XTS测试记录索引",
        "=" * 64,
        "",
        "说明：",
        "1. 原始xts测试.txt及既有独立日志均保留，不删除、不覆盖。",
        "2. 每个官方用例对应一个独立UTF-8文本文件。",
        "3. 文件开头记录测试镜像、大小、SHA-256及需要提交/归档的辅助文件。",
        "4. 标记“待补算”的镜像摘要应在云端占位文件下载到本机后执行shasum -a 256补齐。",
        "5. .venv-uart虚拟环境不应提交；只提交Python脚本及依赖说明。",
        "",
        "镜像总表：",
    ]
    for image in IMAGES.values():
        index_lines.extend(
            [
                f"- {image.name}",
                f"  路径：{image.relative_path}",
                f"  大小：{image.size} bytes",
                f"  SHA-256：{image.sha256}",
            ]
        )

    index_lines.extend(["", "用例文件："])
    for record, filename in generated:
        index_lines.append(
            f"- {record.number} {record.title} | {record.status} | {filename}"
        )

    index_lines.extend(
        [
            "",
            "建议提交的主机端辅助工具：",
            "- openvela-ok8mp-xts/tools/ok8mp_uart_cmocka.py",
            "- openvela-ok8mp-xts/tools/run_sbrb_timeout.py",
            "- openvela-ok8mp-xts/apps/system/ymodem/sbrb.py（包含--packet128）",
            "- openvela-ok8mp-xts/XTS_REMAINING_TEST_GUIDE.md",
            "",
        ]
    )
    index_target = output_dir / "00_XTS测试记录索引.txt"
    if index_target.exists() and not args.force:
        raise FileExistsError(f"{index_target} exists; pass --force to replace")
    index_target.write_text("\n".join(index_lines), encoding="utf-8")

    print(f"Generated {len(generated)} records and one index in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
