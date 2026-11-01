# Marble ADSP / QMP 外围进度，2026-10-29

这是音频与真实电量服务的依赖准备，不是音频、SOC 或 DSP 生命周期验收。
所有实机操作仅临时 fastboot boot；没有刷写、擦除或 Android /data 访问。

## v40：实际依赖盘点

`out/daily-adsp-power-v40c-live` 完成只读盘点：QMP 原生配置和恒等映射均匹配，
物理窗口 c300000/1024，version1/features0。AP 邮箱偏移128h/728字节，对端
c0h/104字节，两边当前消息长度均0。链路均UP，AP通道/确认DOWN，对端均UP。

NPA 图82项且正常终止，79项有定义，3项为合法占位符：`/init/qmp_client`、
`/ipa/pc`、`/pmic/client/xo`。XO占位符不能当可用时钟资源；`/icb/arbiter`
state1也不能当特定crypto_ddr带宽租期。电池32.1→32.3°C，Android熄屏。
v40/v40b两次较早拒绝的日志和恢复记录保留，未改写成成功。

## v41–v44：复用原生 GlinkDxe

原始 Mu GlinkDxe SHA256：
`9fbbf72bc75995c01dd13a31b50eb83634d548202552b600cadbe57bcea443dc`。
使用原生 `qmp/rpm/aop_apps` 通道，144字节配置；Open/Close无This参数。
槽11是GlinkRpmRxPoll，但该QMP实例的transport+88没有轮询函数。
协议后部RPM teardown不是初始化入口；未调用。

- v41：在调用Open前拒绝。实际原生transport低层state1、core link1、channels0，
  虽然共享描述符的link/ack已UP，原生核心仍未处理完成。
- v42：只读监听表发现唯一相关监听者ClockDxe e358；第一版priv检查太严而拒绝。
  已核对其实际priv=ClockDxe+2ece0。此回调只更新本地标志，不开通道、不发送业务。
- v42b：受控调用原生已注册事件函数72e0后，features0选择generic后端d190；
  旧检查只接受启动时的d3c0，因此拒绝继续。没有Open；watchdog恢复Android。
- v42c：接受两套已核对的原生后端，连接/关闭回调均成功。关闭后还出现remote-down，
  旧“原回调历史不变”条件失败；失败日志和watchdog恢复保留。
- v43：单独检查双方均DOWN也失败。读回是AP DOWN/DOWN、对端UP/DOWN，core没有
  通道。remote-down回调不代表AOP的声明状态已DOWN，不能将其当双方关闭。
- v44：关闭后按稳定transport再处理一次原生事件，避免使用已释放的handle。
  最终AP DOWN/DOWN、对端UP/UP，完整60字节描述符回到原值；本地client和回调均
  释放，仅留下原生驱动自有的remote-only节点：local0/remote2/refs1、11个客户端
  字段全0。与旧legacy回调历史条件不同，`legacy_contract_pass=0`仍保留。

具体补充处理：原生初始化6ad4提前确认已UP的对端；7340在link-up后看到相等的ACK
会跳过其core-open通知。在原生link2、无通道、对端确实UP的条件下，使用其原生
core回调7dc4对已存在的远端状态做一次协调；没有伪造共享描述符或修补函数表。
所有原生事件调用均屏蔽本地IRQ以防重入，保留原DAIF，且预先只允许已审计的
ClockDxe被动监听者。出现未知监听者则拒绝，不能据此开放任意原生回调。

v44 两轮同一镜像实机结果：

| 证据目录 | 生命周期耗时 | 事件调用 | 状态收敛轮数 | Android电池前→后 |
| --- | ---: | ---: | ---: | --- |
| daily-qmp-settled-close-v44-live | 5245us | 6 | 1 | 35.9→36.5°C |
| daily-qmp-settled-close-v44b-live | 5221us | 6 | 1 | 36.7→37.0°C |

两轮均active0/dirty0、watchdog_clear0、描述符恢复、本地client释放、冷复位boot ID
变化及Android屏幕OFF通过。温度前后值来自Android BatteryService，不能冒称为
固件期间的连续电池峰值；固件另做真实VADC和TSENS进入检查。

未发送TX业务消息、未调用NPA或PAS、未启动DSP、未访问存储。没有以改变电压、
提高频率或重试失败PAS停机来获得这些结果。v44为firmware-only，拒绝进入XNU；
现有桌面/显示使用原已验证镜像。

## 可复测入口与验收边界

```powershell
python -X utf8 tools/marble-gpu-preview.py --out out/NEW-live `
  --boot-image out/daily-qmp-settled-close-v44-plan/loader-boot.img `
  --ramdisk out/daily-qmp-settled-close-v44-plan/test.apfs `
  --system-disk out/marble-system-zh-v1.apfs `
  --discovery-only --qmp-native-settled-close
```

`tools/marble-qmp-native-report.py --settled-close LOG`单独验证证据，拒绝缺失、重复、
乱序、时限超出、回调残留、混用旧关闭契约或虚构原状态恢复。Host要求独立探测，
不允许混入PIL/NPA/屏幕测试；dirty/attempted后loader只允许日志、状态和复位。
不确定关闭时保留静态回调数据和watchdog，不盲目释放或继续驱动调用。

v44阶段99项Python检查通过，EFI marble/generic两目标和12项C检查通过；C覆盖
1,185,921组QMP范围边界和256组开通/关闭状态组合。保留真实v44精简日志作为
解析器fixture，不把日志PASS文字单独当成功证据。

## XO / SCM 带宽的下一阶段依据

`out/adsp-dependency-source-v1-plan`固定同一Xiaomi commit
`48952ed36228217531482b39d5bef13e7fd808ec`的clk-rpmh.c、rpmh.h、waipio.c和
interconnect绑定，各自SHA在references.json。marble DTS与PAS源码也复制并校验。

- DTS的ADSP `clocks=<0x20 0>`实际指向`qcom,waipio-rpmh-clk`的RPMH_CXO_CLK，
  对应`xo.lvl`。Linux实现的使能值3和sleep/wake/active聚合需要一起处理。
  这不是证明原生`/pmic/client/xo`占位符与它等价，不能直接猜Clock协议名为xo。
- `crypto_ddr`来自aggre2_noc MASTER_CRYPTO43到mc_virt SLAVE_EBI1 512。
  PAS源码请求avg0/peakUINT_MAX，并在认证和停机周围持有；不是仅有arbiter即可。
- 仍需先证明XO和特定带宽租期及释放，再做新的DSP生命周期方案。v34b/v38的
  PAS执行入口继续禁用，QMP单独通过不允许直接重开旧DSP实验。
- 音频数据面应复用marble-s-oss音频源码的GPR/SPF；PMIC真实SOC也仍需ADSP服务。
  当前没有PCM、扬声器、麦克风、真实SOC、无线或传感器完成声明。

## v45：复用命令数据库解析器核对 XO

`out/daily-adsp-xo-v45-live` 单独完成只读核对，boot镜像SHA256为
`9cffb30a8c276584aeb35ea47919d5bf2f26043c0bdb442269ca76438147096a`。
未混入QMP/PIL/NPA或桌面动作。

32个4KiB页均确认恒等映射；对80860000/131072字节做两次完整RAM快照，逐字节
一致。复用原GPU阶段的`marble_gmu_preflight.h`命令数据库解析器，未复制另一套
解析实现。新增宏仅让EFI复用纯解析部分，原XNU编译分支默认不变。

实际找到唯一`xo.lvl`：ARC地址`0x300b0`、slave3、目录version4096、aux32字节，
内容为`0000200050008000000000000000000000000000000000000000000000000000`。
只记录原始元数据，不把其条目解释为已测频率或电压，不将地址直接用于MMIO。
两快照共读262144字节，耗时5811us，free_status0，未创建时钟/RPMh/NPA/QMP/PAS
请求。命令数据库checksum未做认证，`lease_accepted=0`保持明确。

Host复审通过；Android电池34.5→35.1°C、屏幕OFF。EFI两目标、13项C检查通过；
含原解析器4096组变异、截断/溢出/歧义拒绝检查，当前103项Python检查通过。
仍不允许据此启用未核对的XO投票或旧DSP执行路径。

恢复记录：`out/checkpoint-peripheral-qmp-v1-20261029`保存v44阶段完整源码与成功/
失败证据，原样保留；后续v45另存v2。二者均为增量外围里程碑，需配合不可变的
`checkpoint-ui-peripherals-v8-20261029`所保存系统盘、旧构建材料使用，并依赖本机
外部工具链/运行环境；不能当作已完成全部外围或独立包含全部工具链的发行包。

后续v46–v49原生RPMh零命令客户端与ICB路由盘点详见
`MARBLE-ADSP-NATIVE-LEASE-20261029.md`。两轮客户端恢复、完整1018条路由
已验证；XO/带宽业务租期、DSP/音频及真实SOC仍未完成。

## v70–v71：业务 load_state 传输与固件 GLink 接口不匹配

v70固定10段代码，使用发布的GlinkTx无This六参数接口。新鲜通道
connected后，第一个64字节on请求返回EFI8000000000000021/result-5；没有发送到
邮箱、没有TX回调。保留watchdog/静态缓冲直到整机复位，Android熄屏，电池
30.6→31.5°C；没有把独立返回Android当作原地释放通过。该命令在host/loader均
禁用，镜像functional-status为FAILED，不原样重试。

原因与原生指令对应：QMP描述符features0，协商选择generic core/d190，GlinkTx
27c0→27d8→3948→3084要求remote RX intent；QMP mailbox wire没有这种intent公告，
不能伪造一个远端GLink意图或修改native函数表来绕过它。

v71复用同一个原生QMP transport+60/RVA7000，提供本地静态0x30字节transfer
descriptor。该函数逐32位字复制64字节、发布长度，再通过4e90触发原有通知。
参数、传输路径、native完成与队列初始化共13段SHA256已与固定原始EFI核对。
只在两端通道UP、两个邮箱空、原生四个意图队列均空、transport无在途传输时调用。
完成条件为远端清空AP邮箱、native事件清除inflight、原生队列仍空；这是Linux
qcom_aoss.c的QMP传输ACK，不是GLink TX callback或电源状态的独立物理读回。

消息精确使用marble Linux qcom_q6v5_pas.c的64字节零填充on/off字符串；on ACK后
等待10ms再off，各有250ms时限。静态消息/transfer在不确定结果时保留到冷复位。
不通过原生GLink API发送、不向远端所有的状态或函数表写入，不调用PAS或DSP执行。

v71/v71b同镜像两轮通过：on/off分别约1.1ms确认，整体17490us（第一轮），随后
settled-close恢复60字节描述符、本地client与回调释放，只有remote-only节点保留。
两轮均显式冷复位、boot ID变化、Android熄屏；电池31.2→31.8°C、32.6→33.4°C。
156项Python、21项C、marble/generic EFI和75段原生代码核对通过。

恢复镜像为out/daily-qmp-mailbox-v71-plan/loader-boot.img；实机记录在同前缀
v71-live/v71b-live。它是firmware-only专用镜像，不能启动桌面。现有UI恢复点及
外围v1–v5没有覆盖。后续先将QMP on/off嵌入LPI/XO/带宽/ROM无执行租期，不能
由此宣称DSP停机、GPR、PCM、扬声器、真实SOC或全部外围完成。

## v72–v74：组合顺序审计，失败结果保留

v72在on ACK后被旧XO隔离guard拒绝；没有XO请求或ROM/PAS操作，watchdog冷复位
恢复Android并熄屏，电池32.3→33.0°C。v73新增明确owned-QMP gate后进入ROM阶段，
最早失败是流式PAS-metadata-init-before后USB Too many links；没有init-return、
auth/reset或DSP执行。其后oem log超时是第二错误，不能替代第一失败。Android独立
返回、熄屏，电池33.1→33.0°C；原地释放/内存恢复没有通过。

v73镜像SHA256 f68ad6bc55ef7256d9824bd5f01edc13ff12af902640408e1ca9235c7128e861。
functional-status标FAILED，host新capability拒绝该旧顺序镜像。Android读取
ro.boot.bootreason/sys.boot.reason均为bootloader，pstore为空，没有可用last_kmsg；
这些结果不足以确定是secure reset还是watchdog，不能从USB错误推断具体复位根因。

重新核对固定Linux qcom_q6v5_pas.c：adsp_load通过MDT/PAS初始化和装载；之后
adsp_start才进行load_state:on及auth/reset。v73将on包放在PAS初始化之前，不符合
该先后关系。v74把QMP开始移到实际ROM装载完成之后，使用PIL post-load hook，
保持原PAS流程，只有已验证shutdown、内存恢复和RAM文件释放后才允许off/close。
QMP本地会话跨hook保存在静态结构，不保留已返回函数的栈指针；任何不确定结果
继续保留外层供电/带宽/watchdog，不发送off或盲目恢复内存。XO仍在原隔离入口
申请，v73组合XO例外已撤回；QMP新capability为qmp-ROM-post-load-gate-v3。

v74已离线构建：160项Python、21项C、marble/generic EFI及75段原生代码核对通过。
实机结果需另记，不能把离线通过或启动顺序依据说成硬件验证成功。

v74实机确实取得PAS init/memory-setup/native-all-segments返回0，随后只到
PAS-metadata-shutdown-before，USB断开，无shutdown-return。没有auth/reset。
Android独立恢复，熄屏，电池31.9→32.1°C。其SHA256为
1192dfc8f69875a319f3bc18047a057b73bf7c3d88fc1b64d7e6d3ba0d7983ca，标FAILED。
主机已保留第一命令异常，同时将后续oem log超时存为单独adsp_owned_log_error。

v75核对Linux未认证错误路径：先disable_load_state，最后qcom_mdt_free_metadata(err)
调用PAS shutdown retry。因此改为装载后on/off/close全部完成，再进行PAS元数据
清理。流式QMP-noexec-off-close-return=1证明确实到达该分支；仍然在后续
PAS-metadata-shutdown-before失去USB，未执行DSP，也没有原地资源恢复证明。
Android独立返回、熄屏，电池31.9→32.1°C。SHA256为
540d504e9ec9f130f1947e6bf731edf36b73ddce0f12af24583ddc31ca5f07df，标FAILED。

v73/v74/v75均在host按镜像SHA拒绝重跑；当前loader的adsp-qmp-stage和capability
也禁用。v68b无执行与v71独立QMP恢复镜像仍保留。不能将换了顺序后的失败写成
PAS或音频通过；也不能由这些结果证明带宽就是根因。

下一独立实验v76只验证原生RUN下Linux SCM逻辑峰值带宽：固定同一Xiaomi commit
的interconnect.h、interconnect-provider.h、icc-rpmh.c，SHA与原文另存
out/scm-bandwidth-v76-audit。Linux PAS peak UINT_MAX的单位是kB/s，换算为
4294967295000 bytes/s请求；这不是硬件实际吞吐率。原生BCM算法在14位限幅，
不写CPU频率或电压接口。新profile为icb-only，排除PIL/QMP/UI命令；仅10ms持有，
5秒watchdog，核对所请求/ACK的BCM票、峰值期间温度、Complete/Destroy后原聚合
和原投票恢复，再冷复位。该数值从未用来启动DSP，之前的1MB/s不能当等价验收。

v76/v76b使用同一镜像两轮实机通过：12个kind0 BCM计算值与实际请求值、ACK一致，
其中5个达到14位限幅；原native completion在持有与释放阶段均一轮排空。首轮
带宽子阶段12075us，峰值期间TSENS38.9°C；之后Complete/Destroy恢复19个BCM
当前聚合、原有cache/vote值与NPA客户端，history/固件RUN留至冷复位。
两轮均明确复位、boot ID变化、Android熄屏；电池31.0→31.2°C、32.7→33.0°C。
164项Python、21项C、marble/generic EFI与75段native指令核对通过。

恢复镜像out/daily-icb-scm-peak-v76-plan/loader-boot.img，实机证据同前缀
v76-live/v76b-live。仅证明该逻辑峰值请求/ACK/释放可用，不证明实际总线吞吐率、
SCM故障根因、DSP停机或音频可用。下一步仍先做完整无执行ROM组合，QMP/PAS失败
入口保持禁用；不把单一前置条件通过当作全部外围完成。

## v77–v79b：峰值带宽组合与独立 wire 的实际结果

v77 在 Linux SCM 逻辑峰值内完成完整 ROM 无执行流程：PAS init、memory setup、
装载/读回、shutdown、49 MiB 备份恢复、RAM 文件清理和带宽/LPI 释放均通过。
外层 LPI 约 3600354 us，电池 32.1→32.8°C，冷复位及 Android 熄屏确认。
原 exercise.json 的 scope 文字误留“1MB/s”；原始证据不修改，实际 raw log 和
bandwidth_ib 字段为 4294967295000。不是 QMP 或 DSP 执行通过。

v78/v78b 只完成离线构建。v78c 实机在峰值带宽下 native QMP on/off/close 返回1，
DAIF 前后 0x300、PAS shutdown 参数/interface 校验1；仍只到 shutdown-before
而没有返回。没有 auth/reset。电池31.8→32.8°C，Android恢复及熄屏通过。
SHA256 f51b2e94be7cfe019aae2a41dd261fc2a6ee638cb5dae96d12333eaa86a0e0aa。
adsp-scm-qmp-stage 的 host、dispatcher 和 capability 均保持禁用。
这排除了“补上该峰值就能恢复 shutdown”的假设，不能证明具体复位根因。

v79 直接适配固定 Xiaomi qcom_aoss.c 的 AP 通道/邮箱，复用已审计的 Mu doorbell，
不调用 native GLink open/close/event，不改函数表。快照 transport 38/core 19 个
u64、Clock flags 和 DAIF；所有不确定结果保留10秒 watchdog，最后冷复位。
v79-live 在 Android ADB offline 的温度预检失败，未启动镜像；该目录熄屏未确认，
不计硬件测试或恢复通过。原样保留。

v79b-live 实机 on/off 分别1067/1078 us取得邮箱清空确认；36次 AP 写、4次kick。
固件 heap、Clock、DAIF未变化，但关闭后原60字节描述符未恢复，整体失败，未调用
PAS/NPA/DSP。保留watchdog至Android恢复；boot ID变化、熄屏确认，电池35.0→35.3°C。
不能只凭两个传输ACK放开组合或DSP执行。v80仅增加关闭前后描述符日志用于定位，
不将失败描述符直接覆盖为旧值，也不把重启恢复当作原地释放成功。

v80 关闭后观测 local_channel/ack 均 DOWN、remote_channel DOWN、remote_channel_ack UP，
其他链路/geometry、固件 heap、Clock、DAIF 未变化。整体失败；watchdog恢复 Android
及熄屏，电池35.9→36.0°C。定位到漏掉 AP 对 AOP 通道 DOWN 的确认；名字中的 remote
不代表 ACK 字段由远端写。固定 qcom_aoss.c 的 UCORE_CH_STATE_ACK 为 AP 写，Mu
native event 7460/7480、7650/7660 也分别按实际远端 DOWN/UP 写同一 ACK。

v81 增加有限状态握手：只在读到实际 remote DOWN/UP 时写 AP ACK (offset0x18)，
不改远端 state，未知值/geometry/link 变化立即拒绝。生产纯状态判断函数已有全字段
破坏/乱序/边沿缺失检查，报告器要求两个有序 ACK；capability 升为 ack-wire-v2。
首轮38次 AP 写、6次kick、2个消息ACK，119164us完成；原描述符、transport/core
heap、Clock flags、DAIF全部恢复，跨命令getvar再次确认，冷复位/熄屏通过，
电池35.9→36.0°C。证据 out/daily-qmp-wire-v81-live。

v81b在执行前的固件电池读数38°C被门限拒绝，AP_writes=0、PAS/DSP均0；
不是握手复测失败，也不算第二次成功。Android恢复、熄屏确认，电池36.0→37.1°C。
先自然降温，不降低门限或修改充电设置。

v82 为独立的 adsp-scm-wire-stage 候选：在已有 SCM 峰值完整ROM无执行租期的
post-load hook 内调用 AP wire，并在 PAS cleanup 前验证原描述符/固件上下文，
随后再次核对跨命令 preservation。旧 native QMP/PAS 失败入口不开放，DSP auth
继续禁用。177项Python、21项C、两种EFI和75+13段native代码固定检查通过；
这只是离线构建结果，仍需先补齐独立wire的第二次实机成功后才做组合测试。

降温后 v81c 同一镜像完成第二次独立实机成功：117689us，38次AP写、6次kick、
消息ACK2，原描述符/固件上下文/Clock/DAIF与跨命令preservation全部通过。
冷复位boot ID变化及Android熄屏确认，电池34.8→35.1°C。被温控拦截的v81b
仍独立保留，不计成功。随后才允许尝试v82的SCM/ROM无执行组合。

v82 实机：init/memory/all-segments均返回0，AP-wire-noexec-unwind-return=1，
shutdown参数有效1、DAIF300；仍在PAS-metadata-shutdown-before断USB。
没有auth/reset，不能称PAS原地释放成功。Android独立恢复、boot ID变化、熄屏确认，
电池36.0→36.2°C。SHA256 25f8614bd4602c637b85ab07285443d4f81bf5047dbee7ae1ea9ea8f5a11fed0。
host/dispatcher/capability已禁用该入口，保存functional-status FAILED，不原样重跑。
独立wire已两轮证明可恢复固件上下文，组合内也通过preserved检查，因此native GLink
heap/Clock/DAIF改变不足以单独解释故障；不据此宣称找到了全部根因。

v83候选保留同一SCM峰值/ROM装载租期，仅做AP channel开关和远端DOWN/UP的主机ACK：
4次AP状态/ACK写、4次kick、无邮箱payload写、无load_state业务消息、无DSP执行。
与v82唯一业务差异是去掉on/off消息，用于区分通道握手与业务电源状态切换的影响。
capability SCM-ROM-AP-channel-noexec-v1，专用入口adsp-scm-channel-stage。
报告器要求独立channel模式证据，不能把ACK0误报为业务传输成功。
179项Python、21项C和两种EFI离线检查通过，实机结果待补。

v83 实机在通道握手/原状态检查返回1之后仍停在PAS-metadata-shutdown-before，
init/memory/ROM均已返回0，没有auth/reset。Android独立恢复、熄屏确认，电池
34.8→35.5°C。该入口已在host/dispatcher/capability禁用，失败镜像另存分类。
因此业务load_state消息不是重现停机故障的必要条件；不再通过变换消息顺序盲试PAS。

重新核对固定Mu ScmDxeLA（e6ff7055...95812）：Call2 wrapper 3cb4先调用
BootServices RaiseTPL(8)，进入3880核心，最后3d14调用RestoreTPL(old)。之前
“没有Call2返回”并不能区分SMC内部、进入TPL或恢复事件队列的停顿，不能直接等同
于已证明secure调用本身不返回。新v84只做TPL8进入/恢复，在已验证AP wire前后各一次，
保留10秒watchdog和流式阶段记录；不调用SCM/PAS、不装载ROM、不创建NPA客户端。
180项Python、21项C和两种EFI离线检查通过，结果待实机验证。

v84 实机两个TPL roundtrip均通过，before/after原TPL均4、requested8、DAIF300不变。
wire、跨命令preservation、冷复位和熄屏通过，电池34.9→35.2°C。
没有SCM/PAS/NPA/DSP调用。没有复现“单独RestoreTPL就停顿”的猜测。

v85新增固定Xiaomi同一commit的qcom_scm-smc.c、qcom_scm.c、qcom_scm.h与SMCCC
头文件，5个原文及SHA在out/scm-waitq-v85-audit。Linux只把INTERRUPTED=1当作
直接重入，WAITQ_SLEEP=2/WAKE=3另行处理；Mu 4734/4760/4764则对所有正值循环。
Mu该函数也未显式初始化首次x6/x7。两处都是待观察的差异，不是已证故障根因。

独立raw查询v85只发送IS_CALL_AVAILABLE(target=0x02000206)，分别使用本机Linux的
standard0x42000601和既有EFI fast0xc2000601，A6/A7初始化0。只对1按Linux保留
A6 cookie并重送原x1..x5/x7；2/3不重放，最多64次/250ms，10秒watchdog保留。
4个查询在wire前后均raw_a0=0/raw_a1=1，一次调用完成，未观察WAITQ；原TPL4、
DAIF300及固件上下文未变。冷复位/熄屏通过，电池34.5→35.2°C。181项Python、
22项C、两种EFI通过；最终EFI中的寄存器加载/SMC指令另存smccc-compiled-audit.txt。

v86候选只将新SCM峰值/完整ROM/wire无执行流程的shutdown换成初始化全部8个寄存器
的有限观察适配器；原native Call2仍供既有路径使用，不改固件函数表。专用入口
adsp-scm-bounded-stage，只有完整ROM已校验、未auth、两路LPI/SCM峰值仍持有、wire
原状态恢复时才允许调用PAS1 shutdown。WAITQ/错误/超时保留静态返回寄存器、metadata、
49MiB备份及外层租期，由watchdog冷复位，不重放未知token。182项Python、22项C、
两种EFI离线检查通过，实机结果待补。

v86 实机真正取得shutdown原始返回：64次均a0=1 (INTERRUPTED)，没有2/3 WAITQ。
最后a1=1、a2=1、a3=0、a6=0x6190008f；29664us达到64次限制，outcome4。
适配器返回EFI错误8000000000000007，未伪造成功sentinel；原生报告verified0，
restore-gate target_access0，49MiB备份/metadata/两路LPI/带宽均保留到watchdog。
没有auth/reset。Android恢复、boot ID变化及熄屏确认，电池34.9→35.3°C。
旧bounded-stage入口在host/dispatcher/capability禁用，不原样重跑。
该结果将“旧循环不返回”缩小为持续INTERRUPTED，而不是已观察到的WAITQ。

新的v87独立实验核对QMP应答的IPCC接收确认。固定同一Mu IPCCDxe
e91f717f5a3a3e73701324fc4d8476416712af21a8a569a05fce0bb4befb30c1的3120–329c
接收/确认顺序与2784/278c访问原语，2段SHA和原EFI逐项核对。marble DT 的QMP
interrupt/mbox均client0/signal0；APPS MMIO由原生配置确认ed18000/client8。
Linux qcom-ipcc.c与Mu ISR均先读RECV_ID(+10)，再向RECV_SIGNAL_CLEAR(+1c)写同一ID。

v87先读原接收ID，再完成已验证wire，之后只在原状态ffffffff、本轮观测0时确认一次0。
预先存在或外来通知一律不清；不启停IRQ，不读IAR/写GIC，不调用native回调，
不调用PAS/NPA/DSP。需读回ffffffff并保持固件上下文不变，10秒watchdog和冷复位保留。
这是待实机验证的接收通知假设，不将INTERRUPTED直接当作已证明的某个GIC中断号。
183项Python、22项C、两种EFI和77段native指令检查通过。

v87/v87b同镜像两轮实机通过：通信前RECV_ID=ffffffff，wire完成后观测0，
仅向CLEAR写一次0，读回ffffffff；原描述符/native heap/Clock/DAIF与跨命令
preservation均通过。没有改IRQ/GIC配置，也没有PAS/DSP；冷复位/熄屏通过。
电池分别34.4→34.9°C、35.0→36.0°C。只证明该通知与确认路径可用，尚未证明
它就是PAS持续INTERRUPTED的唯一原因。

v88候选将同一确认步骤放在ROM装载后的wire与有限PAS stop之间。原接收ID必须为空，
本轮ID必须是QMP0，确认后立即读空并再次验证固件状态；raw stop入口也要求确认通过
且当前仍无接收项。未执行DSP、未认证；旧v86入口保持禁用。184项Python、22项C、
两种EFI离线检查通过，待自然降温后进行组合实机测试。

外围增量恢复点v7已创建并逐成员校验：1303源码成员、11451证据成员、60个loader
headers，随后5个外层文件再次校验。包含v88候选源码/镜像，但创建时尚无v88-live，
也不含后续静默资格复核的修改；不能把恢复点内容当作后续实机结果。

v88实机：本轮own-QMP IPCC确认通过，PAS1 stop首次一条SMC返回a0=0/a1=0，
1649us；49MiB原内容恢复、57文件句柄关闭、带宽Complete/Destroy及两路LPI释放
均PASS1。与v86相同寄存器初始化/重试策略，实质增量是确认已处理的接收通知。
然而host严格报告因marble-smp2p-ipcc资格行出现两次而拒绝；跨命令preservation
未读，改由watchdog冷复位，整体仍记失败，不改原exercise.json。Android熄屏，
电池34.9→35.1°C。另存HARDWARE_STAGE_PASSED_HOST_REJECTED分类。

v89仅让IPCC独立资格复核使用无重复SMP2P日志的模式；所有原资格检查照常运行。
SMP2P原调用保持原日志行为，不放宽报告器的重复/缺失拒绝规则。需重新完整实机
检查报告、跨命令状态、明确冷复位与熄屏；不把v88的局部成功当全部外围或音频完成。

v89首轮完整通过：PAS_stop一条SMC、a0/a1均0、1909us；49MiB原内容及文件清理、
SCM带宽和LPI释放、host全报告、跨命令preservation、明确冷复位boot ID变化、
Android熄屏均通过。LPI外层3712265us，电池35.9→36.1°C。v88原失败分类不修改。
仍是无执行ROM验收，DSP_start=0；DSP运行后的GLink/SMP2P通知处理和音频未完成。
手机正在熄屏自然降温，之后用同一v89镜像进行第二轮复测。

下一阶段离线复用核对：Mu GLink SMEM transport数组在global d698，5项，每项0x170；
配置d2a8起每项0x38，lpass为index0/remote2，FIFO配置0x4000、IPCC client3/signal0。
初始化65e4–69a8的SMEM分配发生在固件启动期；运行期5b70获取remote FIFO走SMEM
get_ex槽+20，不是allocate_ex槽+18。native RX入口5c30会处理已有FIFO并可能创建本地
BS pool对象，不等同于许可在DSP运行时使用lockless SMEM allocator。后续仍需核对
RX descriptor/FIFO映射、注册监听者、所有分支与通知所有权，不能直接调用整个全局ISR。

v89b同镜像第二轮完整通过：PAS1 stop一条SMC、a0/a1=0、1737us；全部ROM/文件/
带宽/LPI恢复、跨命令preservation、明确冷复位与熄屏均通过，电池34.8→35.5°C。
v89镜像分类QUALIFIED_NOEXEC_IRQ_CLEANUP，限定为无执行验收；不是DSP运行后停机、
GPR/SPF、音频、SOC或全外围的完成。继续保留旧失败入口禁用与原失败证据。


## v90–v92：预分配 lpass 队列资格检查

v90 的两种 EFI 编译及本地检查通过，但打包时超出原内嵌配置盘容量；未生成可用
boot 镜像，也未启动手机。新增专用 --glink-only profile，裁剪当前不使用的
QMP/PAS/UI命令，保持原FAT布局；host在创建输出目录或操作手机前拒绝其它实验模式。
该profile不允许启动XNU，仍仅临时fastboot boot。新增两项profile拒绝测试，190项
Python测试与22项C检查通过。v90b boot SHA256
0dc015003a7eff5280d9a902b6debc324af1dbbeb67fed1f3bfb1690cbdaff6a。

v90b实机读到transport=fe7bc018、config=fe7702a8、core=fe7bcf18，descriptor=
80acf030/80acf038，local FIFO=80acf060/16384，remote FIFO=0，MTU4096，两个AP
cache索引0。后续检查拒绝，没有GET调用/队列写/IRQ写/PAS/DSP；Android恢复、熄屏，
电池32.9→33.6°C。v91只增加拒绝位置日志，再次匹配4段native代码及17个函数槽，
明确拒绝于core名称，未继续到SMEM GET。电池34.3→34.2°C，Android恢复且熄屏。
两个失败保持原结果，没有将读到队列指针等同于完整资格通过。

固定Mu GlinkDxe的0x6904指向字符串RVA0xb871，实际字节为大写SMEM；早期inventory
错误要求小写smem。v92修正为case-sensitive大写并同时要求精确原生字符串地址；
没有放宽transport/SMEM/队列所有权检查。报告器拒绝任何显式refused行，即使后面
伪造complete PASS也不能通过；host现在在命令失败后仍保存结构化解析。v92待实测。


v92首轮完整通过预分配队列资格检查：478=80acf030/32、479=80acf060/16384，480
为EFI_NOT_FOUND，私有分区80acf000/196608；所有页恒等NC映射，描述符两次稳定，
AP head/tail及native缓存0、native transport不变、仅一位既有Clock监听者。
无FIFO payload读取、SMEM分配、RX派发、队列/IRQ写、PAS或DSP执行。
Android恢复/熄屏，电池35.5→36.1°C；不是运行期RX或音频通过。

v93将已验证的QMP wire拆为open/finish阶段，旧cycle仍按相同顺序调用。新的独立
no-exec held profile在ROM加载后发送on并确认其独立IPCC通知，在on保持且接收为空时
调用有界PAS shutdown；明确成功后才off、close、确认第二次通知，再允许恢复49MiB
原内容。旧v89镜像不可变，旧失败DSP入口继续禁用。新增5项报告器测试拒绝早off、
停机失败、外来清除、重复/缺失证据等，196项Python与22项C、两种EFI通过。

v93普通owned-stage镜像仍因容量离线拒绝；新增 --held-only 独立编译profile仅保留
此生命周期入口，保持原FAT布局。v93b镜像SHA256
4b238743e06f916310628c3fb7eda197a49b6c7b0483e14cffc41d6e274461a5。
v93b实机完整通过：on阶段IPCC清除1，PAS1一次SMC返回a0/a1=0、1688us，随后off和
close完成后清除第二次QMP通知，wire完整preservation通过；原内存/文件/带宽/LPI
恢复、跨命令状态、明确冷复位boot ID变化和Android熄屏通过，电池33.9→34.6°C。
仅no-exec，DSP_start=0，不能称运行后停机/音频或全外围完成。历史exercise.json的
AP_wire_noexec_unwind_verified=true标签随后细分：held路径只以
load_state_held_through_shutdown_verified标记，不将其等同于旧的停机前unwind；
原实机记录不改。v93c第二轮结果待补。


v93c同一v93b镜像第二轮全部通过：on保持→PAS成功→off/close→两次分别归属的接收
确认→原内存及资源恢复，跨命令preservation、明确冷复位及熄屏均通过，电池35.3→36.0°C。

v94增加独立控制链路候选：用固定Xiaomi qcom_glink_native.c/.h 和已固定HakiOS
控制消息算法，移植有限的ring wrap/framing/version协商。RX仅接受control，远端OPEN
记录并保持未答复，不创建本地应用channel、不发布intent、不发送GPR或音频。
共有4096/等边界、65536个opcode、所有1KiB ring head/tail及copy长度、open截断/
NUL/长度溢出检查；新的C测试通过。原生dispatcher和运行期SMEM allocator不调用。
源文件及SHA在out/glink-runtime-v94-audit/references.json。

运行前复核预分配lpass/SMEM，运行时只GET新480，逐页NC/边界/非重叠检查；
自有RX tail/TX head必须保持一致，native transport46个word必须未变。只在GLink
control队列处理完、或SMP2P稳定头处理完后确认对应30000/30002通知；外来ID拒绝。
新入口把load_state on保持至新的ready/stop-ack和有界PAS成功后再off。raw PAS
必须本轮stop已确认、队列已空、IPCC无待处理项；失败保留所有资源和watchdog。
旧失败DSP入口继续禁用，不复用其镜像。204项Python、23项C及两种EFI编译通过。

v94最初EFI122880字节超容量，离线拒绝，没有启动手机。v94b专用control profile裁剪
无关历史/UI能力查询后EFI119808字节，保持原FAT几何和103个其它固件文件不变。
镜像SHA256 66d8400624b7fbbf3eecac552e69838e900a3877f2a8f26efb3ce1ede993dd70。
这是待测候选，不是DSP停机或音频成功证据；控制器也要求实际协议、停机/恢复、
明确冷复位和Android熄屏才可通过。实机结果待补。


v94b实机部分进展、整轮失败：PAS auth EFI0/transport1/secure0，114266us；新480 FIFO
80ad3340/16384读到，实际完成VERSION(features7)→ACK(features1)协商，AP TX head16，
RX tail144。远端依次发布pcie_drv、IPCRTR、adsp_apps、fastrpcglink-apps-dsp、
LOOPBACK_CTL_LPASS五个OPEN，全部记录且未答复，没有应用channel或GPR payload。
ready=6、新stop-ack=10e均通过，hold500461us、芯片41.2°C。
但PAS stop在251852us/26次达到限制，全部a0=1、a1=1、a2=1，最后a6=6190006e。
没有观测WAITQ2/3，未进入off、payload恢复或带宽/LPI释放；由原看门狗独立回Android，
boot ID变化与熄屏确认，电池32.9→33.8°C。不能宣称DSP停机或全链路通过。
原始构建清单/证据不改，独立hardware-status.json标为FAILED，并由host的提前拒绝
及通用boot检查阻止无变化重试。

v95改变的是有界SMC重入窗口：每次原始返回的IPCC before/after和处理结果保存到
静态RAM，退出TPL8后再写日志；重入期间不发送USB INFO。只有确认为本轮30000/
30002通知时才调用已取得固定队列的control/SMP2P处理，不运行SMEM allocator或native
全局dispatcher，不清其它ID；其它secure返回仍按原规则拒绝，64次/250ms上限不变。
此举排除诊断流量影响并处理停止期间新到通知，但没有把它当作v94b故障的已证根因。
候选EFI118784字节、两种EFI/23项C与205项Python检查通过；新capability v2，旧v94b
镜像已禁用。v95镜像SHA256
9eb539bd885482c29863954218ad49288d0ab23ba9e605717112b0f8fc20c867。
新实机结果待补。


v95实机仍失败：control版本协商及新ready/stop-ack通过；PAS1在250351us/26次达到
上限，最后a0/a1/a2=1、a3=0、a6=61900074。26个中断返回样本的IPCC before/after
全部ffffffff，owned_service_calls=0，重入窗口USB_progress_calls=0。因此不能把此轮
持续INTERRUPTED归因于仍有未处理的可见IPCC队列，也不能把去掉日志当作修复。
未发off、未恢复目标内存或释放不确定资源，原看门狗回Android，boot ID变化及熄屏
确认，电池33.5→34.4°C。独立hardware-status.json标FAILED，原构建和实机记录不改。

v96增加只读GIC观察：地址和stride以同一固定marble.dts为准，GICD17100000/10000、
GICR17180000/200000、stride40000，按MPIDR匹配本核redistributor；IPCC是SPI229，
实际INTID261。读取PIDR2/TYPER、NS视角enable/pending/active，不读IAR，不写GIC，
不修改IRQ配置/优先级。记录启动前、PAS前、首次INTERRUPTED和最终原始返回，全部
延后到TPL恢复后输出；原有64次/250ms上限不变。它仅用于定位，不是清除/修复实验。
两种EFI和23项C编译检查通过，镜像119296字节、原几何不变；新capability v3。
SHA256 3e84a3326b31042ab2424cbf94ccae8169787374a8130d439f8766032d5ff1cb。
v96待实机结果。


v96实机只读GIC记录有效，停机仍失败。GICD architecture3/banks32、本核GICR17180000、
MPIDR81000000。四个阶段PPI pending/active均0；32个bank的NS可见SPI也未见pending/
active，IPCC bank8 enable20、pending/active0。诊断解析单独PASS，IRQ_writes=0、
IAR_reads=0；只能说明观察时NS可见状态，不排除secure IRQ或secure调度等待。
PAS在252682us/26次到旧限制，a0/a1/a2=1、a6=6190007b；无off/载荷恢复/供电释放。
看门狗返回Android，boot ID变化和熄屏确认，电池33.9→34.7°C。完整实验仍失败，
原构建保持，hardware-status.json单独禁用。寄存器定义另固定同一Xiaomi commit的
include/linux/irqchip/arm-gic-v3.h，见out/irq-observer-v96-audit。

v97是单独的运行后停机预算实验。Linux的INTERRUPTED循环本身无此250ms门限，而
v95/v96每次约10ms返回且26次稳定到250ms；因此验证一次128次/1秒有界窗口。
只有已经认证、取得本轮fresh stop-ack、control队列空/IPCC空且GIC观察器合格的运行
路径使用新门限；所有旧查询和no-exec仍保留64次/250ms默认值，WAITQ2/3仍不重放。
运行窗口每4次调用直接读取TSENS，55°C或读失败立即退出；没有升高CPU/GPU频率或
电压。外层10秒watchdog、15秒供电租期上限和失败保留资源策略均保留。

v97原candidate120320字节超容量，离线拒绝，无手机操作。v97b将独立control镜像
的能力接口缩至此profile真正使用的RAM传输/状态/恢复查询，普通及generic不变；
EFI119296字节，原FAT布局和其它103个固件文件不变。两种EFI/23项C和208项Python
检查通过；v4 capability，SHA256
7d7912e9d521b0e8968d89208e2af2a0abaf7baeee332712856356f3d8132325。
运行结果待补，未宣布预算延长已解决停机。


v97b硬件阶段首次成功，但主机判失败：PAS在608991us/44次（43次INTERRUPTED）后
返回a0/a1=0；11个TSENS样本全部合格，最高43.2°C。control VERSION/ACK、五个
远端OPEN、GLink通知2次处理完成；SMP2P以新ready/stop-ack从SMEM轮询取得，手动
SMP2P清除次数为0。随后off/close、两次QMP通知确认、49MiB原内存恢复、文件平衡、
带宽Complete/Destroy及LPI释放均PASS1。然而报告器错误要求SMP2P手动clear>=1，
原exercise保持失败，没有跨命令状态和明确reset完成证明；看门狗回Android并熄屏，
电池34.1→34.9°C，独立标记HARDWARE_STAGE_PASSED_HOST_REJECTED。

修正报告器的计数假设：不允许为了满足计数去清不存在的通知；SMP2P手动clear可以
为0。fresh ready/stop-ack、队列/IPCC空的raw停机门、完整PAS返回和原有恢复检查
照常保留，并额外要求完整只读GIC证据。加入zero-clear正例和缺失stop-ack/停止门负例，
不接受伪造通知投递证明。原v97b的原始记录不改，另存hardware-stage-reassessment。

v97c同一v97b镜像，修正主机后首轮完整通过：DSP auth、GLink control协商、新
SMP2P ready/stop-ack、有界PAS停机和全部RAM/文件/带宽/LPI恢复通过；跨命令
QMP preservation通过、明确冷复位boot ID变化通过、Android熄屏通过，电池35.2→35.8°C。
host-source/及host-sources.json保存本轮126个实际Python源码，防止将旧构建归档中的
报告器等同于本轮运行版本。仍无本地业务channel、GPR/SPF、音频或全外围验收。

v98仅在独立control profile裁剪无关desktop命令和单独SMP2P prepare/stats入口，
为后续GPR消息腾空间；正常loader/generic保持这些命令。本候选尚待构建/实机复核，
不修改上述已验证v97b镜像。


v97d同一v97b镜像第二次完整通过：PAS 608753us/44次，11次温度采样最高44.7°C；
control/新ready-stop-ack/PAS/全部资源恢复、跨命令QMP preservation、明确冷复位和
Android熄屏均通过，电池35.9→36.1°C。v97c/d两轮均有独立126个host Python源码归档。
这证明有界DSP控制链路和运行后停机/恢复可用，不证明audio、GPR或SPF业务已通。
v98两种EFI/23项C编译检查通过，control专用EFI缩至116224字节，尚未实机测试。


外围增量恢复点v9已创建并逐成员校验：1318源码成员、14432证据成员、66个loader
headers；外层5个文件再次校验通过。包含v97b/c/d历史与两个完整DSP控制测试、
v98离线裁剪候选，依赖不可变UI v8及本机工具链；不包含其后新的adsp_apps修改。

v99新增独立 --apps-only/--adsp-apps-open 元数据诊断。仅在已协商的lpass上确认
远端实际命名adsp_apps的OPEN，并以本地channel1发OPEN；允许此独立profile接收
OPEN_ACK和最多64条remote INTENT元数据。原control-only继续拒绝这些命令。
不发布任何local INTENT、不收发DATA或GPR；remote intents不代表音频可用。
DSP经原有fresh stop/PAS停机后使本地状态失效，不伪称完成了GLINK CLOSE握手。

RX改为先复制8字节命令头，验证种类/长度后再只复制所需控制元数据；不得为判断
控制帧而多读后面未知DATA的body。长度/部分帧/重复intent ID/绑定错误/超界均拒绝；
固定静态表，无运行期SMEM或BS对象分配。native transport保持不变。216项Python、
23项C和两种EFI编译通过，v99 EFI118272字节。实机尚未进行。

v99b补上Linux receiver允许的uint32 remote intent ID 0：此字段是协议ID，并非
空指针；静态表按count管理，不用id0作空槽。仍拒绝重复ID和零/越界size。v99b
正在重建，未把元数据握手当GPR业务成功。另固定同一audio-kernel commit的
spf-core.c/h与audio_prm.c/h，out/spf-core-v99-audit/references.json；下一阶段可复用
read-only APM_CMD_GET_SPF_STATE(01001021)/RSP02001007，不能照搬旧APR音频命令。


v99b实机取得adsp_apps双向OPEN/ACK，remote channel3/local1；远端ID0..63的64项
声明均为128字节，随后更多INTENT触发固定表容量拒绝。没有GPR/local INTENT/DATA。
停止条件未合格，未调PAS shutdown、未off或恢复未知内存/释放供电。原watchdog回
Android，boot ID变化、熄屏确认，电池33.4→34.0°C。原记录失败，镜像单独标FAILED。
日志thermal_ok=0在此版本复用了runtime错误标志，实际记录die42°C、不是温控越限。
后续已把runtime_ok与thermal_ok分开，协议失败不能再错误表示为发热失败。

v100将有界静态remote INTENT表扩至512项（单帧仍最多64项），使用生产纯函数验证
容量/重复ID/size/ID0及拒绝时不越界；端点总消息限2048，每次最多128帧，批次未完
只延后处理、不误当作fatal或提前清空通知。停机前最多16批确认队列和接收通知为空，
仍不分配SMEM/BS对象，不广告local INTENT，不收发DATA或GPR。216项Python、23项C
及两种EFI通过；EFI118272字节，原布局不变。
镜像SHA256 07b0dfed2893fd0f2b4cd61488745fa8bfe26a6a3931dab78b45d9d46c44e23c。
实机结果待补。纯GPRv0六字头/只读SPF状态命令和回复解析另有离线C测试通过，但
未编进此镜像、未发送给DSP，不能将其计为硬件GPR/SPF通过。


v100/v100b同镜像两轮完整通过：adsp_apps remote3/local1双向OPEN/ACK、116项远端INTENT元数据；无local INTENT/DATA/GPR。PAS约609ms/44次后成功，全部RAM/文件/带宽/LPI恢复、跨命令QMP preservation、明确冷复位boot ID变化和Android熄屏通过。电池33.8→34.7及34.5→35.1°C。不是音频验收。v100b补存当前126个host Python源码，控制器执行期间未编辑这些文件。


2026-10-30，v101是独立GPR-only profile（qmp510），旧control/apps契约保持拒绝DATA。
从固定MiCode GLINK/GPR/SPF实现复用六字头、READ-ONLY GET_SPF_STATE、DATA/CONT及
RX_DONE_REUSE：固定512B local intent1，实际remote INTENT选择与ID0合法性检查；仅发
一个24B查询/token4d535001，累计≤512B、fragment remaining严格单调、channel/intent/
domain/port/token/opcode绑定。未发送graph、播放、时钟、功放或校准命令。

v101首次EFI120320超原FAT容量，打包拒绝，没有手机启动。v101b只对新GPR profile裁剪
已完成定位的只读GIC诊断（显式omitted契约）、重复SMEM observed/progress输出；所有
实际SMEM资格、native unchanged、fresh stop/PAS/IPCC-empty及温控/恢复检查保留。
原control/apps仍保留完整GIC检查。EFI118784，原FAT布局和其它103固件文件不变。
224项Python、25项C及两种EFI编译通过，构建前后compiler inputs无变更。
Boot SHA256：19dd5984fd56d272df1aa2854b4f66a546205509bd95b17d08f4ea0bda45f600。

v101b/v101c同一镜像两轮实机完整通过：实际remote channel3/local1，116项INTENT中
选择remote_id0/128B；单次24B GET发出，remote RX_DONE_REUSE14/ID0确认；收到DATA9
28B GPR回复，唯一token和所有header字段验证后SPF state1=ready；local RX_DONE_REUSE
14/ID1发出。500ms观察、fresh stop/PAS约609ms/44次、全部RAM/文件/带宽/LPI恢复、
跨命令preservation、明确冷复位和Android熄屏通过。电池29.1→30.0及30.0→31.1°C。
每轮自动保存当次host Python源码和hash。SPF ready不等于实际音频输出，不宣称外围完成。

v102 Android只读硬件参考已保存，电池32.0°C，屏幕OFF。实机声卡为ukee-mtp-snd-card，
ro.boot.product.vendor.sku=ukee；I2C bus2的34/35地址均绑定aw882xx_smartpa。因此不能
仅因SoC名称就选ROM中sku_diwali的通用speaker路径；接下来核对实际ukee mixer overlay、
TDM路由、DT与功放固件/校准来源。未写mixer/I2C/GPIO/功放，也未访问Android userdata。
