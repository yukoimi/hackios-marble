# Marble 音频：DSP MFC 与 XNU 协议引擎

## 已确认的复用途径

本轮把 Linux AudioReach 的 MFC 输出媒体格式接口固定到提交
`36aad21d50d1af3f6954bb0c988257a908202477`，参考文件及 SHA256 保存在
`out/audio-format-reuse-v200-audit/sources.json`。模块 `07001015`，参数
`08001024`，输出字段为 sample_rate/u16 bit_width/u16 channels/u16 mapping。
参考源码来自 torvalds/linux 的 `sound/soc/qcom/qdsp6/audioreach.{c,h}`。
本地新包构造器按这些线格式生成请求，没有引入 Linux 运行环境。

`v202-live` 在手机上成功取得 MFC 库句柄并释放，typed load error=0、
unload error=0，两个独立 intent 退役，PAS 609521us、ROM/资源恢复、
Android 冷复位及熄屏通过，电池 34.7→35.0°C。
v201 仅因 EFI 超过原嵌入 FAT 容量构建失败，未启动手机；v202 使用现有 RAM EFI chain。

## v203：两轮静音格式图通过

608-byte OPEN 图为 WREP(6002) → MFC(6003) → TDM(6001)，
3 个模块、2 个连接，源 48kHz/S16/Q15/stereo，输出保持板级 96kHz/S32。
原 TDM 配置及 12.288MHz 请求逐字节保留。MFC 输出为 96kHz/32bit/FL1/FR2。

每次仅提交 1024B（256 个输入 frame），周期仍为约 5.33ms；MAP 仍是同一
4K 页。两个独立 token 都须 DONE56 和 RX_DONE_W_REUSE 完成且无 SMMU 故障，
才能复用。功放保持原 HMUTE/90dB 静音保护。

证据：`out/daily-adsp-mfc-graph-v203-live`、`v203b-live`：

| 项目 | 第一轮 | 第二轮 |
| --- | --- | --- |
| 两个输入 buffer、OPEN/PREPARE/START/STOP/CLOSE/UNMAP | 通过 | 通过 |
| PAS（原 1 秒门槛） | 608301us | 608446us |
| typed completion | 第57轮 | 第57轮 |
| 电池（Android） | 35.1→35.8°C | 36.5→37.0°C |
| ROM/资源恢复、Android 冷复位、熄屏 | 通过 | 通过 |

这证明此固件接受该格式图并消费零样本，不证明非零采样转换质量、输出波形、
长期连续流、HAL 播放或可听出声。报告保留 `conversion_verified=0`、
`audio_acceptance=false`，旧 96kHz/两模块报告不能接受新图。

384 项 Python 回归通过。64 个原 C 程序通过；另有 MFC graph、MFC codec、
MFC repeat、MFC AMDB codec/state-machine 的独立主机检查通过。

## XNU 协议引擎：已实现，尚无硬件 transport 绑定

`xnu/files/bsd/sys/marble_audio_stream.h` 接收现有 48kHz/S16 RAM 队列，
实现 MAP → OPEN/MFC → PREPARE → START → WREP → STOP → CLOSE → UNMAP。
它不执行 MMIO、SCM、功率请求或功放解除静音。还没有向现有音频设备注册可运行的
真实 transport，不能声称内核 DSP 后端完成。

`tools/marble-audio-wire-sync.py --verify` 保证 XNU overlay 中的 9 个纯协议头
与已验证的 xnuloader 版本完全一致；不复制 firmware/MMIO 适配器。
WREP 独立 token 最多 2048，仍低于 MAP/UNMAP token 范围。原固件 2/8 次限制不变。

`tools/tests/marble-audio-stream.c` 主机通过：完整 2048-period 协议循环、
交替 DONE/TX 顺序、正确双声道数据、欠载补零、pending DMA 停止和每阶段故障。
发送前即记账所有权，失败锁存；不允许重初始化掩盖 pending DMA；只有外部确认
PAS-off 和 route-restored，才能清零和 reset。协议 UNMAP 本身不授予物理页释放权。

`out/audio-native-stream-v204-plan` 保存了 freestanding arm64 Mach-O 编译对象、
完整命令及源码 SHA，编译成功；该对象尚未链接进 XNU，不能算内核实机通过。

接下来仍需：在 XNU 中绑定实际 GLINK/SMP2P、DSP/资源/SMMU 生命周期和缓存发布，
接现有 sink/HAL/guard，验证持续流和受限非零样本，最后验证实际声音。
不能把已关闭的 EFI 诊断会话当作仍供 XNU 使用的 DSP。
