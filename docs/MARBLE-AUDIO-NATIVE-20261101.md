# 2026-11-01 最新：v241 静音欠载/运行中补数两轮通过

用户要求继续直到音频完成，当前任务未完成；仍没有HAL、连续播放或实际出声验收。手机已恢复Android熄屏，无活动USB控制器。
最新完整基线out/audio-refill-native-link-v241-plan，同镜像v241-live/v241b-live两轮通过。
先装512帧→第三页故意欠载补零256帧→PID1只读ioctl确认空队列→运行中补入1280帧→五页真实DATA恢复→quiet ACK→STOP/UNMAP/PAS→XNU unmap ACK→DMA free。
8页与独立原guard oracle逐页一致，输入1792/输出2048帧；原RAM副本也通过，HMUTE/90dB全程保持。
owner新profile移除无用EFI GLink observer注册/注销；pre-AUTH只检查真实XNU READY/cap/epoch，不据此推断DSP link，实际连接仍必须由后续GPR证明。
QMP close新可选双读一致快照，仅在原750ms内等待暂忙/不一致；原transition codec和所有最终原样恢复条件保留。
两轮原始PAS608621/608397us，完整停止611088/610867us；QMP close2405/2408us，unstable均0。
电池前后33.8→34.0、34.8→35.5°C，不代表iOS期间峰值；Android新boot ID和熄屏均通过。
镜像SHA2a1b950c0adb19978d0482e11561bd936862170381080620488b08d1cd257501。
内核SHA16ca42a112fdb20f918aac005803cfa5e663c90b9550bf9b78e9b722a8e90b51。

v238b虽PCM/oracle恢复，但QMP close只1次ACK整轮FAIL；v239 USB丢状态，v240首轮PASS但复测USB断连，均永久禁用，不得重启或解禁。
v240复测最后preauth USB trace为QMP-held-on-return；v241移除旧observer后两轮通过，但尚未穷尽证明之前USB/QMP偶发失败根因；不能宣称所有间歇故障已解决。
启动前ADB offline已由主机重启adb server恢复；这一次预检未启动镜像，不算实机失败。
425项Python全量通过；之后57项audio报告/14项source定向通过，加入两份v241实机fixture后8项refill报告通过；builder72C及323编译输入哈希通过。
内核26项音频源+GIC与实际VM核对，编译/链接/strip成功；完整make仍既存symbolset/export错误exit2。


# 2026-11-01 最新：v237 两颗AW2032静音持有实机两轮通过

详见 MARBLE-AUDIO-BOARD-HELD-20261101.md。独立sideband在DATA八页全部退役后发布drained，
EFI关闭/复位两颗功放并恢复I2C租期后才ACK，XNU再发STOP；原PAS与ACK-before-free顺序保持。
同镜像两轮完整停止610818/611260us，PCM独立oracle与USB晚期周期恢复通过；Android冷复位熄屏。
v236新参数遗漏白名单在DSP前拒绝，永久禁用；v237已补真实完整cmdline策略回归。
419项完整Python回归、host兼容性修改后13项source回归、加入两份实机fixture后50项audio报告回归通过；builder70C及319编译输入哈希通过。
仍只有2048帧约42.7ms静音有限数据，没有HAL、连续播放或实际出声。下一步静音持续供数与欠载/停止恢复。

# Marble 原生音频传输接续（2026-11-01）

目标仍是实际硬件播放，尚未完成。不能把只读探测、主机模型、ARM64 对象编译或
之前的 EFI 静音图通过当作 iOS 已能出声。

## v206：XNU 原生 SMEM 发现，两轮实机通过

`redwood_audio.c` 的显式探测分支从 loader 的 DT ABI 获取 SMEM 窗口，
只接受 `0x80900000/0x200000`。`marble_audio_smem.h` 复用现有 redwood 的
SMEM 布局及固定 Linux 源码，检查 `$TOC`、APPS(0)/ADSP(2) 分区、uncached/cached
条目、padding、重复目标、边界与稳定 allocator 快照。不会分配 SMEM、读音频 payload、
推进 FIFO、写 IPCC、启动 DSP 或改变功率。

参考为 Linux `36aad21d50d1af3f6954bb0c988257a908202477` 的 `drivers/soc/qcom/smem.c`，
原文及 SHA 在 `out/audio-native-glink-v205-audit`。与原 redwood 共享布局，不照搬其
旧机型 rails(384)、RSC 地址、APR 路径或 ADSP 固件装载地址。

两个独立实机记录 `out/audio-native-transport-v206-live` / `v206b-live` 均为：

- partition=`1cf000`（物理 `80acf000`）、196608B；descriptor=`1cf030`、TX FIFO=`1cf060`。
- RX FIFO 尚未由 DSP 分配，四个索引全零；与原 EFI 的观测一致。
- 映射释放，SMEM/IPCC 写入=0，PAS 调用=0，DSP=0。
- 原队列压力测试、CPU/EFI lease 恢复、Android 冷复位和熄屏通过。
- 电池 33.1→34.0°C、34.0→34.7°C。

候选仍为 18 秒、单核、黑屏、4MiB 精确测试盘、CPU cap、EFI idle 和原温度策略。
`--audio-transport-probe` 还要求 queue+short18 与新内核 marker；loader 只提供
`SMEM-readonly-native18-v1` 能力和相应 DT 节点，重复参数及独立误用均拒绝。
kernel SHA `bf500b4bbfa0388730538f7ef489102e1e88f8b2b2130735308392d8d837cc1f`。
kernel 编译/链接成功，仍有原四项 export/symbolset 检查错误及其递归 make 错误，
未声称整个 make 通过。loader 源码归档后按原 PE timestamp 逐字节重建一致。

## v207：原生 FIFO 与 adsp_apps 协议连接层

以下代码已实现并通过主机模型，尚未链接进实际播放内核、尚无运行中的硬件 provider：

- `marble_audio_glink.h` 绑定 SMEM 回调，实现 FIFO 空间检查、写入/屏障/发布 head/
  doorbell 顺序、TX/RX 回绕、完整消息 peek 后再 commit、分片及失败保留所有权。
- `marble_audio_endpoint.h` 处理 VERSION/OPEN/INTENT，连接已有 MFC 播放状态机。
  单一串行 owner，512 条有界远端 intent 表，只给 `adsp_apps` 广告一个 512B RX intent。
  GPR 发送前预留正确的 8-byte 对齐帧空间，DMA 发布回调须成功；不自行写 power/SCM。
- 流程覆盖 MAP、608B MFC 图、PREPARE/START、2048 个独立 PCM token、STOP/CLOSE/UNMAP。
  DONE 与 TX 退役顺序可交换；RX 分片回收一次，错误 GPR 仍退役 RX intent，然后锁存
  DMA 所有权。UNMAP 完成不等于可以释放物理资源，仍须外部 PAS-off/route-restored。
- 模型逐帧处理真正编码的 GLINK/GPR 数据，检查 2048 period 双声道内容、一次 underrun
  补零、intents 复用、索引多次回绕及错误 token；模型不代表硬件传输或播放通过。

复用的纯 wire 头由 `tools/marble-audio-wire-sync.py --verify` 验证，现为 11 个。
`out/audio-native-endpoint-v207-plan` 有 13944B freestanding ARM64 Mach-O 对象及精确
编译命令/源码 SHA；不是已经链接的 XNU 驱动。
385 项 Python 回归通过；该目录的 5 个当前 C 程序（SMEM、GLINK、stream、endpoint、
启动 policy）全部通过，日志及命令已保存。

## 下一步必须接上的真实硬件生命周期

目前缺的是运行中的 DSP/SMEM/SMMU/功放资源与 XNU endpoint 的连接，不能只添加
另一个模拟设备或把完整关闭后的 EFI 会话冒充可供内核使用的后端。

优先调查可复用现有已验证固件 coordinator 的资源 owner，让 XNU 使用原生 GLINK，
而 owner 在同一有界会话内保持、验证和最终关闭 DSP 与资源，避免照搬 redwood 的
旧 rail/clock/地址。应先审计真实资源适配器与生命周期，再设计和实现共享 ABI。
这条 owner 连接尚未实现，不存在已验证的 handoff 镜像。

关键约束：一个 GLINK owner；START typed 成功后才请求板级 pins/Music；不伪造旧
固件状态字段；每次 DMA 发布前验证 page/route/fault；内核停止或失败后保留所有权，
直到可靠 PAS 关闭和 exact restore；保持温度、watchdog 和冷复位。
先做可自动结束的并发零样本会话，再有限幅非零、sink/HAL/guard 和实际声音。
2048-token 上限是当前原型测试边界，约 10.9 秒，不是最终日常持续播放能力。


## v208：原生 endpoint 接通真实 DSP，两个独立零样本闭环

新增 `xnuloader/marble_audio_native_fw.h`，只在显式隔离固件构建参数
`--audio-native-endpoint` 下使用。它把 XNU 同一个纯协议 endpoint 接到已验证的
EFI 板级资源提供端，真实操作 SMEM/IPCC、SID1801/4K、GPR/MFC/TDM。
FIFO 只有原生 endpoint 一个 owner；旧 GLINK graph、PCM、memory counters 不参与、
没有为满足 guard 伪造状态。本版仍由 EFI 执行 endpoint，不是跨核 XNU sink。

实际 START typed reply 和 TX 均退役后才锁存 `start_retired`，首个 DATA 发布回调
据此启用原 pins/Music/OTP/静音数字检测，再验证 DMA routes/fault/全零页并清缓存。
每个 request 独立 token、消费和 TX 双退役后才能重用。两段 1024B 48kHz/S16
零样本经 MFC 图；STOP/CLOSE/UNMAP 结束后仍持有物理页与 route，直到真实 PAS
关闭成功，才恢复 route、清零 stream 页、disable/sync/exact-restore/free tables。
协议完成之后仍处理通知，避免 SMP2P/PAS 停机等待被旧 idle deadline 判错。

`out/audio-native-provider-v208-plan/build-final`：

- RAM 启动镜像 SHA `a29d84d4db433c6550504943b10b99a48f7aacea919da15f807dd2685ff70d7b`。
- EFI SHA `034dfa25b02db311b4f1f906353e8bda370a3d760381b812f8da71c2fb4330e7`；
  独立重新编译/使用原 PE timestamp 链接后逐字节一致，记录 `loader-rebuild.json`。
- 原 64 个 builder C 程序、5 个相关手工 C 程序通过；385 Python 回归复跑全过。
  第一次全回归跨系统长暂停导致一项 CLI timeout（非代码断言），原失败日志保留。
  新增 4 项原生日志验收测试通过（真实摘录、缺项/重复、PAS-before-free、mute/token/owner）。
- 11 个 wire 头同步校验通过。

`out/audio-native-provider-v208-live` / `v208b-live` 两个实机会话：

- START、2 次消费、STOP/CLOSE/UNMAP 均退役；RX142/TX23，2 次零页发布。
- 两颗功放保持 HMUTE/90dB，原受限模拟/PLL检测通过，无扬声器开关写。
- context5/SID1801，ECATS无fault，PAS 后 exact route/页表恢复和释放通过。
- 第一轮 PAS 609506us/44 calls，运行 die35.3°C；电池26.1→27.1°C。
- 第二轮完整通过，电池28.2→28.3°C；PAS时序见第二轮原始日志。
- 两次 Android 新 boot ID / 冷复位 / 熄屏均确认。当前无活动 USB 控制器，手机 Android 熄屏。

第一轮验收脚本把 PAS-stop 日志与 DMA-free 回调的顺序写反，错误拒绝后走了看门狗
复位。硬件本身已按正确顺序完成；原 exercise/error 保留，`reaudit.json` 独立复核
并记录原因。修正后的审计增加反序必须失败测试，第二轮无需修正直接通过。
没有放宽时限或跳过真实证据。该报告仅把以上两轮判为 native-zero2-provider 通过，
仍固定 `audio_acceptance=False, XNU_sink=False, continuous_playback=False`。

下一步：真实 XNU/EFI resource-owner 交接。现有 owner 的 NPA/ICB 总 held 上限15s、
DSP运行600ms、PAS1s全部保留。不能把本版 EFI 托管 endpoint 当 XNU 已播放。
需要解决 combined loader/RAM package、跨核缓存可见性、单一FIFO owner、真实
START/UNMAP证明、内核 worker 与底层 held 调用栈同时存活；随后受限非零/HAL/guard。
当前原型2048token约10.9秒边界也仍须解决，不能宣称日常连续音频完成。


## v209：原始回包与会话 epoch 证明，两个独立实机闭环

`marble_audio_proof.h` 定义固定128B证明，保存真实MAP28B、START32B、UNMAP32B回包，
以及独立cookie验证后才设置的TX退役标志。stream在第一次命令前绑定外部epoch，拒绝重复绑定；
首个DATA pending时才能导出configure证明，全部消费及UNMAP双退役后才能导出retired证明。
资源端重新解析回包、比较handle、cookie、epoch、计数和阶段，才配置pins/Music或放行停机。
TX标志来自可信endpoint的实际cookie校验，并非密码学认证或独立旁路观察。

`out/audio-native-proof-v209-plan/build` 已构建，原64C builder检查通过；相关stream/endpoint
主机模型加入错误epoch、过早证明、错误token/handle、缺失TX、错误UNMAP/重复cookie等拒绝检查。
6项日志审计测试通过；freestanding ARM64 endpoint+proof对象16448B，尚未链接进XNU内核。
EFI按原PE timestamp独立重新编译和链接后字节一致，见loader-rebuild.json。
镜像SHA `caadda9d3612077a75ca32d41980ae45745aaaa6fcc4b08365c16f50e6899d43`。

两个独立实机证据为 `out/audio-native-proof-v209-live` / `v209b-live`：

- 两次configure flags15、retired flags63；各自真实epoch一致且两轮不同，未用旧固件进度计数。
- 第一轮epoch13eb44b5，DSP hold520172us，die37.7°C，PAS608870us/44calls，电池28.8→29.7°C。
- 第二轮epoch13ddc756，DSP hold510179us，die40.0°C，PAS608534us/44calls，电池30.4→30.5°C。
- 两轮消费、UNMAP、PAS、route/table恢复释放、两颗静音功放恢复、Android新boot ID及熄屏全过。
- PAS分项计时报告独立通过，`PAS-timing-audit.json`；硬件状态记录两轮通过。

这些仍是同一个XNU纯协议引擎由EFI托管执行。它证明可复核的资源交接记录在真实DSP上成立，
**没有跨核XNU worker、没有HAL sink或非零声音**。391项Python全回归通过，记录在v209-plan目录。另补充中断状态boot_allowed=False必须由通用boot入口拒绝，10项source/boot测试通过。

## 下一轮跨核接入候选（尚未实现，不是已验证启动方法）

审计得到的关键时序：run_on_other_cpu()会在24秒后才进入watching_only并屏蔽UEFI中断；
现有短18秒测试不会走这一分支。但这不证明DSP provider与XNU重编GIC并发可靠，必须有界实测。
现有I2C/ICB/RPMh/owned preflight拒绝cap.active；不能删除这些guard凑通。因此可考虑：

1. 精简combined loader保留XNU入口与资源coordinator，新增XNUP audio-dsp entry携带现成MADSP001
   30MB包（kernel约15MB+原4MiB盘+包仍小于64MiB payload上限），不可把PAYLOAD_BASE当同一个包。
   EFI memory chain目前builder只允许firmware-only，需要为新隔离combined profile单独审计/构建。
2. 进入既有held资源流程前先完成display-memory/SCM/package/temperature preflight，cap尚未active。
   仅在QMP postload成功、AUTH前的明确新hook启动已准备的XNU单核；同线程处理原cap mailbox，
   等XNU音频worker ready后再AUTH，保持原600ms DSP窗口，不以延长DSP运行补偿kernel启动。
   启动等待需要另有小的绝对时限，并计入原15s NPA/ICB held总窗；旧profile不能改变。
3. 两侧用独占cacheline的RAM mailbox，显式clean/invalidate/barrier；固定epoch、version、物理窗口。
   EFI单独拥有NPA/XO/ICB/ROM/PAS、SMMU、pins/amp；XNU单独拥有GLINK FIFO与endpoint。
   owner只查看FIFO indices和受限IPCC通知，不得同时调旧control_pump。
4. DSP分配RX FIFO后，EFI建立原SID1801/4K lease，发布真实DMA page/grant/epoch。XNU映射page、
   SMEM/IPCC并绑定epoch。首DATA publish阻塞等待owner校验128B configure证明并完成板级设置；
   不伪造legacy counters。先独立zero queue，原v195 userspace压力数据不得送进DSP。
5. endpoint退役后XNU提交retired证明，继续drain通知等待owner请求fresh SMP2P STOP/PAS。
   PAS-off后owner先恢复route，通知XNU停止访问并unmap DMA，等待明确ACK后才能free其页表/页。
   不能让XNU在owner free后调用stream_reset_after_pas()写已经释放的页。
6. 未收到ACK、温度/序号/回包/超时出错都保留所有权和watchdog，不放宽原PAS1s。
   保持内核返回后的cap/idle恢复及固件冷复位；先做一次黑屏zero并发闭环，再后续HAL/guard。

以上是基于已审计函数的候选，尚无combined/handoff镜像，不能视作可直接启动的后端。


# Marble 音频接续点（2026-11-01，v213）

v213 是最新两轮通过基线：真正 XNU 内核线程独占 GLINK；EFI 持有电源、ROM、SMMU 与 PAS。
两个零样本 period 完成 RX142/TX23，真实 MAP/START/UNMAP proof 与 epoch 跨核校验；
PAS 关闭后 XNU 撤销16KB映射并 ACK8，再由 EFI 恢复路由/页表、释放 DMA。
原队列135168帧断言、CPU cap/EFI idle恢复、新Android bootID及熄屏均通过。
证据 out/audio-owner-kernel-v213{,b}-live；两轮电池34.4→34.1、35.3→35.9°C。
EFI独立重编译复用原PE时间戳后字节一致，SHA0744829e3dbefbfe8afae66e2317bce6cda00f767e77a77b9c87cef8c252be72。
内核 SHA e2ded39f2bd4f5991fd48a3df239188da4a06477bc4e8c6922b61fdad1627406。

此基线 physical profile=disconnected，没有功放命令或引脚写入，没有 HAL sink，没有出声。
新可选 EDK2 SDK19 LZMA 链已完成主机真实解码/ABI/错误拒绝测试（2项）及 ARM64 EFI 构建，尚未实机。
完整旧板级PE181248B压成107008B wrapper，能装原FAT；不能直接启动旧v211候选。
下一步显式owner-board组合现有静音Music/OTP/TDM流程，保留private zero queue和原全部期限。
然后才是有限幅非零、持续流、HAL与实际播放；不得把静音闭环当完成。

v210能力拒绝未启动；v211真实闭环但16KB unmap panic，失败禁用；v212两次硬件闭环
但准备阻塞导致原3.5s温度采样间隔失败，整体失败禁用。旧owner-reaudit错误PASS已撤销，
见owner-reaudit-corrected.json。v213每1s服务原thermal_check，不放宽阈值。
6项新owner审计回归拒绝thermal STOP、epoch错误、缺ACK、提前释放及并发FIFO owner。

仅fastboot boot与RAM盘，不刷分区、不访问Android /data。USB串行；空闲熄屏。
电池<38°C，启动die<50°C，运行die<55°C；保留watchdog、PAS原1s及冷复位。



# Marble 音频接续点（2026-11-01，v220 / PCM v222候选）

v220 最新已通过基线：同一镜像独立两轮 XNU 原生GLINK + EFI电源/SMMU/PAS + 两颗功放静音Music/OTP/PLL/模拟检查。
八个零缓冲均经DSP DONE和TX退役再复用同一4KB页，RX154/TX35，8次PCM消费与UNMAP证明通过；
PAS先关闭，XNU16KB映射撤销ACK8后，EFI恢复路由、页表与DMA分配；原队列135168帧、CPU cap/idle恢复、
Android新bootID与熄屏均通过。证据out/audio-owner-eight-v220{,b}-live。电池34.1→34.9、34.9→35.6°C。
镜像out/audio-owner-eight-v220-plan/build，原PE时间戳独立重编译字节一致。
内核SHA 7f53dcba2d1223442a165d31dbfa5f97ad5ca32a546f8a8ee296846e40b3f498。
注意：8*256/48000仅约42.7ms数据，功放仍HMUTE/90dB且按原流程reset，没有HAL播放或可听声音。

v222非零PCM候选已离线构建，正在进行实机验证，未获PASS前不可记为完成：
内核SHA 3df5393e253077399f8c547ee1d2679dda2921eed943e2159f043023e74b14be。
静态用户程序经/dev/redwood-audio写样本，独立副本经过原redwood_guard（HPF开、gain0）再/16并限幅±64，
仅收2048帧到私有队列，原RAM队列保持独立；worker等待填满才READY。每页1024B数据+3072B零尾，
内核逐页检查并打印hash/peak，EFI独立检查首个DMA页并匹配后才ACK DATA，功放仍静音。
这是诊断设备写入路径，不是mediaserverd/HAL验收；未接长期实时流，未解除静音。
原guard完整主机模拟退出0；全65536个S16值边界与真实guard+队列八页测试通过（合成输入产生74个非零采样）。
PCM EFI独立字节重建一致。xnu编译/链接完成，但仍有原4项symbolset/export检查与递归make错误，完整make非0。
新内核源码闭包保存v222-plan/kernel-source；旧八缓冲内核差异源码保存v219-plan/kernel-source。

过程记录：v214漏mach.json在上传内核前拒绝，已恢复；v215异步USB查询超时，Android冷复位熄屏，原因未独立证实。
v216仅换LZMA、断开功放的基线通过；v217同步命令+认证前INFO的静音板级组合通过；
v218去掉INFO保留同步命令首轮通过，v219八缓冲首轮通过。两者复测均在DSP/内核/功放启动前被原ROM ADC38°C拒绝，
保持失败镜像禁用，不作两轮成功。v220重新构建并冷却后同一镜像两轮通过。
同步oem run避免资源调用中挂起后续getvar请求；这是验证通过的交互缓解，未宣称穷尽v215故障根因。
新的板级测试在Android≤36°C才重启，给ROM整数ADC38°C留余量；原所有门槛未放宽，拒绝时仍熄屏。

后续：首先严格审计v222真实非零传输；随后才能设计保持静音的持续供数/欠载恢复与功放持有/受限解静音。
原redwood_guard只可复用算法，不能把redwood的扬声器标定直接当marble标定。
始终fastboot boot/RAM盘，不刷分区、不访问Android /data；USB串行、空闲熄屏、watchdog/PAS1s/冷复位保留。



# 2026-11-01 更新：v222b 非零设备写入首轮实机通过

证据 out/audio-owner-PCM-v222b-live：原RAM压力程序经/dev/redwood-audio写入的2048帧副本经原guard+限幅后，
真实XNU/DSP完成8次消费及UNMAP。首DMA页EFI与XNU同时测得peak=1、nonzero=74、hash=bc1769b1；
其余7页是此斜坡输入经HPF后的零样本。PAS608587us，DSP hold420072us，温控/ACK-before-free/CPU与idle恢复通过。
电池35.7→36.0°C，Android冷复位熄屏。只记1轮通过，没有长期音频、实际HAL或出声。
v222初次主机电池36.2°C即拒绝，未重启，不属于实机失败；使用v222b目录完成首次真实启动。
404项Python测试全部通过（日志v222-plan/python-tests.log）；原guard模拟、PCM全S16边界及真实guard/队列八页C测试通过。
完整内核make仍保留原4个export/symbolset检查错误；未把链接产物当完整make无错。

下一步已编写独立1kHz整数三角波静态写入程序及原guard主机oracle（尚待构建/实机），
用于验证八页每页均有非零数据和逐页hash，而不是把本轮74个瞬态非零采样当持续音频已完成。
功放继续保持HMUTE/90dB；原DSP600ms、PAS1s、温度、冷复位与/data边界不变。



# 2026-11-01 更新：PAS 恢复路径偶发超时，尚未出声

当前已验证：v220 八缓冲静音板级闭环两轮；v222b 有限非零设备写入一轮。
v223 独立三角波程序与原 guard 主机 oracle 两次构建一致；每页 peak 30–33、nonzero 496–502。
八页仍只有 2048 帧（约 42.7ms），没有持续播放、iOS HAL 或可听输出验收。

- v224：八页数据全部匹配 oracle，但 PAS 总耗时 1408788us，RestoreTPL 800230us，违反原 1s 门槛；保留资源并冷复位。失败镜像禁止重启。
- v226：DSP 启动前因过严的 allocator 绑定检查拒绝；未把此拒绝当成音频测试通过。
- v227：移除无关 allocator 原实现要求，仍保留 RestoreTPL 完整代码指纹与 RaiseTPL 绑定。八页数据及恢复通过一次，PAS 608212us、RestoreTPL 约 69us，电池 34.3→35.1°C。单次通过没有证明偶发延迟修复。
- v228：新建自己的 10ms/TPL16 只读采样事件，重现 PAS 1408497us、Restore 区间 800236us。80 次采样均看到 TPL8 队首仍是 EnvDxe；不能据此断言 EnvDxe 正在耗时（事件可能重新入队，也可能在前置分发或 IRQ 路径）。事件 cancel/close 均成功且 active=0。八页数据仍匹配，整体失败；Android 新 boot ID、熄屏，电池 33.7→34.5°C。
- v229：增加 arm/RestoreTPL/release 分段时间及四次有界只读原始栈采样。构建与主机测试通过；实机 oem run USB 报 Too many links，暂未取得可用日志，不能当作运行通过。

只读代码前缀离线映射定位三个待分发回调：EnvDxe RVA1794、ButtonsDxe RVA1d40、UsbConfigDxe RVA4f70。没有证据允许修改或取消其事件。
下一步先确认 v229 的恢复状态并保存失败证据，定位实际延迟来源；原 PAS 完整 1s、DSP 600ms、温控、失败保留、ACK-before-free、冷复位和 /data 边界均保持。
最新完整 Python 回归仍是 v222 的 404 项；本轮报告器定向测试 13 项通过，不把它描述为全量回归。



# 2026-11-01 最新：v233 USB 轮询隔离与恢复后进度首轮通过

已定位退出偶发超时的真实调用链：RestoreTPL → UsbConfigDxe → UsbPwrCtrlDxe →
PmicDxe RVA205d0 → DxeCore Stall → MetronomeDxe。v231c 两份中断现场的 PC 都在
MetronomeDxe RVA1278，11 个帧记录的返回地址均经原 Mu PE 映射且前一条确为调用指令。
PMIC 在查询 USB APSD 类型时使用 200000us 等待，本次累计约 800ms；并非 EnvDxe 队首能直接说明谁在执行。

新增默认关闭的 --audio-usb-poll-lease，仅支持原 Mu 完整指纹匹配的 SetTimer 和 UsbConfigDxe 回调、
同一 driver 全局持有的事件、NotifyTpl8、context0、100ms 周期和无待分发通知。只在 PAS 的 TPL8 窗口内
用 SetTimer Cancel/Periodic 暂缓并重启此一个事件；不关闭事件、不摘除待处理工作、不改充电寄存器或 IRQ。
重新启用会重排下一次 100ms 轮询时间，不能称为精确恢复旧 deadline。两个 API 调用都计入原 PAS 完整 1 秒。
取消失败仍尝试恢复原周期；身份/恢复/超时失败继续判失败并保留资源、冷复位。

- v232 首轮：PAS608255us，暂停608210us；八页 PCM/oracle、DMA/CPU/idle 恢复通过；电池34.5→35.2°C，Android 新boot ID、熄屏。尚未检查后续周期进度。
- v233 新增只读晚期检查：内核返回后确认原事件仍绑定原回调、100ms 周期、TimerTriggerTime 已推进且 NotifyLink 为0，不调用回调、不再修改计时器。
  首轮 PAS608656us，晚期 scheduler_progress=1、恢复检查通过；Android 最终状态见 out/audio-usb-poll-later-v233-live/exercise.json。
- v229 USB 中断无日志，已确认恢复 Android 熄屏，电池34.0°C；镜像永久保留失败状态。
- v230、v231c 重现超时，数据正确但整体失败；v231 首次快速通过不能掩盖后续失败。v231b 在 Android 电池36.4°C 时干净拒绝，未重启。

v232 修改后的完整 Python 回归413项通过。v233 晚期检查后的报告器14项定向测试和新增 C 生命周期测试通过；不将此前全量回归描述为已覆盖之后的修改。
当前音频仍无 HAL/连续播放/可听输出验收。下一阶段是让两颗功放的静音配置真正跨 DATA 期间持有，再处理持续供数与受限解静音；不能把已复位功放后的42.7ms数据当播放成功。



# 2026-11-01 最新：v234 停止事务隔离两轮通过

重要更正：v233c 复测虽原始 PAS608230us、DMA已释放、USB晚期进度通过，但外层metadata-stop为1410916us，整体失败。
没有降低验收门槛；v233 镜像已永久禁用，v232 同类较窄隔离候选也已退役禁止启动，原首次通过记录另存。

v234 将 USB 轮询恢复点放到原 shutdown adapter 返回之后，覆盖原生安全停止以及 XNU unmap ACK/DMA释放。
仍在既有外层停止事务计时边界内恢复100ms周期，然后才读取原stop_us；原始PAS和外层metadata-stop各自保留1s门槛。
未把延迟移出统计边界，恢复失败会使stopped失败；全程无充电参数、IRQ、回调代码或事件链表直接写入。

首轮 out/audio-usb-stop-closure-v234-live：原始PAS608654us、隔离610970us、外层metadata-stop611066us。
八页PCM/oracle、ACK-before-free、ROM/电源资源、USB周期后续推进、CPU/idle、Android新bootID和熄屏全部通过。
电池35.2→35.7°C（启动前及恢复后读数，不是iOS期间峰值）。同镜像独立第二轮也通过：原始PAS608568us、外层停止611050us；电池36.0→36.1°C，原事件周期推进、新bootID和熄屏通过。汇总见out/audio-usb-stop-closure-v234-plan/two-pass-summary.json。

最新完整Python回归413项通过；之后新增成功/失败实机日志回归，报告器16项定向测试通过。
本次builder实际编译并运行68个主机C测试程序。新增marble_aw2032_held.h及其故障注入模型只在离线测试使用，未绑定硬件。
音频尚未有HAL、连续播放或可听输出验收，不能宣布日常功能完成。

