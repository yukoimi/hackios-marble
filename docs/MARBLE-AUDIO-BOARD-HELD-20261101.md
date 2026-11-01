# Marble 静音功放持有验收（2026-11-01）

v237 同镜像两次独立通过实机验证。仅 fastboot boot 与 RAM 盘，不刷分区、不访问 Android /data；两轮均已冷复位回 Android 并确认屏幕 OFF。

- 两颗 AW2032 保持 HMUTE 和 90dB 衰减，贯穿8页 DATA；2048帧、约42.7ms，不是连续播放或出声验收。
- XNU 确认最后 DATA 的 DONE 与 TX 都退役、pending=0，在 STOP 之前发布独立 drained proof。
- EFI 验证同 epoch/handle/原 MAP/START，按35→34关模拟/数字、复位、恢复AUX，再恢复 I²C config/clock。
- 只有全部恢复后才发布 quiet ACK；XNU 收到 ACK 后继续 STOP/CLOSE/UNMAP。PAS→XNU unmap ACK→DMA free 次序保持。
- 原600ms DSP窗口、两层停止各1s、温控、watchdog、失败资源保留及USB轮询恢复检查全部保留。

| 运行 | 原始PAS | 完整停止 | 功放34/35持有 | 电池前→后 | 结束状态 |
|---|---:|---:|---:|---:|---|
| v237 | 608355µs | 610818µs | 149592/69351µs | 33.8→34.3°C | Android新boot ID、熄屏 |
| v237b | 608720µs | 611260µs | 149878/69673µs | 35.0→35.6°C | Android新boot ID、熄屏 |

电池数值为启动前/恢复后读数，不能当作iOS期间峰值；运行日志die读数分别43.2/44.4°C。
八页PCM均与原guard独立oracle一致；没有HAL sink、解静音、可听输出或持续供数。

离线验证：419项Python完整回归通过；之后host兼容性修正的13项source定向回归通过；加入两份真实实机fixture后，50项audio报告定向回归通过。
新loader的70个C测试通过，包括实际endpoint framing gate和实际双功放清理函数的逐次读写故障注入。
319项loader编译输入哈希无差异。内核25项音频源与VM实际源逐项一致（允许换行差异），另核对GIC，Mach-O与ABI检查通过。
完整XNU make仍有既存symbolset/export检查错误exit2；编译、链接、strip成功，不记为完整make无错。

v236因遗漏新boot arg白名单，在DSP/功放启动前被拒绝，已永久标记boot_allowed=false，恢复Android熄屏。
v237补严格白名单及组合拒绝测试；将v236实际上传的376字节参数串交给真实C策略验证，确认通过，重复/错误参数仍拒绝。

镜像：out/audio-board-held-v237-plan/build/loader-boot.img
SHA256：45a798a6dab0090e313fa6b0a2a1d6064fe6a91a837b8e20a5e92b4fa0284b78
内核SHA256：b692819b5e3258144dd1475ee2951837538302cec995f4ec160ecbf9c1832a8c
证据：out/audio-board-held-v237{,b}-live，汇总见同plan/two-pass-summary.json。
下一步为静音持续供数、欠载/停止恢复，再接HAL；实际出声须单独验收。
