# Marble 内核音频队列与多 period 接续

## v196：RAM sink，两轮实机通过

使用已有 `/dev/redwood-audio` 和 `redwood_audio_set_sink`，通过显式
`marble_audio_queue=1` 注册 4096 frame、48kHz/S16/stereo 环形队列。
该模式要求 adsp/sound 关闭；探测 ioctl 仅 root 可用，返回缓冲全部初始化。
它没有加载 DSP，不能视作 AudioServerPlugIn 或扬声器播放验收。

内核和测试 RAM 盘在 `out/audio-queue-v195-plan`；专用 loader 在
`out/audio-queue-v196-loader-plan`。源内核 SHA256：
`ed425466ab0f6b681c477700f4f195cbf6561ef811e897af73bec0002c61811e`。
内核编译、链接成功，但 make 仍有既存的四项 symbolset/export 检查错误，
并非整个 make 成功。此次只同步该音频设备及新增头文件，guest 其他基线不变。

专用 loader capability 为 `locked-RAM4096-static18-noDSP-v1`；
主机精确绑定测试盘 SHA，loader 验证源码 ABI marker、RAM 盘大小及完整参数策略。
18 秒、单核、黑屏、CPU 请求上限、EFI event wait、原短时温度策略均保留。
已归档 loader 源码并按原 PE timestamp 重建，得到完全相同的 EFI 字节。

证据：`out/audio-queue-v196-live`、`out/audio-queue-v196b-live`。
两轮均验证非法 pop=257 拒绝、满队列保留顺序并丢弃 2048 frame、
欠载 256 frame 全零、128 个 1024-frame 数据块回收。
最终 accepted=consumed=135168、dropped=2048、silence=256、queued=0。
CPU/EFI lease 精确恢复、Android 冷复位及熄屏均通过。
电池分别 34.1→34.2°C、35.0→36.0°C；运行 die 约 41.2–44.4°C。

此前 v195/b/c 均未进入内核：分别为 stock fastboot 枚举等待不足、
旧默认温度 profile 不匹配、旧 loader 参数策略拒绝。日志保留，不能计作内核通过。

## v197：8 次静音 WREP，整轮失败，禁止原样重启

`--audio-pcm-periods` 仅允许 2 或 8，默认仍为 2。8 要求显式 repeat、
原静音模拟功放保护及 RAM-only USB profile。每个请求使用独立 token；
typed DONE56、RX_DONE_W_REUSE、SMMU 检查均成功后才重新发布同一 4K 页。
仅 8 次 profile 的 GLINK TX 上限由 32 加到 44，为额外 6 次 DATA/ACK 预留 12 项；
运行时限、PAS 时限、温度、频率电压策略均未放宽。

本地 C 覆盖 256 种通知顺序、每个 period 的旧 token/错误/错误 handle/
非可复用 intent/SMMU 故障拒绝。报告要求显式精确请求数和完整生命周期。

实机 `out/daily-adsp-pcm-eight-v197-live` 的 8 个请求均退役，
但原 10ms 服务节奏未能在 600ms 内完成图关闭和 UNMAP，且 PAS 为
1,408,792us，超过原 1,000,000us，整轮失败。
SMC 合计 608,182us，thermal 276us，逐调用其他开销 76us；
尚有约 800ms 在逐调用计时区间以外，不能断言是 secure call 本身延迟。
已禁止原镜像再启动，看门狗恢复 Android、冷复位、熄屏通过，电池 34.5→35.0°C。

## v198：一轮通过，第二轮启动前干净拒绝

只对 8 次 profile，在初始 300ms 握手阶段后增加 2ms 服务边界，
仍保留 600ms 最大窗口；给 STOP/CLOSE/UNMAP 留出时间。
PAS v2 计时增加 entry/raise/post/restore 四段；总时间仍从原起点测至
原终点，失败判断仍为原 1 秒。计时一致性通过不能替代 PAS 成功。

`out/daily-adsp-pcm-eight-v198-live` 完整通过：8 次请求、图关闭、UNMAP、
PAS 608635us、ROM/资源恢复及 Android 冷复位熄屏；电池 34.8→35.1°C。
第 56 轮达成 typed completion，运行约 563ms。
`v198b-live` 未启动 DSP，`clean=1 attempted=0 dirty=0`，
后续检查确认 loader ADC 电池读数为 38°C，触发原有 `<38°C` 门槛；
冷复位熄屏通过，Android 电池记录 36.0→36.6°C，两个读数不应混作同一时刻。
不能称两轮完整通过。v198 原镜像已禁用，v199 只补前置条件诊断，不放宽条件。

## v199：成功一轮，复测被电池门槛拦截

`out/daily-adsp-pcm-eight-v199-live` 完整通过；八次共 32768B/4096frame，
同一 4K 页安全复用七次，56 轮内完成图关闭及 UNMAP，PAS 608616us。
ROM/资源恢复、Android 冷复位熄屏通过，电池 36.7→36.5°C。
此前约 800ms 额外时间未复现，不能称已修复根因。

`v199b-live` 前置状态、IRQ/SMEM 均通过；随后 `marble_icb_run_cycle()`
的原 ADC 电池门槛拒绝：`BATT_THERM physical=38`，未进入 DSP，
`attempted=0 dirty=0`。Android 冷复位熄屏通过，电池 37.0→37.9°C。
这是温度保护拒绝，不是 DSP 运行故障；继续硬件测试前须自然冷却，
不得放宽门槛或修改充电策略。v199 仅一轮完整通过。

最新回归 381 项 Python，通过；builder 原 64 个 C 程序通过，
另有 RAM 队列 C 测试和 8-period C 测试通过。

仍待：持续 PCM、48kHz/S16 到板级 96kHz/S32 的明确转换、真正 XNU GPR/APM
播放后端、受限非零音频、HAL 实机播放和可听验收。当前所有输出仍静音。
