# marble 显示同步与日常功能 — 2026-10-27

## 当前结论

GPU 已用于 CoreAnimation，Dock 模糊经过帧缓冲截图检查。硬件显示计数驱动的
空白区提交已连续两轮有界实机验证；这不等于显示 IRQ、原子 pageflip 或 60fps。
日常功能仍在逐项实现，尚未全部完成。

只执行 fastboot boot，系统与支持盘均在 RAM；不刷分区、不操作 Android /data。
每轮结束核对 GPU active=0、GX/context、CPU 请求、EFI 等待寄存器以及显示总线
时钟恢复，然后返回 Android 并确认熄屏。保留失败实验及其原始字段。

## 已验证的同步路径

通过原 Mu 固件 EFIClock 协议租用 gcc_disp_ahb_clk(0x100da)、
disp_cc_mdss_non_gdsc_ahb_clk(0x10053)、disp_cc_mdss_ahb_clk(0x10052)。
原 enabled/on 分别为 0/1、0/1、0/0，共享投票可以出现 enabled=0/on=1。
等 XNU 所有次级核 OFF 后归还。原来直接读显示寄存器导致失联的镜像仍标 FAILED，
不能重用；现在必须先取得总线访问资格。

DSI INTF 0xae36000 的 FRAME/LINE 在运行，实测约60Hz，horizontal total=476、
vertical period=2300032、totalLines=4832、activeLast=2415。COUNT_EN=0 不能单独
否认运行状态，资格依据真实计数边沿、周期及 STATUS。ABI 为
MDISPLAY-MARBLE-COUNTER-v2。

显示线程先在约10.37MB staging RAM 完成缩放/合成，再按 FRAME/LINE 等待有足够
复制预算的 blank，memcpy 到既有 scanout，比较复制前后计数。每硬件帧最多提交
一次，失去计数或超时必须记录 unsynchronized fallback，不可算同步成功。
当前没有接管显示中断，没有改 scanout 地址。帧缓冲截图不能证明光学无撕裂。

## 两轮成功结果

| 记录 | 操作 | presents | failures / overruns / fallback | 最长copy | 电池前后 |
|---|---|---:|---|---:|---|
| out/vsync-gpu-v10-core-live | 设置滚动、桌面、长按、滑页，7动作 | 789 | 0 / 0 / 0 | 1939us | 35.5→36.0°C |
| out/vsync-gpu-v11-panels-live | 资源库、通知/控制中心等，8动作 | 370 | 0 / 0 / 0 | 2148us | 36.8→37.0°C |

两轮六项恢复均通过并返回 Android 熄屏。v10 核心截图检查通过，Dock 有模糊。
v11 部分动作延迟，命名 control-center 的截图实际仍在桌面，下一张才进入控制中心；
负一屏未有效验收。已将动作等待由1秒改为3秒，仍须重测。录像是 RAM 环形缓冲，
仅保留末约29秒，早期设置退出动画已被覆盖；visual_pass 保留 null，不能凭
像素候选排名宣称渲染缺陷消失。

上述为历史 v10/v11 结果。当前已验证内核 out/linux-build/mach-marble-vsync-v13，SHA256：
1a2a84846bfa424e298e89444e456e415da5dd30e8afa15d4837049604b443b4。
软件节拍分支增加 !fScanout 防重复等待，但实测本来 fb_beat=0，不能把性能变化归因
于此修改。原2个显示worker活动区间约10–19fps、driver约11–12ms，4 worker尚待比较。

## 温度与失败记录

采用 gpu90-v2：开始die<50°C，硬上限55°C、连续≥50°C观察60秒、90秒单轮预算；
采样失效/过期仍退出。未提高频率电压。Android真实电池预检<38°C，为用户40°C
目标留余量；iOS暂无连续电池温度，前后读数不是中途峰值证明。
v9旧10秒规则退出的FAIL保留，不能改判为电池过热或删除。

out/daily-buttons-v3-gpu-live 在 GPU 启动前 panic：PE image RVA 对齐不能保证
EFI relocation 后的 CPU cap mailbox 绝对地址对齐。GPU、CPU cap 未取得资源，
原恢复字段 false 保留；另存 early-failure-audit.json 解释未获取状态。EFI/显示总线
恢复与所有次级核 OFF 已确认，返回 Android 熄屏。v3 boot manifest 标 FAILED。
v4 改为启动时 EFI EfiLoaderData 分配8页，内部取16KiB绝对对齐映射，保留完整
allocation，检查范围后才启动，且在 choose_window 前预留以排除 XNU 可用RAM。

## 日常功能状态

| 功能 | 状态与下一验收 |
|---|---|
| 按键 | SPMI v7只读 observer 实机 probe通过；v5 loader / v2 mailbox 的锁屏、唤醒、音量 HID 事件已测试；真实按键边沿及系统音量变化仍待独立验证 |
| 锁屏黑屏/唤醒 | daily-power-v5-visible-live 已实际验收黑屏→首张可见锁屏→单次上滑到桌面→打开设置→最终黑屏；没有面板断电或SoC休眠 |
| 亮度 | DCS读写/读回/恢复已验证，XNU活跃期间的下降与自动恢复也通过；iOS滑杆尚未接入 |
| 电池 | ChargerEx存在但只读presence/plugged/voltage均EFI_UNSUPPORTED；不发布虚假电量，不调用charger初始化或充电动作 |
| 音频、网络、传感器 | 未完成；旧redwood驱动不能直接使用其硬编码地址 |

按键来源为用户 Mu 镜像及固定 MiCode marble-s-oss
48952ed36228217531482b39d5bef13e7fd808ec。core=0xc400000、cnfg=0xc42d000、
observer=0xc440000、EE0、v7 channel stride=0x20。只发EXT_READL observer命令，
不写PMIC配置/IRQ/pin/reset。PON channel407、vol-up GPIO6 channel434均owner0，
类型及输入配置已实机确认。物理键防启动已按住、两次样本去抖、失败释放，synthetic
2秒自动释放。原触摸mailbox-v1默认不变，显式daily-buttons才启用v2。

来源清单：out/vsync-v1-plan/mu-display-reference.json、
out/daily-v1-plan/references.json。旧阶段全文保存在 docs/history/。

## 2026-10-27 后续实测与限制

`out/daily-power-v1-gpu-live`：694 presents，failures / overruns / fallback 均0，
最长copy2040us，六项恢复全过，电池36.6→37.0°C。像素黑屏期间保留staging、
不复制scanout，唤醒不显示旧锁前画面，等待新swap。v5修复了截图覆盖日志staging
后返回日志下载不完整的问题，旧原始失败文件全部保留。

`out/daily-native-panels-v1-gpu-live`：611 presents，0/0/0，最长1768us，
六项恢复全过，电池36.5→37.0°C。虽然所有native marks都完成，截图实际上仍在
桌面启动过渡，没有进入通知/控制中心，故这些面板的视觉验收无效。
192MiB/step8录帧保留了153.231–178.122s的302个源surface帧，不能当作光学录像。

`out/daily-native-power-v1-gpu-live`的单次上滑发生在唤醒约8秒后，而锁屏约6秒
再次熄屏，上滑被用于唤醒，不能据此断言正常时序的首次上滑失效。调整时序后的v3
在操作前触及既有55°C die保护而退出，thermal FAIL保留，六项恢复全过。

`out/vsync-v12-panel-focus-live`：新增EFI手机时钟手势后，实际打开通知中心和
控制中心，静态背景模糊正常。350ms滑动的自动抬起记录为369–373ms；不再让16次
USB命令的延迟拉长手势。此轮新内核的资格变量被同名局部变量遮蔽，未启用硬件
同步，不能算垂直同步通过；候选manifest标FAILED，完整原始记录及审计说明保留。
六项恢复全过、电池35.5→36.1°C、Android已熄屏。修复候选v13另行构建、测试。

SMEM全区raw读取曾导致设备自行回Android，路径已禁用，`oem smem-probe`固定拒绝。
通过原Mu固件已初始化的SmemDxe get/get_ex只查询远端条目地址/长度成功：ADSP/SLPI
都有GLINK descriptor与AP TX FIFO，但都没有DSP RX FIFO。这仅是资源发现，不能
当作DSP已运行、电量或音频可用。adsp2/slpi2与手机Android实际运行固件名称、分段长度
匹配；旧redwood的ADSP carveout/IPCC地址不能直接套用，亦未将DSP固件装入设备。

当前恢复包 `out/checkpoint-daily-power-v1-20261027` 已逐文件hash校验，包含264文件。
后续候选需另外保存，不能覆盖这个已验证的恢复点。

## v13 的有效面板与设置验收

v13 将固定 MMIO 映射和运行资格分开，失效时仅原子撤销 qualified，避免将并发
使用的映射指针置空。DT 检查使用 accessQualified，修复 v12 局部变量遮蔽问题。
v12 未启用硬件同步的原始失败结果仍保留，不计入下表。

| 记录 | 实际操作 | presents | failures/overruns/fallback | 最长copy | 电池前后 | die峰值 |
|---|---|---:|---|---:|---|---:|
| vsync-v13-panel-focus-live | 通知中心、控制中心 | 311 | 0/0/0 | 2150us | 35.9→36.3°C | 53.4°C |
| vsync-v13-panel-focus-fb4-live | 同上，4 worker | 288 | 0/0/0 | 1761us | 37.2→37.2°C | 54.6°C |
| vsync-v13-settings-focus-fb2-live | 设置打开、上下滚动、退出 | 740 | 0/0/0 | 1748us | 36.5→37.0°C | 54.2°C |
| daily-power-v2-dsi-live | 像素黑屏、按键唤醒 | 137 | 0/0/0 | 2154us | 37.6→37.9°C | 54.6°C |
| daily-power-v3-visible-live | 黑屏、等待可见唤醒画面（超时） | 144 | 0/0/0 | 1420us | 35.7→36.0°C | 53.4°C |

以上六项恢复均通过，并回 Android 确认熄屏。v3 的 UI 序列没有完成，不能当锁屏
解锁功能通过；其显示计数同步与热保护检查独立通过。v2 单次上滑实际仍停在锁屏。
v3 唤醒首帧提交约晚于 power-on 5.9 秒，超出原5.5秒门槛，脚本拒绝发送上滑。
下一候选等待第一张非黑 scanout，最多10秒，并保存逐次可见性记录。

4 worker 面板测试的三个完整帧表均人工检查过，模糊正常，未见明显红绿黑矩形。
设置测试的560个保留源帧已通过全部contact-sheet复查，见manual-visual-audit.json。
这些是保留的源 surface 和 framebuffer，仍不能证明所有瞬态缺陷已消失或光学无撕裂。
设置测试在输入后2.5秒活动窗口约43.52次呈现/秒，中位间隔16.663ms、p95 66.174ms；
不是全程稳定60fps，也不能与不同动作的单轮数据直接作显著性比较。

vsync-v13-native-pages-fb4-live 虽有461次同步提交且0/0/0、最长1783us，仍因die
达到55°C退出，thermal FAIL保持。374帧的overview和frames-128可见负一屏、
第二页和资源库往返；native序列没有结束标记，因此只算部分验收。

## DSI 与亮度探测

原Mu只有 SimpleFb，无可用 DisplayPower/DisplayUtils 接口。固定驱动参考及hash
保存在 out/daily-display-fw-v1-plan/dsi-references.json。控制器为0xae94000、
version=0x20060000。有限FIFO传输只在clean idle、计数blank、空闲引擎和无旧错误时
执行；保留中断enable及无关W1C位，结束恢复DMA/TPG并归还clock lease。不修改
PHY、PLL、复位、刷新率或视频引擎；状态不确定则保留lease标dirty，要求重启。

daily-dsi-read-v1-live 成功读DCS power mode=0x9c。daily-dsi-read-v2-live 进一步
读到ID=c2,b6,32，brightness两字节02,66（614）；五条命令无ACK/timeout错误，
配置恢复、总线恢复通过。没有SET_MAX_RETURN，没有面板写。电池36.0→37.7°C，
回Android熄屏。DT名称与DCS ID为不同标识，不能按名称字符串猜测返回值。

daily-dsi-dim-v1 为后续有界下降/读回/恢复候选：只允许0x51，保留亮度MSB字节序，
不跨327的MI BL_SYNC边界，不提高到超出原值，不改vendor page/HBM/DC模式。
独立硬件验证完成前，不能声称iOS亮度滑杆可用。

实机 daily-dsi-dim-v1-live 已通过：614→460，250ms后恢复614，两次读回均匹配；
DSI配置及clock lease恢复通过，电池37.8→37.4°C，已回Android熄屏。
该结果仍只验证底层命令，iOS亮度控制尚未接入。589文件恢复点
`out/checkpoint-vsync-v13-dsi-dim-20261027` 已逐文件hash验证，旧恢复点未覆盖。

## 单次解锁、亮度运行时与截图一致性

`out/daily-power-v5-visible-live`：343 presents，failures/overruns/fallback均0，
最长1799us，六项恢复及thermal通过，电池37.4→37.2°C、die峰54.2°C。
唤醒后轮询14次，6.188秒取得首张非黑画面，再发一次350ms上滑；实际截图确认
进入桌面、点击打开设置，最后锁黑屏。v2/v3失败及v4电池预检拒绝均保留。

`out/daily-brightness-runtime-v1-live`在发调光命令前达到die55°C退出，thermal
FAIL保持，不能算运行时亮度通过。随后独立的GPU关闭测试
`out/daily-brightness-idle-v1-live`通过：睡眠init运行期间614→460，退出自动恢复
614，两次读回确认；CPU/EFI事件/显示总线恢复通过，die峰44.4°C，电池后37.4°C。
这仍是底层亮度租用，不能代表系统滑杆可用。

`out/daily-brightness-observer-v2-live`：349 presents，0/0/0，最长2103us；
六恢复/thermal通过，电池36.7→37.1°C。通过BKS API读取的请求始终500/1000，
控制中心已打开，但第二次滑杆动作实际关闭控制中心，故滑杆变化验收不通过。
退出前自动锁屏截图为纯黑，证据见该目录manual-audit.json。
支持盘由内核APFS reader逐文件读取比对：仅新增observer、修改注入配置、删除
未启用的旧display-test描述；所有其他内容与daily-buttons基线相同。
此前observer-v1额外改变了GPU profile及遗留描述，未实机使用，另存比对失败记录。

复查旧`vsync-v13-panel-focus-fb4-live/ui/control-center.png`可见横向错位。
旧`oem screenshot`只是把活动显存指针交给USB分包发送，可能把不同时刻的内容
拼进一张图；这些截图不具备完整帧一致性，不能把错位直接归因于GPU或面板。
v14候选在每次scanout写入/黑屏前后发布奇偶sequence，EFI复制到事先预留的
16MiB私有内存并检查sequence未变，随后才通过USB发送；并发变更最多重试4次，
50ms总预算，无法取得稳定帧则明确失败。旧数据与原结论保留并增加本限制。
该改动修正验收采集路径，不能声称修复了设备实际可见的所有渲染缺陷。

## 完成帧快照 v14–v16

`vsync-v14-pages-focus-live` 在动画期间无法取得稳定显存副本，明确拒绝传输，
页面序列未执行。15 presents、0/0/0、最长1489us，六项恢复及thermal通过；
此版标为截图可用性失败，不能作为页面通过记录。

`vsync-v15-pages-focus-live` 在新内核启动早期USB消失并自行返回Android；没有
returned.log及资源恢复证据。原恢复结果保持未验证。使用同一loader、旧v13内核、
GPU关闭的`daily-snapshot-v2-idle-live`通过18秒短测，故仅将范围缩至新内核路径，
尚未证实具体根因，不将早期重启归因于某一个未经验证的映射错误。

v16取消XNU对外部固件大块内存的映射，改由IOBufferMemoryDescriptor分配16MiB
物理连续RAM。独立线程在与show/displayPower相同的锁内复制已完成composition，
清理cache后发布request/ack和物理地址；EFI验证范围、字节数及代次后才传输。
无新请求就不改缓冲，黑屏时输出零。该快照证明完成composition的一致性，
不是光学scanout观测，也不能替代瞬态源帧检查。

`daily-snapshot-v3-idle-live`已完成GPU关闭短测：request1、seq0、10368000字节，
图像全黑；CPU/EFI事件/显示总线恢复及thermal通过，die峰45.5°C、电池后37.5°C。
`vsync-v16-pages-focus-live`因启动前电池38.0°C被拒绝，没有boot或启GPU；
工具历史字段new_gpu_attempt不代表GPU实际启动，原字段不改写。

`vsync-v16-pages-focus-v2-live`：六次滑页实际覆盖负一屏、主页、第二页、资源库
及返回；完成帧快照11次请求成功。333 presents、0/0/0、最长1788us，六项恢复
通过，电池36.9→37.2°C、Android熄屏。全部301个保留源帧及五张完整contact
sheet复查未见大块红绿黑矩形；缩小源帧/完成composition仍不证明光学无撕裂。
最后黑屏按键释放期间触发die55.4°C保护，thermal FAIL及黑屏失败原样保留。
不能把页面与同步的独立通过改写成全轮通过。后续缩短测试，动作结束且确认黑屏
即归还资源；host预算从launch命令前计时，包含固件已开始计时的初始化阶段。

`daily-brightness-inventory-v1-live` 收到167条唯一backboardd方法ABI，包含
BKDisplayBrightnessController的setBrightnessLevel:reason:options:，签名为
v36@0:8f16@20Q28。SpringBoard的getter诊断已运行，但原类总数上限使其方法
清单为空，不能当作该进程不存在亮度类。准备操作滑杆时达到die55.0°C退出，
thermal FAIL保持；101 presents、0/0/0、最长1741us，六项恢复通过，电池
37.8→37.9°C，Android熄屏。下一候选只原样转发并记录匹配ABI的请求，仍不写面板。

`daily-glink-v1-live`：严格匹配原Mu GlinkDxe revision、14个wrapper RVA后，
对其音频DSP名称lpass注册并立即注销本地通知成功，active/dirty/invalid均0。
没有收到UP或DOWN通知，故不证明DSP链路在线。没有open/TX/RX/DSP启动或PMIC
写入，电池36.7→37.0°C，已回Android熄屏。精确ABI与远端名称参考保存在
daily-glink-v1-plan和daily-dsp-v1-plan；Linux DT中的adsp不能直接当作该UEFI
接口的远端名称。此结果尚未提供电量、音频或传感器数据。

## 请求观察与扫描相位等待 v17

`daily-brightness-requests-v1-live` 已按实机完整类型编码原样转发一次亮度请求，
收到默认500/1000；SpringBoard方法清单也已读取。90 presents、0/0/0、最长1806us，
六恢复通过，电池37.0→38.0°C。准备滑杆动作前die55.0°C停止，不能算滑杆通过。
`daily-brightness-requests-v2-live` 改2 worker并取消录帧，仍在动作前die55.0°C停止；
23 presents、0/0/0、最长1420us，六恢复通过，电池37.9→38.0°C、Android熄屏。
原截图错误、thermal FAIL与默认500记录均保留，不将其改写为滑杆变化。

核对XNU源码后确认clock_delay_until可按CPU idle latency选择睡眠或忙等；未证明
其250us等待在实机一定忙等，也未证明它是热停根因。v17根据真实行号预测下次
可用blank开头，留750us观察余量；较远时显式thread_block，临近仍250us检查。
醒来后必须重新检查实际frame/line、每帧最多一次及剩余copy预算。预测不授权提交，
迟醒不豁免边界或超时。新增独立phase-v1睡眠/短轮询计数，不改变原runtime统计。
主机边界、回绕、溢出、迟醒检查通过；`vsync-v17-idle-live` GPU关闭18秒短测通过，
die峰43.6°C、电池后36.0°C，黑快照和资源恢复通过。GPU验收另记，不由此推断。

恢复包复查发现旧power-v1和vsync-v13源码tar漏掉marble_dsi.h；原包hash校验仍
有效，但源码依赖闭包不完整。旧包不覆盖。新打包器要求所有marble_*.h都进入源码
清单，避免只验证归档内文件hash却漏掉未归档依赖。

`vsync-v17-brightness-live`：106 presents、0/0/0、最长1646us，phase-v1记录73次
粗等待、638785us等待时间及58次短检查（最后runtime采样）。首张0.75s锁屏截图
仍可见，黑屏验收失败并正常释放；thermal通过，电池37.0→37.1°C，Android熄屏。
`vsync-v17-brightness-quiet-live`取消touch_lat日志及全类ABI枚举，170 presents、
0/0/0、最长1829us，六恢复/thermal通过，电池34.1→35.2°C。首张锁屏图仍可见，
随后有界轮询取得纯黑完成帧。不同起始温度与诊断配置，不能把温差归因于单项修改。
两轮CC均实际打开，但slider-down仍50%、slider-up后者关闭CC；无新的BK/SB亮度
请求，故滑杆验收仍失败。v5只读UIKit hit-test观察候选用于区分目标命中和API链路，
不会重定向触摸、伪造slider值或写面板。


## 亮度命中诊断与 RAM 后端（2026-10-27）

`daily-brightness-hit-v1-live` 与 `hit-v2-live` 记录到控制中心亮度容器的实际
UIKit 命中；子 slider 的 interaction=0 不能单独证明根因。两轮分别 137/135
presents，failures/overruns/fallback 均 0，最长 1740/1745us；六项恢复、
黑屏及 thermal 通过。电池分别 33.3→34.4°C、32.8→34.0°C。
`daily-brightness-cap-v1-live` 确认 BKDisplayBrightnessController 的
isBrightnessLevelControlAvailable 原始结果为 false。190 presents，0/0/0，
最长 2084us；六恢复、黑屏、thermal 通过，电池 30.8→32.5°C。
这些结果只解释诊断过程，均未使滑杆可用。

新增亮度 mailbox 位于 RAM 支持盘尾 0x802000，独立于 APFS、触摸环和 Android
存储。capability、request、ack 各占 64 字节。固件仅在原面板身份、两次原亮度
读回以及显示计数器/时钟租用通过后发布 ready；attach 不写面板。请求必须是
稳定、递增的偶数 sequence，milli 0..1000、reserved=0。实际调光上限 4Hz，
只映射到同 BL_SYNC 区间的 [328, original]（低亮度分支下限 8），不超出启动
原值、不改 HBM/DC/PLL/PHY。写后读回，失败锁存；退出必须核验原亮度恢复。
这仍是有限范围桥接，不是完整显示驱动或自适应亮度。

`daily-brightness-backend-v1-live` 在 GPU 关闭小系统验证三档请求及 malformed
拒绝，399/542/614 均与面板读回一致，退出恢复614。CPU/事件/显示总线恢复通过，
电池29.3→30.0°C。但测试只持续约两秒，thermal-report 明确因不足五个运行
采样失败；report 的 passed 仅代表当时功能检查，不能当作全轮验收。
修订测试增加6秒静置观察，不修改温度阈值。
`daily-brightness-backend-v2-live` 三档及拒绝测试再次通过，温度报告、四项恢复
全部通过，电池30.0→30.1°C，Android已熄屏。原v1二进制和失败报告保留。

`daily-brightness-backend-gpu-v1-live` 固件后端ready，但BK hook报告backend=0，
未收到新滑杆请求。实机设备清单确认 /dev/rmd1 是 root 的0600字符设备；
触摸桥作为root可读，注入的iOS进程不能据此获得raw设备权限。113 presents，
0/0/0，最长2022us，七项恢复、黑屏和thermal通过，电池31.2→32.5°C。
控制中心显示正常，滑杆未通过，不能将后端功能通过当作UI通过。

后续候选将根权限raw访问限定在单独launchd服务，使用会话 RAM /private/var/run
中的192字节文件传递请求；服务只将校验后的16字节请求转发至原RAM mailbox，
不放宽raw设备权限。该服务及新的observer随后进行了实机UI验收，结果如下。

`daily-brightness-backend-gpu-v3-live` 验证滑杆请求经root服务、RAM mailbox到
面板读回的链路。`daily-brightness-cycle-v2-live` 三次手势实际命中亮度slider，
请求从500变化至448、356、635、857、696、496；面板读回相应变化，退出恢复
原值614。209 presents、failures/overruns/fallback均0，最长复制1825us；七项
恢复、黑屏和thermal检查通过，电池30.1→31.3°C。当前仅覆盖受限亮度范围，
不是自适应亮度或长期可靠性验证；RAM请求文件权限仍需收紧和兼容性验证。

会话时钟使用主机UTC epoch，仅写启动参数，不写持久RTC。边界和重复参数检查
通过；`daily-brightness-clock-v1-live` 实机读取与输入epoch一致，恢复及thermal
通过。iOS时区仍为默认Pacific，重启后需要重新提供时间。

`vsync-v17-settings-clock-v1-live`：449 presents，failures/overruns/fallback
均0，最长复制1571us；七项恢复、黑屏和thermal通过，电池31.1→32.8°C，die
采样峰49.9°C。441个缩小源帧的全部contact sheets覆盖设置打开、双向滚动及
退出动画，目视未见明显黑绿块；不能据此证明物理扫描无闪屏或撕裂。

`vsync-v17-pages-clock-v1-live`：257 presents，failures/overruns/fallback均0，
最长复制2102us；七项恢复、黑屏和thermal通过，电池30.1→31.2°C，die采样峰
49.1°C。65秒黑屏预算使六步动作只执行五步，不能算完整页面序列通过；录帧包含
负一屏、第二页和资源库及部分返回；303个缩小源帧全部contact sheets目视未见
明显大块黑绿区域，不能排除物理扫描或缩小后不可见的瞬态异常。

`vsync-v17-panel-clock-v1-live`：四个手势发出，237 presents，
failures/overruns/fallback均0，最长复制1854us；七项恢复、黑屏和thermal通过，
电池30.9→32.3°C，die采样峰49.9°C。270个保留源帧覆盖通知中心打开和退出、
控制中心打开，录制结束前未覆盖控制中心关闭。control-center静态快照拍早，
录帧确认其后确实打开。通知中心退出后约帧151至171可见局部壁纸矩形覆盖图标，
属于待定位的疑似合成异常，不能将该轮视为渲染验收通过。缩小源帧不是光学扫描。
源码复核：连续录制取layer 0的IOSurface，并非多层最终合成画面；因此该矩形
也可能是底层内容被其他图层覆盖，不能据此断言面板上的最终画面有同样的矩形。
`marble-scanout` 按请求在锁内复制完成的staging合成画面，但目前只有离散快照，
尚不足以覆盖每一帧。应先增加有界的最终合成帧录制，再复现并比较两个数据源。

## 60 FPS 诊断（2026-10-27）

新增 `tools/marble-frame-cadence.py` 按指定内核时间窗口统计已保留的底层
IOSurface帧间隔、p95及超过1.5个参考周期的间隔。它不将静止时没有新提交
误报为面板停止，也不能证明最终合成或光学帧率；必须先依据画面选取动作窗口。
环形顺序、截断与空窗口检查通过。

四线程的 `vsync-v17-settings-clock-v1-live` 两段设置滚动窗口约53.3/56.3
源帧每秒，退出动画窗口约16.0。两线程对照 `vsync-v17-settings-fb2-cadence-v1-live`
分别约57.7/60.1及17.3源帧每秒，单窗口长度不足一秒，动作及启动状态并不
严格等价，不能据此确定性能改善或稳定60 FPS。两线程在滚动相关统计窗口中
驱动平均约7.4–7.8ms，四线程约4.8–5.2ms；包含不同负载，不构成严格基准。
两线程437 presents、failures/overruns/fallback均0、最长复制1609us；七项恢复、
黑屏及thermal通过，电池28.9→30.2°C、die采样峰47.9°C，已回Android熄屏。
低帧率退出动画尚需分离GPU、应用合成与显示路径；目前GPU耗时统计为零样本，
不能将其当成GPU耗时为零。维持四线程配置及既有热保护。

### GPU 提交墙钟诊断

`mach-marble-gpu-submit-timing-v1` 在 `redwood_gpu_submit` 外记录墙钟时间，
不改变GPU等待或资源生命周期。构建链接成功；上游四项导出检查仍失败，不能
称make全部通过。`gpu-submit-timing-v1-settings-live` 实际打开设置并滚动：
两段滚动相关的约5.4秒窗口分别有1260/1311次提交，后端平均955/960us，
最大3395/4158us。这是CPU调用总耗时，含同步等待及检查，不是硬件GPU时间；
窗口含其他活动，也不能把提交次数直接归因于某一帧。后端每次提交都等待完成
并确认静止，且SMMU映射更新依赖静止检查。异步化需先处理围栏、资源持有、
映射变更及退出顺序，不能简单删除等待。

本轮438 presents，failures/overruns/fallback均0、最长复制1746us，七项恢复、
黑屏及thermal通过，电池28.6→30.4°C，die采样峰48.3°C；已回Android熄屏。
该版是诊断，未实现动画提速或证明60 FPS。

### 2026-10-28：上下文复用、有限队列和缓冲区依赖

后续内核保留 NativeAdreno 的 cache prologue，只在 client/queue 切换时重置
上下文。来源工具 `marble-native-ring-provenance.py` 从固定上游归档生成补丁；
上游 commit 为 `b686b9054f57d393c80195b82568a09f8ca106a2`。
环缓冲区先持有资源，再发布指令；不确定完成或故障后保留资源，直到复位。

`overlap-v2` 开始按实际 fence、RPTR/shadow、健康状态及全局 idle 回收。
`queue-v3` 最多允许四组有序批次，使用最早一组的一秒超时，并在最新 fence
和全局 idle 均完成后回收整组。零长度 CPU barrier 不排空队列；SMMU 更新、
未知范围 cache 操作和退出仍排空。pending 和 native-ring 的 ASan/UBSan
回归通过，包括深度1..4、wrap、超时、故障后保留；不代表完整设备可靠性验证。

`cache-v4` 将已知范围的 cache 操作改为只等待该 BO 的最后使用 fence，
并合并共享 IOSurface 的最后使用 fence，以覆盖别名和其他 client 的读写。
无关缓冲区可以继续排队，clean/invalidate 触发条件和频率未改变。
`gpu-cache-v4-settings-live` / `gpu-cache-v4-pages-live` 均完成自动输入、七项
恢复、黑屏、thermal 和回 Android 熄屏。分别442/326次呈现，最长面板复制
2375/2164us，failures/overruns/fallback均0；电池30.5→32.1、30.3→32.2°C。
这些呈现总数不是 FPS，也不能证明光学无撕裂。

录制格式 RWREC004 改为在 mutex 内、面板复制完成后保存缩小的多层最终合成
staging 图像。旧 RWREC003 是底层源帧，两者不能直接当同一种视觉证据。
`marble-frame-cadence.py` 对选定窗口注明此区别；3项 Python 回归通过。
已选取的设置退出窗口仍约21–23次完成合成/秒，稳定60 FPS尚未实现。
`animation-home-queue-v3-live` 的第一次 Home 未退出设置，下一次点按进入
Screen Time，因此排除该轮性能对比，不能以 marks 完整代替页面验收。

### 用户态对照与拷贝发布修正

`eagl-scan-v2-plan` 保留现有 surface_changed 标量 hash，增加扫描耗时计数，
只压缩后期 GL 统计日志。scan-v1 设置测试中，约355MB/3274次扫描总计约
173ms，当前证据不支持将该 hash 认作主要瓶颈。

独立 `nofetch-sysmem` profile 仅强制 Mesa sysmem，继续向 CoreAnimation
隐藏 fetch 扩展。scan-v1/default 与 nofetch-sysmem 的短设置退出窗口分别
22.88/22.69次完成合成/秒，详见 `out/animation-cadence-sysmem-20261028.json`。
窗口不足一秒、启动状态不同；没有证实提速，也不是持续 FPS 或光学测量。
这两轮七项恢复、黑屏及thermal通过，电池30.5→32.1/32.0°C。

`eagl-publish-v3-plan` 修正 glFinish 之后新增 mirror blit 的发布：在全部
mirror copy 发出后统一 glFlush，并清除此前 finished 状态，随后执行原有
display-wait 或 synchronous-wait 策略；fence 分配失败退回 glFinish。
原策略会把调用者的早先 glFinish 误用于后来才发出的拷贝。该改动仍需按场景
持续验证，不声称它是现有卡顿或色块的已证实根因。
从实际 frame_end 发布代码生成的 mock GL 顺序测试8项通过 ASan/UBSan；
测试不执行 GPU。`gpu-cache-v4-publish-v3-settings-live` 完成设置自动输入，
七项恢复、黑屏及thermal通过，电池31.7→33.1°C，die采样峰51.0°C；439次
呈现、最长复制2061us、failures/overruns/fallback均0。已审阅的退出窗口
未见大片黑绿块，短窗口22.82次完成合成/秒，仍未修复动画流畅度。

上述源码内核编译链接成功，但继承的 IOKit、Libkern、Private.symbolset、
check_all_exports 四项导出检查仍失败（make返回2）；不宣称完整构建检查通过。

共享输入加 fetch 的独立对照 `gpu-cache-v4-publish-v3-fetch-settings-live`
确认 profile=shared、fetch=1、inputs=1。设置退出选定帧380..395约42.49次
完成合成/秒（median19.18ms，p95 34.12ms），比此前选定约23窗口更快，但仍
不足稳定60，也不是严格受控基准。该轮七项恢复、黑屏及thermal通过，电池
33.7→34.7°C。不能据单场景速度提升改为默认。

`gpu-cache-v4-publish-v3-fetch-pages-live` 六个动作均发出，App资源库确实可见，
已审阅的资源库窗口未见大片黑绿块。第一步 Today 手势未留下稳定负一屏，
录像中回到原桌面，因此负一屏覆盖不足，不算完整页面验收。七项恢复、黑屏及
thermal通过，电池34.8→35.3°C。

`gpu-cache-v4-output-timing-fetch-panels-live` 明确复现渲染失败：通知中心退出
帧179..189出现黑块；控制中心帧260起出现持续大片黑色三角区域。该轮不能
通过视觉验收，shared-fetch 继续保持实验 profile。344次呈现、最长复制2350us、
failures/overruns/fallback均0仍无法排除这类 GPU 合成错误；七项恢复、黑屏及
thermal通过，电池35.8→36.2°C，die采样峰53.8°C。

`eagl-output-timing-v1-plan` 仅在 MARBLE_GL_OUTPUT_TIMING=1 时打印大尺寸
surface 的发布时刻、前一输出以来的GL耗时/flush/等待和提交组数量，并补记
glFinish 的CPU墙钟耗时。fullsize surface publication 不等于面板帧。
`eagl-frame-errors-v1-plan` 另外提供显式 MARBLE_GL_ERROR_POLL=frame 候选，
将启动期每API调用的额外错误查询改为每flush group一次，应用自身的glGetError
仍转发；尚未实机验证提速。Mesa当前 _mesa_GetError 主要读取/清除ErrorValue，
不能未经测量宣称它造成GPU同步或是卡顿根因。

### CA fetch 独立快照候选与动画复测

`eagl-fetch-copy-v1-plan` 新增显式 `MARBLE_GL_FETCH_COPY=1`：继续向 CA
宣告 fetch，保留其着色流程，但将实际 CA fetch shader 改写为从独立纹理按
framebuffer 坐标 texelFetch。每次相关 draw 前复用有序 GPU 快照 helper，避免
读写同一 attachment；默认 nofetch 配置不变。生产 helper 和四种 shader 的主机
llvmpipe ASan/UBSan 回归56组、14544通道通过，不代表 Adreno 验收。

`gpu-cache-v4-fetch-copy-v1-panels-live` 已审阅 sheets064/128/192/256，
旧通知中心退出黑块及控制中心大片黑三角未在这些窗口出现，背景模糊保留。
最后控制中心关闭动作没有确认到桌面，不能称完整面板验收。七项恢复、黑屏、
thermal通过，电池35.3→35.8°C，die采样峰53.4°C。

`gpu-cache-v4-fetch-copy-v1-animation-live` 第一次设置退出帧130..159约
35.09次完成合成/秒（median32.68ms，p95=63.00ms），第二次退出误成列表滚动，
排除第二段。对应第一段 fullsize GL发布统计均值约25.33ms、其中flush墙钟约
9.11ms，等待约1.21ms；统计不等于GPU执行时间。七项恢复、黑屏、thermal通过，
电池36.4→36.5°C、die采样峰54.2°C。已审阅sheets128/192/256。

将 animation-focus 自动退出起点移到y2398后，
`gpu-cache-v4-fetch-copy-frame-errors-animation-live` 两次实际打开/退出均确认。
选定退出帧154..178与257..284约30.76/37.84次完成合成/秒，未证明仅将额外错误
轮询改为每组一次就提速。七项恢复、黑屏、thermal通过，电池35.9→36.6°C，
die采样峰54.2°C。已审阅sheets128/256；没有完整视觉通过或稳定60 FPS结论。

`eagl-backdrop-grow-v1-plan` 另提供显式 `MARBLE_GL_BACKDROP_GROW=1`，
仅在尺寸超过已有容量时分配快照纹理，尺寸变小时保留容量并只刷新所需区域。
所有快照消费者使用整数 framebuffer 坐标，纹理容量不会改变采样坐标。
生产 helper / shader 主机像素测试扩展为精确尺寸和增长容量两模式，包含宽高
交替变化及变小后复用，ASan/UBSan共128组、34048通道、GL errors=0。
此优化仍是候选，收益和设备渲染正确性需单独实测；不改变频率、电压或默认配置。

`gpu-cache-v4-fetch-copy-grow-v1-animation-live` 仅执行第一组打开/退出后触发
现有55°C die保护，未执行第二组；七项恢复通过，已回Android熄屏，电池
37.0→37.1°C。thermal报告正确判失败，不能作为完整通过或提速证据；没有
立即自动重跑。已审阅sheet128，退出前存在明显长间隔，不给该轮提速结论。

后续离线 `eagl-copy-timing-v2-plan` 修复增长容量候选的 unpack PBO 状态：
`glTexImage2D(..., NULL)` 只有在没有绑定上传PBO时才表示仅分配存储，因此
临时解除该绑定，分配后恢复。更新主机用例始终绑定仅1字节PBO，旧helper
负对照明确失败；修复版128组、34048通道、GL errors=0、ASan/UBSan通过。
不检查内存泄漏；不代表设备验收。生产代码另提供显式output-timing的快照
调用次数、分配次数、像素和API墙钟分项，尚未取得设备计时样本。

支持盘 `marble-root-fetch-copy-grow-timing-v2.apfs` 已离线构建，仍非默认，
尚未实机。可复现主机验证命令：

```powershell
python -X utf8 tools/marble-backdrop-test.py --out out/NEW-host-test --run-vm
```

当前结论：fetch-copy在已审阅窗口改善原生fetch面板错误，但设置退出约31–38
次完成合成/秒，稳定60未达；增长容量候选没有可接受的实机收益证据。

2026-10-28后续已完成独立GLES最终缩放候选、seq绑定与晚到丢弃恢复、FD725
像素自检和3×对照。3×未证明提速；2×内部渲染有速度收益但出现红顶栏与黑壁纸
回退，未作为默认。细节、原始证据与局限见 `MARBLE-GPU-SCALE-20261028.md`。
