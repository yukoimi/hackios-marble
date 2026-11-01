# GitHub HakiOS 音频复用调查（2026-10-31）

本轮只读取公开源码和本地文件，没有启动手机或修改驱动。仓库文档中的操作命令只作为资料，不作为用户授权。

## 范围与证据

列举上游342个fork元数据，并搜索hakios/hackios独立仓库；对14个候选请求递归源码树，其中hoshikv/xnu-kernel-peridot-marble返回409，不能确认有可读源码。成功读取其余13个候选树；精选98个源码/说明文件，全部核对Git blob和SHA256。没有声称审计342个fork的全部代码。

固定上游HEAD仍为a3384e031eb1e605cf37adaefc19991f04f6274b，与前次v176审计相同。来源清单：out/github-audio-reuse-20261031-audit/source-manifest.json。

## 结果

| 候选 | 实际源码观察 | marble用途 |
|---|---|---|
| [kvfsrl marble](https://raw.githubusercontent.com/kvfsrl/xnu-kernel-peridot-marble/c84b4634277a70beac73921029b1fb6afc2530af/xnu/files/bsd/dev/hakios.sound.c) | sound/speaker/adsp三份文件与本次ishtar候选各自Git blob完全相同；播放仍是48kHz APR/ASM/ADM/AFE，功放是AW88261，源码树未见marble板级目录 | 不能当作AW2032/GPR已完成实现 |
| [13 Ultra / ishtar](https://raw.githubusercontent.com/peter8889/hackios-ishtar/6f7b60344b996bb87016bc0db502bc905d7e7793/devices/ishtar/PORTING.md) | 有SoC地址与移植说明；音频三文件与上述marble副本相同 | 可参考平台分层；不直接采用音频硬件路径 |
| [12 Lite / taoyao](https://raw.githubusercontent.com/skglab-dev/ios-taoyao/28a9f80fe7d8d532fc9dba4208272500a67f5f94/xnu/files/bsd/dev/hakios.speaker.c) | 确有新增TFA9874识别、reset、container及启停路径；与原Awinic分支区分 | 可参考功放后端拆分方式；NXP寄存器不适用于AW2032 |
| [K60 / mondrian](https://raw.githubusercontent.com/XTOM0706/ios-sweet-k60/8a03335994b379a5fb37e4177141066343cd9b33/PORT-STATUS-K60.md) | 状态文件记录裸机shim与显示交接阶段，未提供已完成音频的证据 | 不作成熟音频方案 |
| [BALMUDA X01A](https://raw.githubusercontent.com/Stoobyy/ios16-a101bm-x01a/dcfe8d2a7b785024857da896fb6c8608f3bfd8a4/README.md) | README明确声音尚未工作 | 可参考触摸/电池bring-up，不解决音频 |
| OnePlus9 / Mi10Ultra / Mi11Ultra / Xiaomi15 | 选读的sound和speaker文件在四库中分别完全相同，仍为redwood实现；上层README不能替代该机音频测试证据 | 对本轮GPR/AW2032缺口没有直接实现 |
| [picasso kernel](https://raw.githubusercontent.com/minhtritt1996/hakios-xnu-kernel/caa3c6e9ffffda302f8749163f0cb167b8091977/drivers/audio/hakios.sound.c) | 选读sound/speaker/adsp文件与当前上游逐字节相同 | 直接复用上游即可 |

未找到可核对的“marble + GPR/APM + AW2032 + iOS连续播放”完整公开实现。此结论仅限已检查源码，不代表GitHub不存在其它实现，也不代表这些项目其它功能没有进展。

## 明确复用的层

1. [audio_plugin.c](https://raw.githubusercontent.com/oosexgang/hakios-xnu-kernel/a3384e031eb1e605cf37adaefc19991f04f6274b/userland/src/audio_plugin.c)：保留AudioServerPlugIn、输出设备、时钟时间戳和向设备节点写PCM的方式。
2. [hakios.audio.c](https://raw.githubusercontent.com/oosexgang/hakios-xnu-kernel/a3384e031eb1e605cf37adaefc19991f04f6274b/drivers/audio/hakios.audio.c) 与 [共享接口](https://raw.githubusercontent.com/oosexgang/hakios-xnu-kernel/a3384e031eb1e605cf37adaefc19991f04f6274b/xnu/files/bsd/sys/hakios.audio.h)：复用/dev/hakios-audio、sink注册和音量/录制接口，避免另写HAL。
3. [播放feeder](https://raw.githubusercontent.com/oosexgang/hakios-xnu-kernel/a3384e031eb1e605cf37adaefc19991f04f6274b/drivers/audio/hakios.sound.c)：参考环形队列、缺帧补零、DONE后复用period和停止时撤销sink。其period_write/APR指令不能照搬，需换成marble现有WREP/GPR并验证每个buffer的独立token与退役。
4. [guard](https://raw.githubusercontent.com/oosexgang/hakios-xnu-kernel/a3384e031eb1e605cf37adaefc19991f04f6274b/drivers/audio/hakios.guard.c)：复用48kHz S16路径上的高通与限幅；既有主机测试不等于本板扬声器保护或声学验收。

## 适配顺序

上层保持48kHz/S16/双声道接口，保护处理也留在48kHz；下层与目前96kHz/S32板级路径之间明确处理位宽与采样率，不能把RATE常量改成96000就假定滤波器和时间常数正确。优先核对本机AudioReach能否承担采样率转换，否则再选用可验证的转换实现。

下一阶段先做多buffer静音PCM，验证DONE/token、缓存同步、欠载补零和完整停机；再将现成sink/插件接到marble后端；最后有限幅/音量上限地验证可听输出。保留本机已通过的QUP2、AW2032 ROM/OTP、静音启动/恢复、GPR/APM和PAS清理。

原项目docs/driver-review/sound.md是源码审阅，明确没有实机验证；本轮不复制其中的问题复现指令。采用代码时核对修复是否实际落地，并保留出处/许可证。
