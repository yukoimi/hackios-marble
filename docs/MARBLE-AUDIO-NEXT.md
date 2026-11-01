# 当前：v259显示内存恢复修复同镜像两轮通过

v258失败原因已确定：DMA审计错误依赖连续VSync无丢帧。v259仅音频显式模式复用原总线租期直接执行全部66字段隔离检查，保留20ms双采样及原样恢复。首轮24页数据/oracle、欠载恢复、STOP/UNMAP/PAS、DMA释放和QMP恢复通过，同镜像第二轮也通过；HAL/长期播放/实际出声仍未完成。详见[运行记录](MARBLE-AUDIO-RUNTIME-20261101.md)。

以下保留历史状态，遇到冲突以顶部当前进展为准。

# 当前：v254首次实机进行中，未完成音频

已保存并验证v33（1595源/9206证据）；v253增加有界原始消息诊断的内核构建及模型通过，但USB中断无消息证据，镜像禁用。v254显式保持初始100ms旧时点，此后50ms，正在首次实机。最新可靠双轮基线仍v241，HAL/长期播放/出声未完成。详细入口[运行/停止记录](MARBLE-AUDIO-RUNTIME-20261101.md)。

# 最新：v252已取到内核失败现场，下一步诊断MAP回复

v251/v252实机均在DMA授权后的MAP阶段失败，未执行功放命令；READY/AUTH通过、固件ADC35°C，不能误称CPU未启动或温控拒绝。已冷复位Android熄屏，最后电池35.1°C。失败候选禁用。最新双轮基线仍v241，无HAL/长期播放/出声。完整状态先读[运行期限记录](MARBLE-AUDIO-RUNTIME-20261101.md)。

# 当前入口：2026-11-01运行/停止链路仍在适配

请先读 [MARBLE-AUDIO-RUNTIME-20261101.md](MARBLE-AUDIO-RUNTIME-20261101.md)。v241仍是最新双轮可靠静音基线；v246仅单轮通过且余量不足，v242至v245和v247至v249失败镜像均不可原样重启。v250失败禁用；v251已构建但因Android预检电池36.5°C未重启，等待自然降温。HAL/长期播放/出声未完成。以下是历史记录。

# 2026-11-01 当前：v245正在构建，v243/v244失败禁用

v243在QMP-held-on-return后USB中断，自动冷复位Android熄屏，电池31.9→32.1°C。
v244关闭pre-AUTH USB INFO，首轮完整通过：24页oracle/19次写/绕回、DSP530162us、53次RAM采样cancel+close、PAS608279us、外层610686us、QMP总2216018us、两次close ACK，电池33.3→33.3°C。
v244复测同样数据及DSP530166us/PAS608266us通过，但USB轮询恢复后OFF等待800575us超出原250ms，ACK仍1，整轮拒绝；冷复位熄屏，电池34.0→35.0°C，镜像已禁用。不能宣称USB INFO就是全部根因。
v245是新显式USB-QMP-v3租期：只将已鉴定100ms USB轮询的恢复点移到QMP关闭后，仍原SetTimer API、不修改IRQ/充电/外国回调、不移除pending工作；完整metadata-stop与租期仍<1s，原始PAS<1s，QMP close原750ms、总2.5s不放宽。另将QMP等待截止时间压到外层999ms内，为恢复轮询预留1ms；关闭失败仍尝试恢复轮询再保留资源冷复位。
固件运行期530ms诊断仍有真实600ms上限，包含final thermal、完成状态复核和采样释放/日志；没有用轮次冒充墙钟。
v244构建期间439项完整Python回归通过；新增v3审计的6项组合parser测试通过（不是硬件证据）。
音频目标尚未完成，仍无HAL、长期播放、可听输出。v241保持最新双轮可靠静音基线，所有失败候选不得原样重启。

# 2026-11-01 当前：v242复测超时禁用，v243诊断候选USB中断

v242首轮24页/19次写入/队列绕回/逐页oracle通过；复测数据和功放清理通过，但DSP实际1529933us、QMP总2545500us超出原600ms/2.5s，整轮失败，禁止原样重启。v241仍是最新两轮可靠静音基线。
v243补固件typed-complete和runtime-exit实际墙钟检查，自有10ms/TPL16 RAM采样事件只覆盖DSP运行，停止前cancel/close；异常路径也尝试释放。没有改变外国事件、IRQ、电源电压或温控门槛。
74项音频报告回归通过，构建成功，328编译输入哈希一致；镜像05770a71b5f1b1b11fce86dd17ca93a0e8b47784ec74d97db42c8725b56b1a37。
首次实机在pre-AUTH最后可见QMP-held-on-return后USB报告Too many links，未取得运行期RAM日志，不能据此判断采样/DSP是否执行、阻塞根因或数据是否通过。等待原控制器恢复，失败候选不原样重启。
仍无HAL、长期播放或实际出声验收，用户目标未完成。仅RAM临时启动，不刷分区、不访问/data；空闲恢复Android熄屏。
以下为历史记录，条目中的“尚未实机”等仅代表对应当时状态。

# 2026-11-01 当前：v242 STREAM24 v2已离线构建，尚未实机

用户要求继续直到音频完成，任务仍进行；不能在有限静音阶段宣称HAL/持续播放/出声完成。最新完整双轮基线仍v241，checkpoint-audio-refill-v32-20261101已验证1585源/3278证据成员。
24页/6144输出帧（128ms）、5888输入帧，超过4096队列容量；先512帧→单页补零→补1024帧→17次256帧按水位补入，原RAM副本随写随校验。
新独立info/gate version2、periods24，owner320B/sideband256B尺寸与2/4/6/8序列保持；旧v1严格2/8校验器未放宽。
marble_audio_stream_window.h在验证时对副本复用旧地址/保留字段/epoch/typed-GPR解析，真实消息始终保留24；没有把真实计数改成8发给对端。
内核、EFI、builder、runner/controller/report已绑定显式 --audio-owner-stream/--owner-stream 与 marble_audio_stream=1；不能与refill8混用。
要求native-only link、coherent QMP close、quiet-before-STOP；仍全程HMUTE/90dB，全部600ms/PAS双层1s/QMP750ms与总2.5s门槛不变。
内核验证最后5888 consumed、256 silence、head/tail5888（绕回一次）、19次写入、水位1024..2048后才允许最终闭环；每页用独立原guard oracle匹配。
当前 out/audio-stream-window-v242-plan：73C测试、325编译输入哈希、399字节真实payload C策略检查通过；完整Python回归正在运行。
内核27项音频源码+GIC与VM核对，编译/链接/strip成功；完整make仍既存symbolset/export错误exit2。
内核SHA1d3bf5af38032c6d6d9a93562e58d223422b3751520e7dfa7c9dcac2217e75dd。
镜像SHAd106671ae61db978db557968f9ece40f878ee37318e82d9e0e270aa0a2efe2f3。
新静态盘out/audio-stream-signal-v242-plan，disk SHAa6543aa75200f376321839e94152838b825f8a17ee7ab1d78a26f4b6cb1e7522，oracle SHAdad27d03cf94f63d75bcd0136cd55cfc399d92b3d8306f70cbc274061198aad2。
尚未启动v242。回归通过后控制器需带 --owner-board --owner-periods 24 --owner-pcm --board-held --owner-stream --owner-qmp-close-snapshot --owner-native-link --owner-usb-poll-lease 和显式oracle/package。
不重新启动v238/v239/v240，全部已禁用。v241双轮保持允许；手机Android熄屏，暂无活动USB控制器。

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

下一步：24页有限窗口（6144输出/5888输入帧，128ms，超过4096帧队列），保持原600ms诊断上限。
新增marble_audio_stream_window.h严格独立v2/24验证器，原v1/2或8验证器不放宽；真实framed endpoint主机模型通过。
这份24页header尚未绑定内核/EFI/主机，未构建或启动对应镜像。接下来实现生产者保持队列水位与跨4096绕回，显式新ABI/profile，再验证实机；不能通过延长诊断超时冒充持续播放。
全部原600ms、双层PAS各1s、QMP close750ms/总2.5s、温控、watchdog、失败资源保留、冷复位继续保留；仅fastboot boot与RAM，不刷分区、不访问/data。

# 2026-11-01 当前：v238运行中补数已观测，整轮失败；v240追踪候选

任务仍未完成，用户要求持续推进直到音频完成；不得把静音有限模型、编译或DATA恢复记为出声/HAL/持续播放。
最后完整通过基线仍是v237双功放静音持有两轮。v238新source kernel SHA16ca42a112fdb20f918aac005803cfa5e663c90b9550bf9b78e9b722a8e90b51。
新增只读64B MAREFILL_IOCTL与显式marble_audio_refill=1：512帧预装入，第三页补零256帧，PID1观测队列空后再写1280帧。
v238b实机8页与独立原guard oracle一致，第三页FNV76efddc5/peak0/nonzero0，后五页恢复，原RAM副本1792帧通过；功放仍HMUTE/90dB。
但PAS后QMP关闭只有一个DOWN ACK，未完成UP ACK，完整资源恢复失败，镜像永久禁用；原PAS608165us与USB隔离610478us本身通过。
新报告修正日志收集的跨生产者顺序误判：PID1 console与kernel printf ring分段收集，只分别验证各生产者顺序及真实队列计数。
reaudit-current.json仍严格FAIL，PCM/oracle真但完整恢复失败；没有追认通过。

v239 --audio-qmp-close-snapshot为默认关闭的新profile：双读一致且邮箱空才执行原有close codec，不一致仅在原750ms内等；稳定非法状态拒绝。
不改远端字段、ACK值、几何/上下文/时钟/DAIF最终原样恢复、QMP总2.5s或PAS双层1s条件；尚未证明能修复实机问题。
v239执行oem run时USB Status read failed (Too many links)，未取得日志；按失败禁用，Android新bootID与熄屏已确认，电池32.4→33.0°C。
v240正在构建：仅额外启用现有preauth trace以定位断连阶段；AUTH后仍只RAM日志，不进行USB追踪。
不得重启或解禁v238/v239。v238首次只是ADB offline的启动前拒绝、未启动镜像；主机重启ADB server后恢复，之后实际测试为v238b。

425Python全量回归通过；其后QMP snapshot C及56项audio report、14项source定向通过，builder72C、323项输入哈希通过。
内核26项音频源码与VM、GIC核对，编译链接strip完成；完整make仍有既存symbolset/export错误。
仍保留600ms诊断窗口、全部温控、watchdog、失败资源保留、冷复位；仅fastboot boot与RAM，不刷分区、不访问/data，空闲熄屏。

# 2026-11-01 最新：v237 静音功放持有两轮通过

见 MARBLE-AUDIO-BOARD-HELD-20261101.md 与 out/audio-board-held-v237-plan/验收记录.md。
两颗AW2032在8页DATA期间保持HMUTE/90dB，全部DONE/TX退役后，EFI按35→34关闭/复位，再恢复I2C配置与时钟；quiet ACK后XNU才发送STOP。
旧owner320B/2,4,6,8 ABI不变，16KB共享页+512独立sideband；新kernel、loader与主机需显式opt-in。
同镜像两轮原始PAS608355/608720us，完整停止610818/611260us，PCM/oracle、ACK-before-free、USB周期继续推进全部通过。
两轮Android冷复位新boot ID与熄屏确认；电池前后33.8→34.3、35.0→35.6°C，不作iOS期间峰值。
319项编译输入无差异，builder70C通过；419Python完整回归通过，之后host兼容性13项source回归通过。
XNU编译/链接/strip成功，完整make仍有既存symbolset/export错误。v236新参数缺白名单在DSP前被拒绝，已禁用，不可重启。
新静音持有仅2048帧约42.7ms，没有HAL、连续播放或实际出声验收。下一步静音持续供数/欠载，再接HAL。
手机已Android熄屏，无USB控制器运行。全部原温控/600ms/双层PAS1s/watchdog/冷复位保留；仅RAM临时启动，不刷分区、不访问/data。

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

# Marble 音频接续点（2026-10-31）

v209最新：真实DSP的128B MAP/START/UNMAP原始回包+epoch证明两轮通过，
证据 out/audio-native-proof-v209-{plan,live} / v209b-live；镜像plan/build，PAS608870/608534us，
两轮消费/释放/恢复/冷复位熄屏通过，电池最高30.5°C。EFI字节一致独立重建；391Python、6审计测试与ARM64对象通过；通用boot也拒绝boot_allowed=False中断状态（10source测试通过）。
仍是EFI托管同一XNU协议引擎；没有跨核worker/HAL/非零播放。下一步候选时序详见NATIVE文档末节：
在preflight cap未active时进入held stack，postload→AUTH之间启动XNU并等worker ready；600ms不放宽。
先解决combined loader+XNUP audio-dsp entry、mailbox、单一FIFO owner及PAS后kernel unmap ACK再free。
当前无活动USB控制器，Android熄屏。目标active，禁止将本阶段当音频完成。


v208 最新：同一个 XNU endpoint 已在 EFI owner 下连接真实 DSP，两次零样本闭环通过，
RX142/TX23、两次消费与UNMAP退役、PAS<1s、exact route/table restore/free、Android冷复位熄屏。
镜像 out/audio-native-provider-v208-plan/build-final；证据 v208-live/reaudit.json + v208b-live。
EFI独立字节一致重建；385Python+新增4native审计测试通过。仍未出声，尚无XNU跨核handoff/sink。
手机Android熄屏，无活动USB控制器。目标active，下一步接真正XNU/EFI资源owner；不重复只读模型。
完整约束与细节见 MARBLE-AUDIO-NATIVE-20261101.md 的v208节。


2026-11-01 最新见 MARBLE-AUDIO-NATIVE-20261101.md。v206 XNU 原生SMEM只读发现两轮实机通过，
地址与EFI一致（80acf000/descriptor+30/TX+60），无DSP/FIFO/IPCC写，原队列与恢复熄屏通过。
385 Python及v207目录5个当前C程序通过。v207原生GLINK/adsp_apps连接层实现、2048-period
完整编码帧模型和ARM64对象编译通过，尚未连接真实硬件provider或链接进播放内核。
下一步审计/实现运行时资源owner与原生endpoint连接，不照搬旧redwood rails/APR，不伪造旧
firmware状态，不把已关闭的EFI诊断会话当运行中的DSP。没有handoff镜像或硬件非零播放。
手机已Android熄屏，无USB控制器运行；目标active。现有wire同步为11个纯头。

最新见 MARBLE-AUDIO-MFC-20261031.md：v202 MFC库取得/释放一轮通过；
v203 WREP48/S16→MFC→TDM96/S32 静音图两轮通过，PAS608301/608446us，
恢复Android熄屏，电池35.1→35.8、36.5→37.0°C。384 Python/64原C通过。
XNU的marble_audio_stream.h协议引擎主机2048-period循环通过，尚未绑定真实transport，
没有连续硬件播放或出声。9个纯wire头由marble-audio-wire-sync.py精确同步。
当前手机Android熄屏，无USB控制器运行。目标保持active，继续实际内核音频接入。

最新进展见 MARBLE-AUDIO-QUEUE-20261031.md。v196 内核 RAM sink 两轮通过，
尚无内核 DSP 后端。v197 八次回收已退役但图未关闭、PAS 超时，禁用；
v198 加 2ms 轮询后首轮完整通过，第二轮启动前干净拒绝，禁用原镜像。
v199 补前置诊断后一轮完整通过（8次静音WREP+UNMAP/PAS608616us+恢复）；
复测由原loader ADC电池38°C门槛干净拒绝，未进DSP，Android恢复熄屏。
v198b同样是ADC38°C；不得降低门槛。v199不是两轮通过，偶发PAS额外800ms仍未定位。
381 Python/64 builder C，
另有 RAM queue 和 8-period C 测试通过。既存音频底层基线如下。

最新可测试候选 out/daily-adsp-pcm-repeat-timing-v194-plan，两个独立live为v194-live/v194b-live。
64C/377Python通过。两颗AW2032的ROM/OTP+PLL+静音模拟AMPPD验证通过；两次WREP复用4K页，
独立token、每次DONE56/RX_DONE_W_REUSE(14)/SMMU无故障后才重用。每轮总8192B/1024frame，96kHz/S32/stereo。
只支持两个串行请求，尚无长期连续PCM、非零音频、有限幅出声、麦克风或marble内核DSP后端。

v193c两个请求/UNMAP/STOP ACK通过，但PAS耗时1.408865s超过原1s，严格失败、资源保留/看门狗恢复。
不要再次启动v193c；不要扩大时限或把raw0当成功。v194仅加RAM逐SMC计时，未证明修复了偶发延迟。
复测中继续记录时序，若失败保留日志并结束本轮，由看门狗恢复。

本地原有 /dev/redwood-audio、redwood_audio_set_sink、48kHz S16 HAL插件可复用。
下一步需要可在XNU内使用的marble GPR/APM持续播放后端；不能把UEFI诊断全释放后当内核音频仍存在。
保留48kHz插件/guard，与96kHz/S32板级路径的采样率转换须明确验证，不靠改常量。
先静音多period、独立token/退役/错误/欠载，再受限非零，再接sink与用户空间验证。

始终临时fastboot boot与RAM盘，不刷分区、不访问Android /data。USB串行；空闲熄屏；不加频压/改充电。
电池<38°C，DSP启动die<50°C，运行<55°C；保持watchdog与冷复位闭环。
