# HackiOS on marble — Redmi Note 12 Turbo / POCO F5

**实验性移植，开发快照：2026-11-01。** 本仓库整理 marble 的 XNU、EFI loader、
GPU/触摸/显示/音频适配及实际使用的用户态改动，便于后续开发。

Experimental HackiOS port for **marble**. iOS userspace boots and accelerated
CoreAnimation renders; audio remains a **short, fully muted diagnostic**.
This is a source handoff, not a daily-use ROM or a ready-to-flash release.

> 上游 redwood（POCO X5 Pro）的功能不代表 marble 已支持。当前没有可用的系统音频、
> 稳定 60 FPS 或完整日常功能。测试采用 `fastboot boot` 和 RAM 镜像，未访问 Android
> `/data`、未刷写或擦除分区。仓库不分发 iOS 系统、SDK、ROM 或闭源固件。

## 已完成到什么程度

| 项目 | marble 当前状态 | 仍有限制 |
|---|---|---|
| 启动 | 现成 Mu-marble UEFI → 自定义 loader → 源码 XNU → launchd/backboardd/SpringBoard/应用 | 需要自行取得匹配 Mu 与 iOS 输入；不是完整开源 iOS |
| 多核 | 4 个大核运行、压力/返回恢复检查通过，流畅度有明显改善 | 未完成全部 8 核与完整电源管理 |
| 触摸 | 轻点、滑动、拖动、长按已实机反馈正常；修复松手后仍被当成长按的问题 | 仍需扩大多指、边缘与长时间回归 |
| GPU | Adreno 路径参与 CoreAnimation/GLES；Dock 模糊及 2×显示已验证 | 不是完整 Metal；瞬态色块与特定合成场景仍需回归 |
| 中文与布局 | 中文桌面/设置/控制中心、2×布局、状态栏几何修复；静态壁纸兼容回退 | 原生 Poster/动态壁纸、实际中文输入仍需进一步验收 |
| 显示同步 | 硬件帧/行计数与空白区提交经过有界实机检查 | 未完成显示 IRQ、原子 pageflip；没有稳定 60 FPS 或光学无撕裂结论 |
| 锁屏/黑屏 | 有限测试完成黑屏→锁屏→桌面；测试退出恢复 Android 并确认熄屏 | 不等于面板断电或 SoC 深度休眠 |
| 亮度 | DCS 读写、读回、调暗及恢复探测通过 | iOS 亮度滑杆未接通 |
| 按键 | 只读 observer、mailbox/HID 路径已有测试 | 真实按键边沿与系统音量仍待独立验收 |
| 音频底层 | v259 同镜像两轮静音 PCM、补数/欠载恢复、停止与资源归还通过 | **没有持续播放、可用系统音频 sink、实际出声或麦克风验收** |
| 温度与恢复 | 有限会话保留温度/超时、ACK-before-free、失败保留、冷复位检查 | 不是完整热管理/充电/电池 SOC 驱动；端点读数不代表峰值 |

## 尚待适配

- **音频**：持续生产者/EOS/排空/暂停/停止、队列背压与 DMA 同步 → 接现有
  iOS 音频插件 → 有限幅双扬声器输出/音量/故障保护 → 麦克风。
- **显示/GPU**：消除通知中心、控制中心、负一屏、资源库、设置退出的瞬态色块；
  完善缓冲区所有权和同步，完成实际稳定 60 FPS 验收。
- **日常外围**：真实按键、亮度滑杆、休眠唤醒、电池 SOC/充电、电源服务、
  Wi-Fi、蓝牙、蜂窝、传感器、摄像头。存在参考代码/固件不等于已适配。
- **工程化**：去除旧 EFI 模板 bootstrap 及本地 `out/` 隐式依赖，补干净环境
  构建与发布流程；解决 XNU 完整 make 的 symbolset/export 检查问题。

当前开发入口是 [开发交接](docs/MARBLE-HANDOFF.md)，不要把历史文档里的候选、
单轮通过或失败镜像当成当前稳定版本。

## 最新音频基线：v259（仅静音诊断）

两轮均完成 24 页（48kHz 下 **128ms 数据**）、5888 输入/6144 输出帧、19 次补入、
队列绕回、一次欠载补静音恢复及逐页独立 oracle 检查。功放保持 HMUTE / 90dB。

| 测量 | v259 首轮 | 同镜像第二轮 |
|---|---:|---:|
| DSP 实际诊断运行 | 530082 us | 520123 us |
| 完整 metadata-stop | 716754 us | 717030 us |
| QMP wire 总计 | 1385945 us | 1375849 us |
| Android 电池端点 | 33.7→34.4°C | 34.4→35.8°C |

READY/AUTH、DATA/TX 退役、quiet ACK、STOP/UNMAP、PAS、XNU unmap ACK、
DMA free、QMP OFF/close ACK 和显示内存恢复检查通过。此前 v258 的显示 DMA
审计错误依赖连续 VSync 测量，已在 v259 分离；全部 66 字段、20ms 双采样及
隔离/时钟归还检查保留。

- 参考命令、profile、哈希与**已测内核源码快照**：[reference/v259](reference/v259/)。
- 当前主线含后加的原始回复诊断代码，**不等于上述已测内核的逐字节源码状态**。
- 实机日志是脱敏文本 fixture；合成解析器测试不是新增硬件验收。
- 原运行期 <600ms、PAS/外层停止分别 <1s、QMP close <750ms/总 <2.5s 的诊断
  限制继续保留。持续播放需要独立生命周期，不能仅延长这些超时。

## 从哪里开始

### 1. 克隆源码并做离线检查

```sh
git clone https://github.com/yukoimi/hackios-marble.git
cd hackios-marble
python3 tools/marble-host-tests.py
```

Python 3.11+，C 模型需要 `clang` 或 `gcc`。也可以分别运行：

```sh
python3 tools/marble-host-tests.py --python-only
python3 tools/marble-host-tests.py --c-only --cc clang
```

这些检查不连接手机。本地固件/内核/镜像缺失时，相应集成测试会明确跳过，不能把
skip 计为通过。用例清单在 `tools/tests/local-input-tests.json` 与
`host-model-local-inputs.json`。GitHub CI 运行同一套公开检查。

### 2. 准备外部输入与构建环境

先看 **[准备材料与下载入口](docs/MARBLE-INPUTS.md)**：按必需/编译/可选列出下载链接、版本、哈希及来源缺口。
再看 [构建说明](docs/MARBLE-BUILD.md) 和 [输入身份](reference/v259/inputs.json)：

- 原上游 XNU/依赖、固定 Linux iOS toolchain 与 Theos SDK；
- 自行取得 iOS 16.5 / 20F66 系统并通过 `ios165/` 的脚本本地提取；
- 自行取得匹配的 Mu-marble.img；其匹配源码当前缺失；
- 自行从适用 marble 固件/ROM 提取 GPU、ADSP、功放与设备树参考材料。

本库**已集成固定版本的 userland 源码**，无需另找一份覆盖工作目录。
`common/setup.sh` 检测集成标记后会保留这份源码。Linux 编译步骤沿用：

```sh
python3 tools/marble-source-normalize.py
bash tools/marble-linux-build.sh setup
bash tools/marble-linux-build.sh kernel
bash tools/marble-linux-build.sh userland
```

这些是开发构建步骤，不是已验收的一键安装器。完整 XNU make 仍有既存检查失败；
原 Windows EFI 打包还依赖 `../toolchains/` 布局与历史模板。旧文档的 `out/...`
路径是需要自行生成的材料，未随 Git 提供。不要直接把 redwood 的
`common/run.sh` 或旧 prebuilt 镜像当成 marble 启动包。

### 3. 配置本机设备后，才进行新的临时启动测试

Windows PowerShell 示例：

```powershell
$env:MARBLE_ANDROID_SERIAL = '填写你自己的 adb 序列号'
$env:MARBLE_ADB = 'C:/adb/adb.exe'
$env:MARBLE_FASTBOOT = 'C:/adb/fastboot.exe'
```

新构建须建立自己的内核/profile/hash 清单，并先完成静音、温度、停止与恢复检查。
本库没有提交原手机序列号、SSH 密钥或可直接刷入的 marble 发布镜像。

## 源码地图

| 路径 | 内容 |
|---|---|
| `xnuloader/loader.c`、`xnuloader/marble_*.h` | RAM loader、触摸桥、时钟/温度/显示租期、DSP 与外设生命周期 |
| `xnu/files/bsd/dev/marble_*`、`xnu/files/bsd/sys/marble_*` | marble GPU/SMMU、音频协议/队列/内核桥 |
| `xnu/files/iokit/Kernel/Marble*.inc` | 合成、缩放、GPU ring 和显示节奏 |
| `userland/` | 已集成用户态源码；含 EAGL、音频插件、启动任务与注入库 |
| `tools/marble-*.py`、`tools/tests/` | 构建、诊断、报告器、恢复工具与主机测试 |
| `reference/v259/` | 双轮静音基线的必要参考与旧内核源码，不含二进制输入 |
| `docs/MARBLE-HANDOFF.md` | 当前进度、已知坑、下一位开发者的起点 |
| `docs/MARBLE-*.md` | 各阶段详细记录，含保留的失败与局限 |

## 贡献与验收

提交改动时说明设备/输入哈希、构建参数、主机测试、实际实机结果、温度来源和
停止/资源恢复情况。请区分模型检查、日志解析、单轮实机、多轮实机和可听输出。
失败镜像保持禁用；不要用历史成功状态覆盖当前失败记录。

## 来源与许可证

基于 OOSEX GANG 的 HackiOS 工作，保留原作者、历史与组件许可证。
用户态源于固定提交的 `hackios-xnu-userland`；原项目说明保存在
[README.upstream.md](README.upstream.md)。上游功能表主要针对 redwood，不能套用到 marble。

见 [NOTICE.md](NOTICE.md)、根 `LICENSE`、`userland/LICENSE` 与各第三方文件旁的
许可证。Apple/iOS、Qualcomm 固件及 SDK 不随本次源码发布重新分发。
