# marble / Redmi Note 12 Turbo bring-up

本适配以用户提供的 `Mu-marble.img` 为 UEFI 基础，只使用 fastboot 临时启动和 RAM 文件系统。
不刷写分区，不打开 Android userdata，不解锁 bootloader，不更新触摸固件。

## 已取得的硬件证据

- 12 GB marble，原厂 fastboot 确认已经解锁；Android USB 序列号 `YOUR_ANDROID_SERIAL`。
- GOP 1080×2400、stride 4320、BGR，帧缓冲 `0xb8000000`。
- XNU 已验证 1、2、4 核，四核为 CPU7、6、5、4；CPU0 保留 UEFI / USB 服务。
- 静态 Darwin PID1、动态 dyld / libSystem 程序已运行。
- APFS RAM 系统诊断：36 项检查、0 失败。
- iOS 16.5 原版 launchd → backboardd → SpringBoard 已显示桌面；用户确认时钟更新和壁纸加载。
- Goodix 实机探测已通过版本和 IC info 校验：`BERLIN / 9916R`，MISO GPIO16，报告地址 `0x10308`，坐标 10800×24000，header / point 均 8 字节。
- 静态事件桥已在 Darwin 执行，原始 `/dev/rmd1` 邮箱自检通过：heartbeat `106→130`，ready=1；同次启动原有 36 项检查仍为 0 失败。
- 用户已确认实体触摸可以操作桌面、滑动和打开应用，关闭 `-v` 后控制台闪屏消失。
- 首版交互明显卡顿，点按容易变成长按；Interactive / nice -20、缩短轮询及合并 MOVE 后，用户仍反馈费劲。完整日志测到过 565 ms 的输入桥延迟，不能声称仅提高优先级已解决。
- 双核和四核均完成 RAM APFS 的 36 项诊断，0 失败；并行算术分别测到约 1.9 和 3.8 核同时工作，单核对照为 1.0。返回 loader 时各副核的 PSCI 状态均为 OFF。
- 四核原版 launchd / SpringBoard 已再次实机启动，桌面中 `hw.activecpu=4`、触摸 mailbox ready=1；用户确认流畅度明显改善，尚无 FPS 测量。
- 用户确认四核后流畅度明显改善，但轻点/滑动仍会被当成长按。加入 backboardd 输入 QoS 后仍会误判：线程 QoS 实机为 User Interactive（33），队列到读取不超过 38 µs，单次 HID 分发不超过 130 µs；同轮前端记录到 83、201 ms 的短触及 1.2–3.1 s 的接触，不能将这些全部归因于 backboardd 调度。

证据保存在 `out/marble-ios-cli-console.log`、`out/marble-ios-launchd-console.log`、
`out/marble-desktop.png`、`out/marble-touch-probe.log`。Apple 系统文件与本机测试日志不提交到仓库。
首版触摸桌面完整日志为 `out/marble-touch-first-desktop.log`；邮箱自检位于
`out/marble-touch-cli-host.log`。

## 固定输入

原始 Mu 镜像 SHA256：
`77733522406161dd0c621b204d34e2b42b377f71e6474dfe4a928e3eefc5c328`。

上游预编译内核 `prebuilt/mach.development.bcm2837` SHA256：
`9239d4adb7a2c433d7ed5729b47b63acc5281af73d8a81640ef3c678c8afd26c`。
单核仍使用此内核；`xnu/files/osfmk/arm64/gic_v3_irq.c` 的源码改动没有进入预编译文件。
多核由 `tools/marble_smp.py` 从锁定 SHA 的原文件生成独立 `out/mach.marble-smp`：
只将 VA `0xfffffff0074748b0`（文件偏移 `4655280`）的 `9140816c` 改为 `9141016c`，
使 GIC redistributor 扫描和边界步长由 128 KiB 改为硬件实际的 256 KiB。
输出 SHA256 为 `774b03c0c397a56e177c0185387e0ee2fe1afbd9d0d8b9a2c73bd86a3b3c15fb`。
原 prebuilt 不覆盖；host 按原始输入重新构造并逐字节校验，loader 也检查 CPU 配置与指令匹配。
后续完整重建内核时应改用 DT stride 的源码修复，不能把这个固定地址补丁套到其它版本。

硬件定义来自随 Mu 镜像携带的 marble DT，及 MiCode/Xiaomi_Kernel_OpenSource
`marble-s-oss` commit `48952ed36228217531482b39d5bef13e7fd808ec`。

## 构建与运行

需要按上游方式合并 userland 源码和预编译工具，已提取的 iOS 16.5 文件，以及 portable LLVM。
userland 的 `mkjobs.py` 需应用 `tools/marble-mkjobs.patch`，避免按需启动的 plist 被覆盖。

```powershell
python tools/marble-build-touch.py --llvm ../toolchains/llvm-mingw-20261020-ucrt-x86_64/bin
python tools/marble-build.py --llvm ../toolchains/llvm-mingw-20261020-ucrt-x86_64/bin --lzma-sdk ../toolchains/lzma-sdk --source C:/Users/Administrator/Downloads/Mu-marble.img
python -X utf8 tools/marble-prepare-ios.py --sevenzip ../toolchains/7zip/7z.exe --unzbm out/unzbm.exe --reuse-extracted
python -X utf8 tools/marble-userspace.py
python -X utf8 tools/marble-userspace.py --mode launchd --support-only
```

手机在原厂 FASTBOOT 时：

```powershell
python tools/marble-run.py boot
```

若 UEFI 停在启动菜单，按电源进入 HackiOS USB loader，USB 序列号变为 `xnuloader`。

```powershell
python tools/marble-run.py touch-probe
python tools/marble-run.py ramdisk out/marble-system.apfs
python tools/marble-run.py run --ramdisk out/marble-root-launchd.apfs --input-mailbox --seconds 300
```

已验证的四核桌面：在上面 `run` 命令加 `--cpus 4`。默认仍是 `--cpus 1`。
多核要求新 loader 的 `marble-smp=big-cores-1,2,4-gic256k-v1` 能力；旧 loader 会被 host 拒绝。
仅枚举 OFF 的 CPU4–7，数量不足则拒绝启动；退出时若副核未关停，loader 会锁住后续 kernel 运行，必须重新临时启动 EFI。

单/双/四核对照诊断：

```powershell
python -X utf8 tools/marble-userspace.py --support-only --cpu-test --input-selftest
python tools/marble-run.py run --ramdisk out/marble-root-cli.apfs --input-mailbox --cpus 2 --seconds 60
```

实测日志：`out/marble-smp-1-cli.log`、`out/marble-smp-2-cli.log`、`out/marble-smp-4-cli.log`。
四核完整桌面截图：`out/marble-smp4-desktop.png`。
这些并行运算结果不等于桌面帧率提升倍数。`snapshot` 现在会保留已结束会话暂存的完整日志。

默认不传 `-v`，避免屏幕控制台覆盖桌面；需要屏幕内核输出时添加 `--verbose`。
USB 内核日志仍保留。`--seconds` 到期后 XNU 返回 loader，屏幕上的最后一帧可能继续显示；
此时不是仍在运行的桌面，需查询 `status`。上限 3600 秒。

输入邮箱自检（不产生触摸事件）：

```powershell
python -X utf8 tools/marble-userspace.py --support-only --input-selftest
python tools/marble-run.py run --ramdisk out/marble-root-cli.apfs --input-mailbox --seconds 45
```

## 输入路径

`xnuloader/marble_touch.h` 在 UEFI CPU0 用 GPIO 模拟 SPI mode 0，轮询 IRQ21；
GPIO16 MISO、17 MOSI、18 CLK、19 CS、20 reset。
只通过 RPMh 开启 DT 指定的 ldoc2 1.8 V、ldoc9 3.224 V，不操作其它电源。
芯片版本和 IC info 校验通过后才能读取报告、向报告地址写零确认。
不实现固件升级或配置上传。

支持盘在 APFS 容器之外的 `0x800000` 偏移追加 16 KiB RAM 邮箱。
loader 仅在存在完整 opt-in 标记时挂接邮箱；不会改写 APFS 元数据。
128 个事件槽、单生产者、序列号和奇偶 heartbeat 防止读取不完整的报告。
缓存清理覆盖共享数据及发布标记。

静态 Darwin 进程 `marble-touch-bridge` 只读 `/dev/rmd1` 的邮箱，再写
`/dev/redwood-touch`，由上游注入 backboardd 的库产生 HID 事件。
队列溢出或生产者停止时释放已有接触点。内核的 redwood 触摸硬件驱动保持关闭。
launchd job 必须使用 `ProcessType=Interactive`，避免后台计时器合并导致按下/抬起延迟。
桥进程以 nice -20 运行，每 4 ms 唤醒一次，并保留 DOWN/UP、合并同一手指连续积压的 MOVE。
每 5 秒记录投递数和从 EFI 采样到桥进程的最大延迟。该测量不包括 backboardd 到屏幕呈现的时间。

`--input-qos` 支持盘选项加入仅对 backboardd 生效的 `marble-input-qos.dylib`：
通过 Darwin 公共 API 将输入线程和 HID dispatch queue 设为 User Interactive，
并有限记录 kernel queue / HID dispatch 的实际时延。静态桥有限记录 source/posted hold 时长。
诊断源码为 `tools/marble-input-qos.c`，由 `marble-build-touch.py` 一并构建，无固定系统地址补丁。
未加该选项时使用原来的注入库，可直接回退。

针对“松手仍保持按下”的新测试版：120 Hz 每轮读取 report 的就绪位，不再把 GPIO IRQ 低电平
作为唯一读取条件；USB 传输等待期间也继续采样，校验失败的 header 重读一次且不确认丢弃。
不按超时猜测 UP，以保留真实长按。有限记录原始 flags/count、IRQ 电平和最大轮询间隔。
`marble-touch-poll.c` 在 IRQ 始终为高的情况下验证真实解码函数仍能依次产生 DOWN/MOVE/UP，
并验证损坏的抬起报告能重试、不被错误确认；本地通过。
2026-10-23 用户实机确认“可以，触摸已经可用了”。本轮日志已有 4234 个有效报告、bad=0、
3043 个事件投递（20 个 MOVE 合并）；所示抬起报告到桥进程延迟 0–2 ms，5 秒统计最大采样延迟约 4 ms。
完整当前日志保存为 `out/marble-touch-fixed-live.log`。这证明当前路径可用，尚不代表原生驱动或所有多指场景均已验证。

本轮实际临时启动镜像 SHA256 为 `92092c2526aa677113f4f36b852c396356fa3d037d8be2c51b710dcd5279cfc1`。
随后为增加 host 回归测试而重新构建了相同运行逻辑，当前产物 SHA256 为
`2002548b6dc2220235c2a1344ca7b23df3538638fd9f8c577fef90d83d8934b4`，
保存为 `out/marble-touch-fixed-boot.img` 和相应 JSON；不把重建产物误称为逐字节相同的已启动镜像。
支持盘保存为 `out/marble-root-launchd-touch-fixed.apfs`，含输入 QoS 和诊断模块。

LLD 暂不支持 Darwin `-static`。构建脚本先完成无依赖链接（禁用 chained fixups），
再保留静态段、符号表，生成 ARM_THREAD_STATE64 的 LC_UNIXTHREAD，清除 PIE / dyld 标志。
测试检查入口位于可执行段、命令边界及无动态加载器依赖；实际执行仍须以手机日志为准。

## 2026-10-23 第 1–5 项验证

恢复包 `../marble-baselines/20261023-touch-smp4` 已保存 hash 清单和源码；其重建镜像
`2002548b6dc2220235c2a1344ca7b23df3538638fd9f8c577fef90d83d8934b4`
已重新临时启动，用户完成轻点、滑动、拖动、正常长按和屏幕边缘检查并回复“正常”。
真实解码测试补充 IRQ 恒高、坏 UP 重读、真实长按、十指、重复 ID 拒绝及 2000 轮手势。
回归记录中 136 次 UP 延迟 p50 2 ms/p95 4 ms/max 19 ms；该值仅覆盖已记录 EFI→桥事件，不是触摸到显示的总延迟。

源码内核已通过实机 CLI 和三轮桌面自动滑动：GIC 使用 DT 256 KiB stride，不再修改内核固定地址；
36 项检查全过，四核 60 秒算术/内存/计时压力零错误，4 线程获得约 241 秒 CPU 时间。
四个 XNU 大核在线；退出时副核均 PSCI OFF。只读 EPSS 输出请求频率，未开启 DVFS。
构建环境、固定依赖及运行命令见 [MARBLE-BUILD.md](MARBLE-BUILD.md)。

相同源码支持盘、同一组六次水平滑动（每个配置只跑一轮）：

| 显示拷贝线程 | 平均拷贝 | 活动帧间隔中位数 | 活动呈现速率 | 帧间隔 p95 |
| --- | --- | --- | --- | --- |
| 1 | 8.83 ms | 33.20 ms | 31.36/s | 64.50 ms |
| 2 | 4.85 ms | 33.08 ms | 30.76/s | 64.98 ms |
| 4 | 2.88 ms | 33.15 ms | 30.15/s | 67.91 ms |

源报告 `out/marble-source-fb{1,2,4}-perf.json`。活动速率排除 >=100 ms 暂停，不能等同所有应用的 FPS；
每轮只有六个手势，未声称 1–2 FPS 的差异有统计意义。四核在部分一秒采样窗口达到 100%。
减少拷贝时间未提高本组动作的整体速率，表明拷贝不是唯一瓶颈。软件渲染没有 GL draw markers，
不能进一步准确拆分绘制与调度等待。当前选择双线程拷贝，四线程保留为选项，默认 CLI 仍显式选择配置。

## 现有限制

四个大核已验证，四个小核未开放。仍是 CoreAnimation 软件渲染。
GPU、Wi-Fi、音频、基带、相机、电量和侧键未适配。Adreno 725 的差异见 `MARBLE-GPU.md`。
不持久保存 iOS 数据；重启丢失全部 RAM 会话。系统日期尚未校准。
触摸采用启动阶段的实验性 EFI 事件桥，不是完整的 XNU 原生驱动。


GPU适配追加结果（2026-10-23）：CX/时钟恢复及SMMU只读审计已验证，
CB0的编程、实际页表审计、禁用/TLBSYNC及页表根清理连续两轮实机通过。
标准ATS因实际IDR0.ATOSNS=1不可用，已显式拒绝；尚无硬件地址翻译或GPU执行证据。
早期测试的CBAR回收异常已修正，并在重启后复测通过。后续接入Qualcomm TBU/ECATS，详见`MARBLE-GPU.md`。
独立检查点`../marble-baselines/20261023-gpu-context`保留本轮源码、候选、支持盘、日志、来源和校验清单。
