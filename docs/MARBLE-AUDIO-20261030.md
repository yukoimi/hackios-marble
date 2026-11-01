# marble 音频适配：GPR/SPF 与实际扬声器链路

当前未完成音频输出。所有手机实验仅临时 fastboot boot、RAM 包和冷复位；不访问 Android /data。

两轮同一 v101b 镜像的只读 GPR GET_SPF_STATE 均取得匹配 token/域/端口/操作码的 28B
state1=ready 回复，remote intent0/128B 的 TX_DONE 和 local intent1/512B 的 RX_DONE_REUSE
验证通过。fresh stop-ack、PAS约609ms/44次、全部资源恢复、冷复位和 Android 熄屏通过。
电池分别29.1→30.0、30.0→31.1°C。该结果不能当作播放或扬声器已工作。

源码与两轮证据已保存在 checkpoint-peripheral-spf-v10-20261030；1326源码成员、16473证据
成员、68个loader headers，逐成员/外层5文件校验通过。依赖不可变 UI v8及本机外部工具链。
此恢复点不包含后续 v102–v106 I2C/功放工作，不能把当前工作目录等同于恢复点内容。

实际 Android 声卡 ukee-mtp-snd-card、vendor sku=ukee。Linux bus2 两颗 aw882xx_smartpa 位于
34/35，控制器 DT 为880000/4000。四份 ukee mixer/resource 配置及2672B aw882xx_acf.bin
与用户 ROM 提取物逐字节相同。固件 SHA256：
77fa69a4915cf493300b63bb7911cd3cc7061fbc4c9319626c520f67556551d5。

ACF 表为 AW88263S/2032，两个声道、各 Music/Voice/Receiver 的44条寄存器及独立monitor配置；
不能只根据 XML 中aw88261注释选择芯片。当前只是固件候选硅片ID，尚无物理chip-ID验证。
离线解析检查整体表/区段边界与覆盖、CRC8、嵌套校验和、长度/寄存器范围、监控表边界及
设备/声道/档位绑定。上游24个驱动源码文件固定MiCode audio-kernel 7d174adf，Git blob SHA1
及SHA256均核对，见 out/audio-route-v103-plan/aw882xx-sources.json。

扬声器配置链路：TDM-LPAIF-RX-TERTIARY-VIRT-0、双声道96kHz/32bit、speaker protection=1。
static mixer overlay会替换common的WSA speaker路径；两套开关不得简单叠加。
板级材料记录于 out/audio-route-v103-plan/board-contract.json，physical_chip_id_verified和
hardware_writes/audio_acceptance明确保持false/0，未写mixer/I2C/GPIO/功放，也未播放声音。

v104 I2C-only镜像 EFI18432B，实机匹配Mu I2C protocol revision10000/5槽及完整2196B方法代码。
接口表和client head不变；无Open/Read/Write/Transfer/Close。冷复位与熄屏通过，电池30.1→31.0°C。
v105 EFI19968B，只读记录DT provider33词及两个方法前32B，实际provider由EnvDxe转接到
a70149f8/a702620c；接口表与cache不变，DT_queries=I2C_calls=0，冷复位/熄屏通过，电池30.7→31.4°C。

这两个实际方法在用户ROM images/uefi.img 中精确找到；该ELF标注ARM40而指令实际AArch64，
分析副本只改e_machine为183用于反汇编，原文件保持不变，不会加载分析副本。
原厂xbl_config.img含两份FDT；UEFI配置41865B中只有I2C实例2和6，没有880000的功放控制器。
这先作为离线缺口，不直接对EFI Instance16进行试探。v106候选将只读取经完整入口代码固定的
DT全局指针，要求完整RAM副本的SHA256匹配ROM，才可确认运行环境也存在同一配置缺口。
它不调用DT或I2C方法，不改原DTB/global pointer/时钟/GPIO；实机结果待补。

控制器还修复discovery提前return绕过最终恢复验收的主机流程：用独立完成异常经过finally后
继续执行最终验收。完整主机模拟确认未冷复位、回程电池≥38°C或证据损坏均非零退出，
模拟命令不存在flash/erase/userdata访问。历史记录保留，当前已通过的实机恢复证据没有改变。

后续仍需：EFI控制器映射及完整资源所有权/释放、实际功放ID、TDM pin/clock及SPF graph，
扬声器保护/校准、XNU/iOS播放接口及实际声音验收。网络/蓝牙/传感器/SOC/硬件休眠与
真正IRQ/pageflip垂直同步和光学稳定60FPS也未完成，不能宣称全外围完工。


v106严格ROM整字节匹配实验被正确拒绝：实际DT指针80602800，header41865B与ROM相同，
完整快照哈希不同。没有DT/I2C调用或硬件写入，明确冷复位/Android熄屏通过，电池30.9→31.1°C。
镜像已标FAILED_RUNTIME_CONFIG_HASH，禁止原样重试。原版本pointer_unchanged=0由短路表达式
跳过末尾读取导致，不能当作指针发生变化的证据；后续修正为独立读取并记录pointer_read。

v107为不同目的的有界capture-only实验，不降低原v106的ROM验收要求。仍固定同一ROM
两个完整入口代码和全局指针地址，仅复制同长41865B、恒等映射RAM到AP自有静态缓冲。
无原DTB/global pointer写入，不调用DT方法，不取得控制器句柄。完整副本返回主机后由
有边界的FDT v17解析器检查headers、reservations、node/property/token/strings边界、
重复节点和属性及END；保留原始字节和逐属性差异。ROM_configuration_verified保持false，
需要完整语义比较后才能判断配置缺口。

v107两轮实机捕获成功，282节点均通过严格解析。与ROM只差
`/soc/iort/NAMEDNODE_UFS_95MEM:CacheCoherency`（ROM0、运行值1）；整个I²C子树相同，
确认只有2/6，缺少880000。保留运行值，禁止拿原ROM整个DTB替换共享运行配置。
两轮明确冷复位/熄屏通过，电池31.5→32.3、32.6→33.1°C。
证据为daily-i2c-capture-v107-live及v107b-live，独立子树审计在
out/i2c-controller-v105-audit/runtime-I2C-subtree-audit.json。

v108实机通过五个公开GetClockID查询，ClockDxe revision10009/table RVA1b0f8与68B入口
代码匹配。五个名称及实测固件ID依次为：
gcc_qupv3_wrap2_core_clk=10119、gcc_qupv3_wrap2_core_2x_clk=10118、
gcc_qupv3_wrap_2_m_ahb_clk=100ca、gcc_qupv3_wrap_2_s_ahb_clk=100e9、
gcc_qupv3_wrap2_s0_clk=1011a。接口表与I²C client head不变，没有时钟启停/变频、
I²C/GPIO调用。88B候选配置仅验证布局和板级几何，未交给硬件函数。
daily-i2c-clock-v108-live明确冷复位/熄屏通过，电池31.4→32.1°C。
镜像SHA256=c5f2a31958bacd3fd4ebbb8ba9cc83b5e8d7d97b11266b54c2a8de7cfbccd84f。

v109新增独立五时钟引用生命周期，使用公开Clock接口，不调用native I²C init/Open。
每个引用成功后才登记所有权，逆序释放；失败的enable保留uncertain并禁止复用，不能
盲目disable误减其他所有者的引用。检查启用前后enabled/on及频率、接口表和client head。
全部32种初始所有权组合、逐次接口调用故障、时钟不启动、频率漂移和逆序释放由C模拟验证。
v109实机在首个频率元数据查询返回EFI_NOT_STARTED/零后停止，未启停任何时钟。
精确反汇编4458..44e8表明：缺少active frequency-plan指针时返回-8并写零；不能把零
当作测量频率。v110把此状态保留为不可查询哨兵，但旧generic诊断40°C芯片门限在40.0°C
拒绝了实验。两个失败镜像及原日志保持失败标记，均冷复位/熄屏通过。

v111独立1ms外围lease使用既有芯片启动<50°C/结束<55°C门限；电池预检仍<38°C，
不改变GPU或其他实验门限。实际取得前四项引用，S AHB enabled=1而on=0，正确逆序
释放并恢复全部五项初值；未触及SE或控制器。失败镜像不原样重试。

v112将“引用所有权验收”与“物理访问就绪”明确分开：五项都须enabled=1；on状态仅记录，
register_access_qualified=0。固定waipio源码中S AHB有HWCG，Mu固件节点的CBCR与vote
寄存器也精确对应，见clock-board-static.json。这不等同于控制器可访问。
实机五次Enable/五次逆序Disable全部成功，初始/最终enabled/on与频率元数据状态一致，
表/head不变，held/uncertain/active/dirty全零；明确冷复位/熄屏通过，电池33.5→34.0°C。
证据daily-i2c-clock-v112-live。没有控制器MMIO、I²C/GPIO或频率请求。

v113新增独立八地址RO快照：先检查所有地址的恒等翻译，取得五项引用后对wrapper版本、
GENI状态/只读clock/fifo设置、主/次固件版本与FIFO几何各读两次。地址来自固定Xiaomi
msm-geni-se.h，严格排除FIFO数据/IRQ清除/命令/引脚寄存器。快照成功也不代表I²C事务
或功放ID通过；缺失I²C协议时只标snapshot captured，不标identity qualified。
v113实机通过：wrapper版本30080000，GENI_STATUS=0，clock-control=1，FIFO-disable=0，
主/次固件版本303（协议3=I²C），TX/RX参数201029e4/20104800（32位、深度16）。
两份快照完全一致。所有恒等翻译在读寄存器前通过，五时钟引用均释放，原状态恢复，
冷复位与熄屏通过，电池33.3→33.3°C。证据daily-i2c-registers-v113-live。
镜像SHA256=6d0abdd91410bcc594c82e6979cd251a6437109afb9f07a2ea6f266ab604eca5。
这证明已有GENI I²C固件和可读的FIFO接口，尚未发任何I²C事务、复位功放或输出声音。

v114在同一五时钟引用下捕获41词GENI/GCC/TLMM被动快照，两份一致（GPIO输入位仅记录，
保留输出锁存位）。RCGR cfg100000/M=N=D=0；S AHB CBCR80000002确实有HWCG bit1且
处于halted，本地vote1f。SCL/SDA GPIO56/57 cfg=1、输入为高；两颗功放reset GPIO1/120
cfg=1、io=0；speaker switch GPIO52亦保持原cfg=1/io=0。控制器DMA=0、FIFO空、
主/次命令和IRQ状态为0，尚未设置I²C pinmux。冷复位/熄屏通过，电池32.7→33.4°C。

v115首次只配置/恢复总线，没有命令和功放复位。采用固定Linux I²C 100kHz timing：
serial divider71、SCL counters a02c1a，保持RCGR/PLL不变；两引脚cfg=7（function1、
pull-up、2mA、输出禁用），恢复为原cfg1。关闭该SE的IRQ forwarding用于轮询，恢复原f。
RX packing恢复前从7f807临时改为8-bit MSB-first 7f8fe，TX原packing匹配而不重复写。
13个候选词里实际仅6项改变，共12次写入/恢复；读回IOS=7，SCL/SDA均高，STATUS=0。
全部配置和41词受保护状态恢复，未改reset GPIO/功放/时钟源；五时钟引用均释放。
daily-i2c-setup-v115-live冷复位/熄屏通过，电池32.2→32.3°C。
镜像SHA256=6bbb97a20d309cbb10dd47c411675fc40c865eb95f6ebd144c53c925e17c80d2。

v116为独立ID候选，尚未实机通过。只允许地址34/35、寄存器00的两字节读取：
固定Linux的WRITE+STOP_STRETCH后READ，TX FIFO只允许零字节寄存器指针，不允许
功放数据写入。每命令20ms/4000轮双重超时，中止10ms/2000轮；失败不自动重试。
按固定aw882xx.c的reset低1ms/高2ms生命周期读取后立即重新拉低，再恢复GPIO cfg。
默认SYSCTRL的AMPPD/PWDN均为power-down，且speaker switch52始终不改、不装载ACF。
成功也必须匹配真实chip-ID2032，空FIFO/无活动命令/IRQ清理及辅助寄存器恢复。
CMD0属于不可恢复的命令历史，不通过写旧命令来“恢复”；该词只记录，其他配置/状态
仍精确检查。中止或恢复失败标dirty，禁止关闭运行中SE的时钟，转明确冷复位。
FIFO纯C测试覆盖两个固定地址、NACK/超时/中止失败/无效FIFO长度及逐次接口故障；
这不能取代实际chip-ID和音频播放验收。

v116实机失败：提前启用TX watermark造成空闲M_TX_FIFO_WATERMARK事件，FIFO前置检查
拒绝，commands=0。功放复位还原，但辅助配置/clock留dirty后明确冷复位；电池31.7→32.0°C，
Android熄屏确认。镜像标FAILED_IDLE_TX_WATERMARK_PREFLIGHT，禁止原样重试。

v117按Linux顺序改为先WRITE命令后开水位、写完指针立即关水位。地址34真实返回
raw_rx3220、chip_id2032，耗时600us；但S_IRQ_STATUS=c000000（RX watermark/last）
未清，M_RX汇总80反复出现，中止后仍不能证明空闲，故整个实验失败、地址35未尝试。
功放reset还原，配置和时钟留dirty并明确冷复位，电池32.9→33.5°C，Android熄屏确认。
镜像标FAILED_SECONDARY_RX_EVENT_CLEANUP，不能把收到ID倒推清理通过。

v118候选只清零初始S_IRQ下、本次唯一RX产生且已消费FIFO数据对应的c000000事件，
任何其他S事件均拒绝；没有blind clear-all。主事件也只ack实际观察到的已知位。
模型补入空闲TX水位触发、次级RX事件锁存和M_RX汇总重现，旧失败场景保留。
仍需实机完整两颗ID/恢复/冷复位验收。

v118/v119也保留失败：都实际读到地址34的2032，owned S事件清零后raw M_RX=80仍存在。
v119采用Mu完整init反汇编4060..407c的RX watermark13/RFR14，S事件只剩RX_LAST8000000，
但没有清除raw M_RX汇总；不能称调整水位已解决该汇总。两版均恢复reset后明确冷复位、
Android熄屏；电池分别33.5→34.0、33.9→34.0°C。原失败状态/日志不改为成功。

v120为不同的逻辑空闲合同：GENI活动位须0、TX/RX FIFO空、S_IRQ=0，M_IRQ只容许0或80，
且M_IRQ & M_IRQ_ENABLE必须0。唯独bit7（M_RX）可作被屏蔽的raw指示；任意其他raw位或
bit7被使能均拒绝。Mu 3e10..3fd0及固定Linux IRQ处理按完成/错误/FIFO位推进，没有把
raw M_RX读零当作事务结束条件。该解释是基于代码和本机状态的推断，不是整个raw字还原。
记录idle/post raw IRQ与enable，raw_IRQ_restored按真实读回标0/1；强制cold_reset_required=1。
CMD0最后命令历史和被屏蔽的bit7只记录，其他配置与状态仍精确恢复；不能用这一例外掩盖
任何可使能的pending事件。模型覆盖“masked80允许、enabled80拒绝”，报告器拒绝虚报raw还原。
此候选尚待实机，绝不把逻辑空闲合同倒套为旧v117–v119的成功验收。

v120第一轮实机完整通过新合同：地址34/35均raw3220/chip2032，分别606/605us，
每颗只发两个固定命令（寄存器00指针+两字节READ），不写寄存器数据；无中止/重试。
S_IRQ只消费本次RX_LAST8000000；空FIFO、无活动命令、无使能中断事件。每颗post_IRQ80、
post_IRQ_enable33c0007e，raw_IRQ_restored=0、cold_reset_required=1真实保留。
两reset GPIO回原cfg1/io0，6辅助寄存器/13词总线配置恢复，GPIO52不变，五时钟引用释放。
CMD0历史和masked bit7外的全部受保护状态匹配；没有宣称raw IRQ逐位恢复。
daily-i2c-amp-id-v120-live明确冷复位/Android熄屏通过，电池33.9→34.5°C。
镜像SHA256=fd010512a5c02b87db3b9bc7015160febd99b3d02b6c1ac329fbef3298dfea23。
第二轮同镜像daily-i2c-amp-id-v120b-live也完整通过，同样两颗2032、无中止，
原配置/时钟引用释放、raw masked80记录后明确冷复位/熄屏，电池35.1→35.5°C。
这两轮只证明I²C控制器/两颗AW88263S物理身份与有界生命周期，音频输出/保护/TDM/SPF graph
仍未验收。主机在实验后让Android保持熄屏，继续离线协议/板级资料核对。


v122板级TDM审计：只读Android runtime FDT 895307B，SHA256
0973dd78214906911189bb3cb8fcab718f3a08aa9d0eca8df763b05347cea973。
4184节点使用显式8192节点/1MiB限制；通用FDT解析器默认4096节点/65536B未放宽。
tertiary TDM GPIO121/123/122/124分别sck/ws/data0/data1，mux1；没有修改这些脚。
DT sync_mode=1、invert_sync=1、data_delay=1、internal_clk=1、group_port9020/group9120。
DT tdm-max-slots=8不是实际使用8槽的证据。
从Android只读提取machine_dlkm.ko，与用户ROM VM中保留提取文件逐字节相同，
两者SHA256=11aff0d910e2f62cf5123f85cb1467f7d167818c677d60c012c99ee209993a90。
反汇编msm_common_snd_init 147c..14a8把不在1..4的槽数限制为4。
msm_common_snd_hw_params 780..790根据smartpa类型选择16/32位slot_width，
8c8从pdata+1c0读槽数、8e4..8ec用rate*slot_width*slots计算clock。
公开旧机器源码缺失smartpa条件，不直接当作本机32位wire证据；后续需核实类型返回。

v123 AMDB版本查询：独立固件96B GET_CFG，单模块0700100e/param080013d6。
首轮收到32B basic error却因GPR client_data非零严格拒绝，runtime服务终止，
没有正常stop/PAS shutdown；保留全部依赖等待看门狗冷复位。
电池31.5→32.3°C，Android重启ID变化/熄屏，硬件状态明确FAILED，不重复原镜像。
v124增加仅完整owned packet的错误退役：记录前8词，发送RX_DONE、保持控制服务，
不接受元数据。实机完整fresh stop ACK/PAS停止/依赖释放/明确冷复位/熄屏通过，
电池33.1→33.8°C。原始回复：2060,a80203,3,3,4d414d01,2001005,1001007,2。
client_data=a8含保留高4位，不合官方规范；error2按官方ar_osal_error.h为BADPARAM，
不是UNSUPPORTED(3)。不能据此断言模块不存在或固件绝对不支持该接口。
v125只对nonzero basic error保留非零client_data兼容；严格检查域/顶保留字节/
端口/token/原opcode/长度，非零client_data不能进入成功metadata路径。
实机得到相同BADPARAM2/client_dataa8，header_conformant=false、metadata_exchange=false、
module_available=false（未证明可用，不等于证明不存在）、audio_acceptance=false。
控制生命周期/明确冷复位/Android熄屏通过，电池33.6→34.0°C。
AMDB可内部检查库；本轮无graph/register/load/unload/时钟/TDM/功放寄存器数据写。
新codec、报告器、独立CLI及失败/冷复位模拟通过；marble/generic EFI构建和32项C检查通过。


v126b单模块library acquire/release：64B AMDB LOAD01001020，48B reply02001008
成功返回0700100e句柄b0d7f42c/error0；确认TX_DONE后，用独立remote intent1发送
64B UNLOAD01001022，原样带回同一opaque句柄，不作地址访问。
卸载32B basic回复因client_data残留被旧解析器拒绝；owned GLINK仍退役回复并保持控制服务。
PAS stop/依赖释放/明确冷复位/Android熄屏全部通过，电池32.2→33.0°C。
此轮硬件状态保留FAILED_UNLOAD_REPLY_DECODING_RECOVERY_VERIFIED，不冒充协议卸载验收。
v127只把basic reply的client_data当不参与路由的opaque兼容字段记录，所有域/保留顶字节/
端口/token/opcode/原command/status/长度仍逐项校验，48B返回句柄的LOAD reply仍要求client0。
第一轮卸载basic成功error0/client58，第二轮也完成相同module/opaque句柄的acquire/release。
两个实验daily-adsp-amdb-load-v127-live与v127b-live均明确fresh STOP ACK、PAS停止、
ROM/资源依赖释放/冷复位ID变化/熄屏，电池分别33.6→34.0、34.7→35.0°C。
header_conformant=false如实保留；不把legacy client-byte兼容说成完全符合GPR规范。
两轮证明已注册TDM sink0700100e可获得并释放library句柄；不证明CAPI实例/音频图/TDM或播放。
没有REGISTER/DEREGISTER、GRAPH、PREPARE/START、PRM clock或功放寄存器数据操作。
实际固件状态机C模型覆盖：reserve第二个独立intent、等待TX_DONE、不复用未知句柄、
partial load error仍带handle需释放、malformed完整回复退役后保留stop service、拒绝重试。
最终独立EFI原始FAT布局不变；删除循环原始dump后119808B，成功打包；34项C与283项Python通过。
ROM/runtime TDM合同已可由tools/marble-tdm-board.py重建，核对实际模块指令和FDT/ACF；
aw882xx类型1及默认0使用32-bit slots，fs19xx类型2使用16-bit，尚需live类型交叉验证。
实际clock表tertiary ID204、attr4（不套DT attribute1），候选96k*32*4=12.288MHz尚未请求。


v128b：296B GRAPH_OPEN只含一个RX/playback subgraph4001、一个endpoint-capability
container5001和已注册TDM sink实例6001；一入零出、零连接。
按固定Linux/AudioReach APM结构8字节对齐，没有PREPARE/START/SET_CFG、共享映射、
PCM buffer、PRM clock、TDM引脚或功放命令。图内CAPI初始化的DSP内部行为不宣称零副作用。
首次只选可容296B的remote intent40/512B，64B GRAPH_CLOSE用独立intent0/128B，
关闭唯一自有subgraph4001，从不发CLOSE_ALL。两回复error0、client60，兼容字段如实记录。
完整TX/RX DONE、fresh STOP ACK、PAS停止、依赖释放/明确冷复位/Android熄屏通过，
电池32.3→33.1°C。镜像SHA256=f8e6bc3844b83f5c4ac54c124282969f94e867781496dca730852626120c8073。
289项Python、36项C检查通过（含实际图固件状态机和主机失败恢复模拟），119808B EFI
保留原FAT布局。只证明实例图可创建/关闭；没有TDM参数、时钟/管脚/功放或音频输出验收。
当前将按APM graph-open文档允许的“non-APM module calibration payloads”追加板级配置，
仍只OPEN/CLOSE、不PREPARE/START，不含PARAM_ID_HW_INTF_CLK_CFG0800113c。


v129b：400B GRAPH_OPEN 增加实例6001的三项板级参数（0800101b/08001017/08001018），
明确配置 tertiary TDM、4 slots、32-bit slot、96kHz stereo、long/inverted sync、delay1。
实机 OPEN/CLOSE 均 error0；fresh STOP ACK、PAS停止、依赖释放、冷复位ID变化与熄屏通过，
电池31.9→32.1°C。没有 PREPARE/START、PRM时钟、引脚更改或功放数据写入；尚未播放。
Android只读 smartpa 类型观察为 aw882xx，与实际 machine_dlkm.ko 的32-bit分支一致。

2026-10-31 十分钟GPU手动预览 manual-desktop-20261031b-live 正常自动结束，
硬件时序报告通过（只证明计数器/blank-window copy，不等于光学60FPS），
电池27.0→31.0°C，芯片观察峰值47.5°C，资源释放并回Android熄屏。
音频尚未接入该预览，不能将“桌面可用”作为“已出声”。


v131c：独立PRM时钟申请76B/释放64B，tertiary ID0x204、12.288MHz、attr4/root0，
两个typed回复02001002/02001003均error0/client0；各自TX_DONE及本地回复退役完整。
七根GPIO前后只读校验通过：两功放reset/扬声器switch低，四TDM脚GPIO输入，
配置和输出锁存位前后一致；没有GPIO写入、图命令、功放写入或PCM。
PAS停止、依赖释放、明确冷复位及Android熄屏通过；电池30.4→30.5°C。
297项Python、40项C检查通过。压缩诊断日志字段后EFI119808B，保留原始FAT布局。
MiCode固定提交audio_prm.c的Git blob df9193ed6138c3b9eb872c720955c8ad14904403
已经GitHub API交叉核对；请求/回复结构也比对ROM audio_prm_dlkm.ko。
只证明DSP接受并释放时钟资源，不证明引脚上的实际波形或扬声器出声。


v132c：432B OPEN追加0800113c/16B的ID0x204、12.288MHz、attr4/root0；
OPEN/PREPARE均error0/client60，START已发送并收到TX_DONE，但旧500ms窗口结束时
尚无START回复，SMP2P停止随后发生。不能声称START成功或完整STOP/CLOSE通过。
PAS停止、引脚前后只读检查、依赖释放及冷复位/熄屏通过，电池29.9→30.1°C。
v133将500ms内服务改为每10ms，温度仍每100ms采样；OPEN和清理CLOSE均error1，
没有PREPARE/START。PAS停止、冷复位、熄屏通过，电池31.4→32.1°C。
这提示初始DSP服务时序依赖，并不能据此确定具体内部错误。两版均标记FAILED并禁止复用。
v134候选保持前300ms原100ms握手节奏，仅后半段改10ms服务，仍总500ms/5次温度采样；
失败后仍只STOP/CLOSE，不重试、不写引脚/功放、不使用PCM缓冲区。


v134实机成功：五条432/64/64/64/64B命令OPEN/PREPARE/START/STOP/CLOSE均error0，
独立remote intents40/0/1/2/3、各自匹配token4d545201..205，TX_DONE和本地回复退役全部完成。
前300ms保留100ms节奏，后200ms按10ms服务，总窗口不变，5次温度采样；
此为定时启动兼容处理，并没有证明异步SPF服务就绪事件，后续长期音频应采用明确就绪状态。
legacy basic reply client60如实记录header_conformant=false，其余头/路由/token/opcode/status严格核对。
两功放reset和switch仍低、四TDM脚仍GPIO输入、前后cfg/输出锁存一致；无PCM/功放/引脚写。
PAS停止、依赖释放、冷复位ID变化和Android熄屏完整，电池32.6→33.0°C。
这证明校准后的孤立TDM sink可完整运行生命周期；不证明物理管脚波形或音频播放。
301项Python和42项C检查通过；compact日志最终校验及主机成功/失败/复位模拟通过。
EFI119808B，原始启动包和FAT布局不变。下一阶段为连接shared-memory PCM source，
受控映射/回收PCM缓冲，再处理board pinctrl与AW88263S/2032的ACF配置和低电平试听。

复用边界：上游userland/src/audio_plugin.c与redwood_audio.c已提供48kHz/S16 stereo HAL
和字符设备队列，可以复用iOS接口；redwood_sound.c使用redwood的APR/ASM/ADM/AFE和
primary TDM地址，不能照搬到marble的GPR/APM/tertiary TDM。当前板级路径是96kHz/S32，
后续需明确数据格式/采样率转换与队列节奏，而不是仅加载HAL就声称硬件音频通过。


v135：仅对编译输入副本做日志常量前缀去重，格式参数/输出日志不变，
所有被转换的源文件逐字重建核对，并归档原始与生成源码及hash；
实际loader formatter本机编译测试覆盖64-bit参数、零号字典项和普通日志。
EFI从119808B减为118272B，独立TDM五步生命周期/复位熄屏实机通过，电池31.2→32.1°C。
随后扩展为固定字段无损字典，formatter支持字符串内片段；外部字符串参数不解释字典。

v136b：528B OPEN连接WREP07001000/实例6002/静态output1到TDM实例6001/input2，
保留legacy EP3（官方APM图属性定义映射GC，而PP1映射SC，不能替换）。
WREP配置96kHz/S32/Q31、FL/FR、显式interleaved与little-endian；
media payload_size=18（不含补齐），APM param_size=32（4-byte对齐）。
所有新增官方API均按固定AudioReach graphservices提交逐个Git blob核验；
修正Linux旧struct把两个8-bit字段当uint16 alignment的差异，不照搬错误payload_size计算。
五步OPEN/PREPARE/START/STOP/CLOSE及独立intent退役全部实机通过，
GPIO前后守卫、PAS停止、明确冷复位、Android熄屏通过，电池31.3→32.2°C。
无宿主PCM缓冲区/映射、TDM引脚写入或功放命令，不能声称出声。
307项Python/44项C通过，EFI117248B，保留原FAT与启动布局。

Android原始FDT已只读提取音频DMA合同：apps-smmu@15000000，
SID0x1801、MSW sidmask0xf、coherent IOVA pool10000000..1fffffff，
硬件覆盖限制120SMR/78CB。与上游redwood_sound的音频SID/地址池相同，
但必须先确认本机已有路由/上下文状态，不能盲目复制SMMU写操作。
v137候选只读GR0的IDR/SCR/全120条SMR/S2CR两次，比较稳定性；
不读取context bank，不改SMR/S2CR/CB、清fault或发起DMA，不据空闲条目推断所有权。

v138/v139：修正DT override的解释。实际ID0=4c017e71、ID1=6000004b，
原始计数为113 SMR/75 CB，DT 120/78是边界上限；不读取不存在的尾部。
v139两次GR0快照一致，460次读，无SID1801现有路由、无全局fault；
CB0..4有现有路由，首个未绑定SMR为5，但尚不能据此断言CB5闲置。
连接PCM图五阶段/PAS/冷复位/熄屏通过，电池33.5→34.1°C。

v140：使用官方LZ4 v1.10.0/ebb370ca83af193212df4dcbadcc5d87bc0de2f0，
仅在生成编译副本中压缩完整print格式常量；参数/协议不变，源文件逐字重建验证。
262条格式26004B→11423B，EFI110080B，解码在efi_main任何外设启动之前完成。
实际formatter/varargs/嵌入NUL索引/截断块拒绝本机测试通过；实机五阶段和恢复通过，
电池31.9→32.8°C。后续字典偏移在总表≤65536B时使用uint16，否则保留uint32。
加入同一固定提交的LZ4HC，仅在主机构建时提高压缩率；固件仍使用原LZ4解码器。

v141/v142：CB5两次快照一致，SCTLR=e0（M=0）、TTBR0/1=0，TCR2=60、
FSR=400（FORMAT字段，不是fault）、CBAR=100ff、CBA2R=1、ACTLR=3、ID2=5111。
v141因“必须全零”守卫拒绝，已留证并标记禁止重启；v142按有定义的使能/fault位检查，
仍要求页表根为零、所有原始读数稳定且不被任何有效SMR引用。v142恢复通过，电池34.3→34.7°C。

v143：自有4K缓冲/三页页表编程、禁用同步、上下文原值恢复与释放实机通过；
但错误地将ID0.ATOSNS=1视为支持非安全ATS，翻译没有结果，故整轮标记失败而非成功。
固定Linux arm-smmu.c明确以!ATOSNS启用此能力，已删除本板不支持的ATS调用。
保留失败镜像/完整日志，手机已冷复位熄屏，电池33.9→34.5°C。

v144d：改用手机实际FDT中的LPASS TBU151e9000、SID范围1800..1bff、micro-idle bit6。
复用先前GPU阶段的marble_ecats_sid_access，局部SID=1；不照搬GPU TBU地址/电源配置。
参考固定MiCode48952ed3 arm-smmu-qcom.c/h，保留FDT合同和文件SHA；
先唤醒LPASS micro-idle，仅将自有SID1801临时路由到已验证的CB5，再执行ECATS。
硬件返回PAR=ff000fa0ef000a82，解码PA=fa0ef000，与自有分配fa0ec000的第4页一致。
临时SMR5/S2CR5、TBU调试寄存器、micro-idle和上下文配置全部精确恢复，禁用/TLB同步后释放四页。
原SMR5=110584（VALID=0）、S2CR5=20005，不是全零；必须恢复原值。
实机writes31、stream_writes4、TBU_writes9，无PCM请求；五阶段图/PAS/冷复位熄屏通过，
电池32.9→33.1°C。v144/b/c仅因原嵌入FAT容量不足而未进行手机测试，失败记录保留。

当前后续工作：跨异步GPR事务保留已验证的自有页表/缓冲/SID租约，发送官方MAP44和UNMAP28，
要求MAP返回非零opaque handle、UNMAP回复和TX_DONE完成后才释放；异常时仅在验证PAS停止后回收。
其后才提交静音PCM、验证DATA_BUFFER_DONE，再处理TDM pinctrl、功放ACF和低电平播放。
截至v144d，没有提交PCM，也没有编程或启用功放；不能宣称iOS音频播放完成。

v145d：MAP44/UNMAP28直接跟随24B GPR header，不照搬Linux构造函数遗留的尾部16B填充。
分配4页（三页原生4K页表、一页缓冲），在异步GPR往返期间保留上下文和精确SID1801路由；
MAP回复返回非零opaque handle=b0d7f408，随后图OPEN/PREPARE/START/STOP/CLOSE均成功，
UNMAP基本回复error=0和独立TX_DONE全部完成后才恢复原路由、禁用同步、还原配置、释放。
无PCM提交。实机电池31.8→32.6°C，PAS/冷复位/熄屏通过。
v145e复用同一个已验证EFI，仅将两份FAT12根目录表缩为16项：
磁盘/分区大小、起点、FAT份数和大小均不变；嵌入128KiB磁盘释放6个空置根目录扇区。
两份文件链独立读回、配置与EFI逐字匹配、103个其它固件文件不变、容量上限检查通过；
完整MAP/图/UNMAP/恢复流程实机再次通过，电池32.8→33.5°C。
旧镜像保持原布局，新增布局由显式构建选项记录，不读取/修改任何手机存储分区。

v146：在START成功且TX_DONE后，向WREP实例6002提交一页已逐字验证全零的4096B PCM：
96kHz/S32/stereo共512帧，WRITE_V2消息68B，SID编码地址110000000，opaque映射句柄原样传递。
DSP返回DATA_BUFFER_DONE_V2 56B，地址/句柄/模块/路由/token完全匹配，data_status=0、md_status=0，
GFSR=0且本上下文FSR无fault；独立TX_DONE完成后才允许STOP/CLOSE，再UNMAP、恢复和释放。
实机pd: end PASS=1，8条GPR命令全退役；PAS/冷复位/熄屏通过，电池32.8→33.5°C。
54项C程序随镜像构建通过。此里程碑证明一页静音数据被DSP消费；TDM仍未接到物理引脚，
功放保持reset低，未编程ACF、未启用输出，尚未集成iOS HAL或证明扬声器可听播放。
后续顺序：TDM引脚有界租约/原值恢复，复用AW2032板级驱动与ROM配置，再低电平播放和iOS流式输出。

v147b：原样v146 EFI经LZ4解压、SHA256和AArch64 PE边界检查后，使用UEFI
LoadImage(SourceBuffer)/StartImage加载；未直接跳到解码地址，未使用文件或块协议。
原EFI122880B，入口95744B，适合原始FAT布局；实机静音PCM及恢复通过，电池32.0→32.2°C。

v148b：实际FDT为qcom,cape-pinctrl，绑定固定MiCode cape源码及原始FDT合同。
GPIO121/123/122/124按板级function1、8mA、方向和偏置接通，512帧静音消费后精确恢复。
CLOSE回复和全部图TX_DONE均完成后才恢复引脚和UNMAP；输入采样位允许随时钟变化，
输出latch和完整配置必须恢复。实机接通6次/恢复6次写，共12次，三颗功放控制脚始终低，
PCM、映射恢复、PAS shutdown、冷复位及熄屏通过，电池31.7→32.1°C。
未测量物理波形、未开启功放，也未完成可听播放或iOS HAL集成。


v149：独立于旧ID-only API，新增10项非破坏性寄存器读取；不读clear-on-read SYSINT。
两颗实机均SYSCTRL=4003、SYSCTRL2=0038，确认PWDN/AMPPD/HMUTE。
OTP EFRH/EFRM1分别c3ff/4015及c3f7/401e，保留独立校正输入。
44条有界I2C命令、所有租约恢复/冷复位/熄屏通过，电池31.8→32.6°C。
完整334项Python测试和56项C程序通过。

v150：单独白名单只允许SYSCTRL2=0xf000|(原低6位)|HMUTE，即OEM 90dB衰减。
GENI三字节write+STOP，线上05 f0 38；回读两颗SYSCTRL仍4003、SYSCTRL2均f038。
共2次功放寄存器写、54条总I2C命令，未开启功放/扬声器开关、未提交任何音频。
故障注入覆盖每个回调、NACK、超时及无法abort；旧ID-only门限仍拒绝非零指针/数据写。
实机恢复/冷复位/熄屏通过，电池32.3→33.0°C。可听播放和iOS音频仍未完成。


v151：从原始ROM ACF解析两声道Music表，逐字生成44项C常量表；构建前独立核对
原始FDT声道0→34、1→35及aw_none校准模式、三份OEM源文件Git blob/SHA、完整寄存器访问权限。
保持PWDN/AMPPD，SYSINTM写ffff，SYSCTRL2强制90dB/HMUTE；不写EFCTRL/EFW和OTP编程寄存器。
VTMCTRL3由每颗芯片EFRH/EFRM1低10位有符号数按OEM 8192*(1000+ical)/(1000+vcal)计算。
每颗44项配置+1项校正均单独写入后回读，全通过；总92次功放寄存器写（含先前最小音量）、
324条I2C命令。58项C程序通过，含全部1048576种OTP组合及每个读写回调故障注入。
首次手机连接offline，在电池预检阶段拒绝，未启动/操作功放；原始失败证据保留。
重新授权后的v151b实机通过，电池33.5→35.1°C；GPIO/控制器/时钟恢复、冷复位和熄屏均通过。
保持掉电、静音、扬声器开关低，没有可听音频；下一步需在运行的TDM时钟下验证功放数字端PLL，
随后才处理有限幅播放、持续PCM、iOS HAL和其它日常外围，不能将本次结果称为音频适配完成。


v152：试验先持有QUP2配置/时钟再嵌套完整DSP生命周期；软件构建和39项I2C报告测试通过，
实机在返回阶段丢失USB。fastboot原始失败输出记录到PAS-metadata-shutdown-return及ICB destroy-returned，
随后Status read failed (Too many links)；不能据这些进度推断完整恢复，也不能将此轮标记通过。
Android已独立返回，boot ID变化、电池33.7→34.3°C、熄屏均已确认。镜像已禁止再次启动。
下一候选缩小资源重叠窗口：预先只读绑定I2C provider，在DSP START成功后短暂获取/恢复QUP2，
让QUP2资源在DSP/NPA整体回收之前释放，先不发任何功放命令。


v153：仅在START后获取QUP2，仍丢失USB，最后进度在PAS授权返回。失败镜像禁止再启动；
Android冷复位/熄屏验证，电池33.8→34.5°C。
v154：原样v148b已通过镜像作控制组，DSP、静音PCM、引脚/映射恢复全部再次通过，
电池35.3→35.9°C，证明原链路仍可用，不能把新增路径失败说成手机或USB总体不可用。
v155：把Clock ID查询延后到START以后，五次查询均返回0，之后USB中断；恢复确认35.9→36.0°C。
v156：逐次Clock API进度显示五次enable及其状态查询均返回；DAIF=300，不能据引用计数推断物理时钟ON。
v157b：增加物理ON守卫，发现core/core2x enabled=1/on=0，串行叶时钟enabled=1/on=1；
守卫在任何GENI访问前拒绝。回收引用后，原PCM准备失败等待不存在的DATA_DONE，导致退出不完整。
v157无b只有离线构建，报告行计数问题修正前已被替代，未上手机。
v158：修复未提交PCM时的STOP许可，仍禁止对已提交或畸形未退役DMA绕过等待。
真实固件状态机测试覆盖准备拒绝可STOP、已提交待TX不可STOP、畸形回复不可STOP。
安全读取GCC（不读GENI）：162010=1f，133280/13328c=80004220，12e00c=4220，
12e004=0，12e008=80000002，12e014=0，12e018=100000；确认core HALT为1。
本轮干净拒绝，QUP引用恢复、图五阶段全退役、UNMAP/上下文/引脚恢复、PAS shutdown PASS=1；
PCM_requests=0，完整功能验收仍失败，不能据清理成功称音频通过。冷复位熄屏验证，电池34.9→35.6°C。

v159资料审计：实际compatible=qcom,i2c-geni，绑定vendor i2c-msm-geni.c与
 drivers/platform/msm/msm-geni-se.c（不是mainline i2c-qcom-geni.c）。
原始FDT的wrapper 8c0000引用waipio-clk_virt master42/slave577的qup-core路径；
实际vendor源码se_geni_clks_on先geni_se_add_ab_ib，再启用m-ahb/s-ahb/se。
固定MiCode48952ed3的7份新增源码均验证Git blob及SHA；原waipio.c验证既有下载SHA。
QUP2声明keepalive_early、vote_scale1，BCM post-init空闲最低值为ACTIVE/WAKE x=y=1；
init使用16000，不允许在本移植中照搬。下一候选只申请最小60004001 ACTIVE/WAKE，不投SLEEP、不投零，
新增自有RPMh client与正投票保留至强制冷复位；不改变CPU/GPU频率、电压或充电。
这仍是诊断候选，尚待实机确认BCM缓存/ACK、原所有者不变和QUP核心实际ON。

v160实机：CmdDB两次快照稳定，QUP2地址5004c、unit=20、width=4；
发现资源已在native缓存中，按守卫拒绝，未创建QUP client、未投票、未启动DSP。
不能将该轮失败归因于ACK格式或时钟投票效果。Android冷复位和熄屏确认，电池32.5→33.1°C。
失败镜像禁止再启动。下一候选仅增加现有QUP2资源和有界vote链只读日志，保留拒绝条件。
v161首次离线构建日志格式不兼容loader自定义print，未上机，已由v161b替代。

v161b只读实机：QUP2缓存唯一资源5004c，pending=0；唯一DRV2 vote九个64位word为
2,0,1,100000000,0,0,0,4,0。按原生drv_votes_t布局，state=IDLE、priority=0、
ACK data=0/valid=1、ACTIVE data=0/valid=1/clean，SLEEP无有效值，WAKE无有效值且dirty=USE_ACTIVE。
没有新增QUP投票、没有启动DSP。冷复位熄屏确认，电池33.2→34.0°C。
原生缓存按DRV共享，不是按client隔离；v162候选只允许上述九word精确匹配，
拒绝非零/其它DRV/进行中/SLEEP有效/链式vote。仅允许目标DRV2 vote改变，
其它资源、vote、原client与队列必须精确保持；新增正投票和client保留到冷复位。

v162实机：QUP2两次请求成功，WAKE id=0、ACTIVE id=1，ChurnAll返回0。
目标vote为2,6000400100000000,1,160004001,0,0,160004001,0,0，
确认ACTIVE/WAKE最小值和ACTIVE ACK，SLEEP仍无效，其它资源和原client保持。
新增检查错误地要求原生free-node池也为空，owned_drained=0，因此DSP未启动。
下一候选使用已验证XO相同完成语义：请求完成序号匹配、无callback/barrier、
仅在途队列0/2须为空，空闲池1/3允许保留单个可复用节点，额外打印每个队列。
修正包含纯C正反例及故障注入，不用取消排空检查。冷复位熄屏确认，电池34.5→34.9°C。

v163实机：QUP2完成序号ffffffff00000001、无pending/callback/barrier；
队列0/2空，队列1保留一个free节点、队列3空，严格排空检查通过。
最小BCM保持后core/core2x的HALT由80004220变为4220，物理ON守卫通过；
START后的I2C控制器探测、100kHz配置和精确恢复通过，静音PCM一请求/回复/TX完成且无SMMU fault。
PAS shutdown和ROM恢复通过，但整轮失败：旧peak检查按ICB逻辑aggregate计算QUP2=0，
实物已有独立保持的60004001，不能将此差异当作硬件失败或直接删除检查。
v164候选只对已成功保持的精确QUP2地址，要求ICB逻辑值仍为0，
同时前快照和held快照都通过最小vote/ACK验证，才使用独立floor=20004001作比较。
其余BCM仍使用原有推导。电池35.5→35.2°C，冷复位和熄屏确认。
完整Python349项通过；后增加的floor报告测试单独5项通过。

v164实机完整通过：全部12项BCM held/ACK检查、QUP2核心ON、START后I2C窗口、
静音PCM消费、STOP/CLOSE、MAP/UNMAP、引脚/控制器/Clock引用恢复、PAS shutdown、
带宽释放和两颗LPI client释放均通过；冷复位回Android且熄屏，电池35.6→36.1°C。
此为DSP与I2C共存基线，尚无功放寄存器命令、没有可听播放或iOS HAL。
v165候选新增显式--audio-amp-profile，仅在QUP2最小投票成功、DSP START且TDM pins有效后，
复用v151 ROM Music+OTP严格白名单配置；两颗功放分别写回读、复位拉低，再恢复QUP配置/Clock，
之后才提交静音PCM。仍PWDN/AMPPD/HMUTE/90dB，扬声器开关低；没有放宽原isolated API。
50项相关Python测试通过，包含combined模式和两颗reset先于QUP释放的顺序检查。

v165实机USB中断，最后INFO到QUP Clock状态查询返回；详细boot日志未能取回，
不能确认两颗profile、PCM或PAS收尾结果，不标记通过。Android独立恢复、boot ID变化和熄屏确认，
电池34.9→35.2°C。镜像禁止再启动。v166仅补每颗amp开始/结束、窗口返回和PCM准备/提交INFO，
用于定位实际中断阶段，尚不做数字端上电/PLL或可听音频。

v166实机INFO：34/35均返回identity=1，累计命令数分别a2/144（162/324）；
I2C窗口返回1、PCM prepare=1、PCM sent intent3，随后USB中断。
这能定位到提交之后，不能证明PCM消费或PAS。冷复位熄屏确认，电池35.7→36.3°C。
v167显式prestart候选保持相同ROM/OTP写入和两颗复位，但移到PREPARE回复与TX均完成后、
START请求之前；START后沿原快路径接通TDM引脚并提交静音PCM，无长I2C事务。
运行时严格验证stage2/sent=tx=reply=3且pins未接通；旧stage3路径仍验证三项=7和pins有效。
目的仅验证时序相关性，不预先宣称已确定根因。47项相关Python检查通过。

v167窗口耗时0x297d6=169942us；prestart仍在提交后USB中断，不能据此确认START空窗是根因。
冷复位熄屏验证，电池35.4→36.0°C。
v168有界GLINK INFO显示PCM intent3 TX_DONE、DATA回复到达，随后intent4/5/6的
STOP/CLOSE/UNMAP交通和多次pump drained/IPCC空队列；不能把只有消息类型的进度当作完整typed验收。
USB仍中断，尚未取得完整日志/PAS收尾。电池36.1→36.6°C，冷复位熄屏确认。
v169仅追加runtime service/stall/stop入口进度以定位余下边界；仍无数字端上电/出声。
完整Python352项通过。

v169最后INFO为runtime第43轮service返回，随后等待未返回；v170只读确认实际FDT
SPI373(level-high)/INTID405在I2C前后enable/pending/active三位均0，无依据改动IRQ配置。
v169电池36.7→36.5°C；v170为36.9→37.0°C，均冷复位熄屏。
v171用CNTVCT只读10ms等待替换runtime的原生Stall，温度节奏与IRQ masks不变，
仍在第41轮service后USB中断，故不能确认原生Stall是根因；37.3→37.0°C，恢复确认。
v172将ROM/OTP配置移到QUP2保持后、bandwidth/PAS认证前，以免改变DSP运行时长；
首次固件BATT_THERM=38，DSP/功放均未启动，明确退出并回Android熄屏。
自然降温后v172b：两颗45项Music/OTP+reset全通过，原生命令返回OKAY(5.709s)，
但boot日志恰524287B，在audio DMA inventory中截断，后半段无法验收，仍不标记PASS。
冷复位熄屏确认，电池36.7→36.9°C。v173只将联合Music构建日志改为2MiB RAM，
普通构建仍512KiB；保留全部每项写入/读回证据。完整Python353项通过。
检查点out/checkpoint-audio-QUP2-v17-20261031已验证：1454源成员、67451证据成员、112 loader headers；
其内容至v172首次温度拒绝，后续v172b/v173不在此不可变归档中，旧v16/v8保持不变。

v173首次仍因固件电池38°C拒绝，DSP/功放未启动；回Android熄屏37.2→37.4°C。
继续自然降温至36.3°C后v173b完整通过：boot日志565110B，2颗45项Music/OTP逐项写回读、
324条I2C命令/92次数据写、reset/config/Clock恢复、DSP静音PCM、typed MAP/UNMAP、
PAS shutdown、bandwidth与LPI释放、冷复位/熄屏全通过，电池36.3→36.5°C。
仍无可听播放。v174用于独立重测START后的同配置、2MiB日志；首次温度38°C在DSP前拒绝，
37.4→37.8°C，冷复位熄屏确认，待充分降温再测，不能据预检拒绝判驱动失败。
下一阶段离线primitive为marble_aw2032_digital.h：只允许两颗ROM SYSCTRL的PWDN位清零/恢复，
AMPPD保持置位，要求SYSCTRL2精确f039，2ms/最多10次SYSST PLLS|CLKS检查，
UVLO/过流/过温拒绝，写/读/超时均走恢复；失败恢复标dirty，必须交上层硬reset处理。
未接入任何固件入口。纯C覆盖精确写白名单、每个回调失败、超时、故障和恢复失败；
离线源审计绑定已校验的OEM bit、10次上限、PWDN→PLL→AMPPD顺序和两份ROM值，2项Python检查通过。

v174b低温复测START后配置仍在第43轮service返回后丢USB，2MiB日志不能独自消除该故障。
电池35.9→36.0°C，冷复位熄屏确认，镜像禁启动。认证前配置v173b仍是已通过基线。
v176复用审计固定上游oosexgang/hakios-xnu-kernel a3384e031eb1e605cf37adaefc19991f04f6274b，
12份音频/扬声器/电池/接口/原测试文件均验证Git blob与SHA256。
原guard和guard_test用Windows clang+原内核header/空ioctl shim编译运行，结果保留audit目录；
这是主机信号测试，不是marble声学验证。可复用采样保护/音频sink接口；
redwood硬件路径为APR/ASM/ADM及AW88261 ID2113，与本板GPR/APM/TDM及AW2032不同，禁止照搬引脚/寄存器。
检查点v18已验证1458源成员、69777证据成员、113 loader headers，包含v173b完整通过证据，
不含后续v174b/v175数字端离线审计/v176上游审计与v177+新实验。

v177首次仅离线编译错误（capability分支误置于全局log声明，未产出可启动镜像）；v177b已修正并构建。
v177b在START后额外持有五个QUP原生Clock引用，内层控制器lease仍精确恢复；
额外引用只有PAS/ROM恢复确认后才允许释放，异常保持看门狗复位。
实机仍在第42轮service后丢USB，无本地PAS证明，冷复位熄屏确认，电池33.0→34.0°C。
不能支持“内层过早释放Clock”的假设，不再重复此镜像。
v178以真实500ms deadline覆盖I2C耗时，并把初始服务提前到每轮；
实机typed MAP返回error1/handle0，没有功放命令或PCM，runtime=500205us/5温度样本，
PAS/ROM/带宽/LPI释放通过，整轮功能失败。电池34.6→35.1°C，冷复位熄屏确认。
v179恢复原已验证初始100ms服务节奏，300ms后每轮处理，仍按真实500ms截止时间抵扣I2C耗时；
不扩大运行预算，不改IRQ mask/频率/电压。52项相关Python检查通过，60项C程序通过构建。

v179实机通过：START后2颗Music/OTP配置324命令/92次寄存器写、静音PCM、typed UNMAP和引脚/映射恢复、
runtime=540267us/5温度样本、PAS/ROM/带宽/LPI恢复及冷复位熄屏通过，电池35.6→36.2°C。
复测v179b因固件电池38°C在DSP/功放前退出，不能算第二次功能通过。

v180离线数字端候选由v180b完整manifest/纯C FIFO白名单测试替代，未上手机。
v180b增加数字端PWDN-only（34:b443→b442→b443，35:3443→3442→3443），
SYSCTRL2必须f039；AMPPD始终1、HMUTE/90dB保持，原OEM 2ms/最多10次SYSST轮询，随后硬reset。
底层三字节写机制提取为受每次回调白名单限制的write_raw，旧Music全1M OTP/每个回调失败测试仍通过。
实机到两颗数字端结果进度，但长INFO被fastboot截短，随后SMP2P-stop-requested后USB中断。
不能据截短值称PLL通过，PAS未证实。冷复位熄屏确认35.1→35.8°C，镜像禁再次启动。
v181短INFO证明两颗数字端函数都返回1/status11，运行时间0x888ff=559359us，
图stage5/sent=tx=reply=1f，PCM sent/tx/reply=1且bad0，MAP finished/sent=tx=reply=3，
引脚已恢复、映射不再active。停机请求后首个remote值仍6，随后USB中断，PAS仍未证实。
冷复位熄屏确认36.0→36.3°C。完整Python358项通过，数字PLL仍仅INFO证据。

v182诊断返回于STOP前，明确保留DSP/ROM/电源/看门狗，仅为日志采集而非放宽清理验收。
返回FAIL为诊断预期，后续get_staged超时，36.9→37.0°C，冷复位熄屏确认。
v183尝试在原命令INFO内发送最多32768B关键记录，以20B十六进制分块、连续offset和FNV64结束校验；
主机解码拒绝缺块/重复/乱序/越界/损坏/缺结束，不宣称PAS；3项提取测试通过。
实机在完整结束标记前USB中断，采集验收明确失败。36.1→36.5°C，冷复位熄屏确认。
v184尝试typed PCM/图/UNMAP/引脚恢复完成后立刻停机、最后一次温度检查，保留原最大预算；
本轮仍在回复dispatch中断，尚未执行early STOP，没有完成PAS，36.9→37.0°C，恢复熄屏确认。
v185保留2MiB全部typed RAM日志，仅过滤逐Clock/GCC/register/round/pump等重复USB INFO进度，
保留PLL/STOP/PAS关键进度，验证诊断握手本身对时序的影响。
首次固件电池38°C预检退出，无DSP/功放命令，待降温；不改变频率、电压、IRQ或充电。

v19检查点已验证1463源成员、79237证据成员、116 loader headers，内容至v185首次温度拒绝；
后续v185b及v186+不在其中。完整Python361项通过。
v185b降低重复USB进度后完整日志取回：2颗数字端各write2/poll1，status11锁定，
SYSCTRL b443/3443与SYSCTRL2 f039恢复，硬reset/总356命令/96次数据写均验证；
PAS shutdown/ROM/带宽/LPI恢复通过，runtime503952us/5温度样本。
但PCM sent=tx=1/reply0，图没有完成后续STOP/CLOSE/UNMAP，PAS后才恢复映射，
因此整轮功能失败，不能将PLL子项成功当成完整PCM通过。电池36.3→36.4°C，冷复位熄屏验证。
v186显式600ms/60轮/6次温度样本，与原500ms预算分开验收；
两颗PLL及typed图/PCM/UNMAP INFO已完成，但SMP2P-stop-requested后仍USB中断，
没有完整PAS证据。电池36.4→36.8°C，冷复位熄屏验证。额外100ms没有单独解决停机。
v187组合quiet+typed-complete early STOP，固件38°C在DSP/功放前拒绝，37.0→38.0°C，恢复确认。
v188发现early STOP仍受原50轮上限限制，故显式标记aud-typed-budget最大600ms/60轮，
只在typed PCM、图、UNMAP、引脚恢复均完成后立即停机，最终温度再次检查；
若未完成则按最大窗口停机并判功能失败，不以timeout冒充完成。

v188b将完成条件提取为纯C gate，逐字段每bit变异覆盖图/PCM/TX/片段/错误/映射/引脚/功放dirty，
任一未退役状态都拒绝提前STOP；typed预算报告独立验证最终温度样本数=round/10+1及两颗数字端恢复。
实机第55轮typed完成后立即STOP：runtime552944us/6次温度样本；两颗PLL status11/poll1，
356条I2C命令/96次寄存器写、静音PCM消费、typed UNMAP、图关闭、引脚/映射恢复、
fresh SMP2P停机ACK、PAS shutdown、ROM/带宽/LPI恢复、冷复位和熄屏全部通过，电池36.6→36.6°C。
这是首个数字端+PCM+完整停机通过，模拟功放仍AMPPD、HMUTE/90dB，尚无可听播放和iOS HAL。
v189同路径强化SYSST故障屏蔽：加入OEM OVP2S15、OVPS10、BSTOCS11、CLIPS7，
已有UVLO14/OCD3/OTH1仍拒绝；NOCLK5阻止成功判定。源审计核对全部位，C逐bit故障测试通过。
模拟阶段只新增离线marble_aw2032_analog.h及测试：要求数字端PLL已锁、SYSCTRL2=f039，
只允许AMPPD清零/恢复，最多10次1ms状态检查且status311，故障/超时走恢复；
没有任何loader入口启用它，也没有启用扬声器开关、反馈、音量或出声。


v189实机仍在STOP请求后USB中断，故v188b单次通过尚不足以判稳定。冷复位熄屏确认，电池36.5→37.0°C。
v190仅将STOP后等待轮询改为100us CNTVCT只读等待，5s超时、100ms温度检查、IRQ mask和fresh ACK要求不变。
首轮完整通过：两颗PLL status11，typed完成round55，runtime552985us/6温度样本；
fresh STOP ACK等待14291us，PCM消费/UNMAP/PAS/ROM/带宽/LPI恢复及冷复位熄屏全部通过。
电池36.5→37.0°C。365项Python回归全部通过；尚需独立复测，未启用模拟功放或可听音频。

v190b同镜像独立复测两颗PLL INFO返回成功，I2C窗口25729us，PCM发送后USB中断，未取得完整日志/PAS证明。
冷复位熄屏确认，电池36.5→37.0°C；v190镜像禁再次启动。失败早于STOP，不支持fast STOP独立修复稳定性的结论。

v191独立audio-usb-ram-only候选：auth尝试后PIL及ICB/XO进度只写RAM，不做USB INFO发送；
保留2MiB typed日志、500/600ms原运行预算、100ms温度检查、fresh ACK/PAS/冷复位验收。
显式构建标记、镜像profile与运行选项一致性、独立capability均验证；旧镜像不会默认使用此路径。
报告只把policy作为子证据，不替代PCM/PAS/reset；缺失/重复/改动保护位/无抑制计数均拒绝。
62个C程序随构建通过，模拟功放primitive仍未接入硬件。此候选只验证USB握手时序假设，不假定根因。

v191首轮完整PASS：post-auth抑制38次INFO、完整日志552780B；两颗PLL status11，
typed PCM/UNMAP/引脚恢复round54，runtime542952us/6温度样本，fresh STOP ACK14405us，
PAS/ROM/带宽/LPI恢复及Android冷复位熄屏确认，电池36.5→37.0°C；367项Python全部通过。
这是首轮通过，仍待独立复测。保留v190b失败，不宣称已确认USB握手为唯一根因。

v191b独立复测完整PASS：runtime543027us/6温度样本、round54、STOP ACK14411us、38次INFO抑制；
两颗PLL/PCM/UNMAP/PAS/ROM/资源恢复及冷复位熄屏全部确认，电池36.5→37.1°C。
两轮通过仅适用于bounded静音诊断，不等于流式音频稳定或已确定唯一根因。
v20检查点已归档校验1471源、85860证据、118headers；内容至v191首轮，未含v191b/后续模拟功放代码。

v192将模拟AMPPD测试通过独立白名单FIFO/回调接到digital locked_probe，构建/运行显式选项一致、capability验证；
旧数字/Music API未放宽。两颗只允许b442→b440→b442或3442→3440→3442，SYSCTRL2始终f039，
最多10x1ms，所有回调失败仍走外层PWDN恢复+硬reset；无音量/扬声器switch/反馈修改。
严格报告核对每笔MMIO/typed读回、PLL锁定与数字恢复之间的顺序、100次数据写和392条命令。
63项C程序及371项Python通过；嵌套模型逐回调失败覆盖，旧模式拒绝模拟证据。
首轮实机PASS：两颗status311/poll1，模拟窗口7167/7304us；PLL、静音PCM/UNMAP/引脚恢复、
PAS/ROM/资源恢复/冷复位/熄屏均通过，runtime562950us/6温度样本，STOP ACK14267us，电池35.4→36.1°C。
仍保持HMUTE/90dB，无可听播放；待独立复测。

v192b独立复测PASS：两颗模拟status311/poll1，7238/7276us，100次数据写/392条命令；
PLL/typed PCM/UNMAP/引脚/PAS/ROM/资源恢复/冷复位/熄屏均确认。
runtime562947us/6温度样本，STOP ACK14446us，电池36.7→37.1°C。
已通过两次bounded静音模拟上电，尚无可听播放、持续PCM或iOS HAL；后续先验证连续buffer退役与停机，再接音频sink/guard。


v193新增显式audio-pcm-repeat：同一4K页串行两次静音WREP，token4d504401/4d504402；
只有typed DONE56与RX_DONE_W_REUSE(14)均退役且SMMU无故障，才允许页/intent复用。
第二次重新核对zero/cache/pin/lease；任一旧token、错误handle、重复TX或DMA故障拒绝。
原单请求分支保持；MAP/UNMAP、GLINK包数、物理buffer数与提交次数分开计数和验收。
主机C覆盖两种到达顺序、无提前复用、无第三次提交和错误路径；Python验证逐请求顺序/geometry及旧模式拒绝。
v193首次仅打包超小FAT容量，没有镜像上机；v193b紧凑root（相同磁盘/分区大小）离线构建，
同期报告pr:前缀误匹配旧gpr:已修复并补回归；v193c重建源码/报告完整闭环，64C/375Python通过。
v193c实机两个PCM、typed UNMAP及引脚/SMMU恢复、fresh STOP ACK全通过，runtime572952us/6样本；
但PAS调用45次/elapsed1408865us，虽raw_a0/a1=0仍超过原1000000us限制，严格判失败，保留资源/看门狗。
Android冷复位熄屏确认，电池31.3→32.0°C；镜像禁再次启动，不能把子项完成当完整通过。

v194只加显式audio-pas-timing，CNTVCT只读记录各SMC/thermal/other耗时，调用后输出RAM日志。
不改1s/128calls上限、IRQ mask、运行预算、频率电压或温度要求；计时报告只验一致性，不覆盖PAS验收。
64C/377Python通过。首轮完整PASS：两个typed PCM（8192B/1024frames，总物理页4096B）和回收、
UNMAP/PAS/ROM/资源恢复/冷复位熄屏确认，runtime572970us/6样本，电池32.3→33.2°C。
PAS608604us=SMC608155us+thermal274us+other82us+少量计时/边界开销，max SMC181856us；
本轮未复现延迟，不足以认定上一轮原因。仍无可听/连续播放，也没有接通marble内核DSP后端。
本地现有redwood_audio.c的sink接口和userland audio_plugin.c已存在，可沿用，不另写HAL。

v194b同镜像独立复测完整通过：两个PCM请求逐一DONE/TX回收、UNMAP/PAS/ROM/资源恢复/冷复位熄屏确认，电池33.9→34.5°C。
PAS计时：{"version": 1, "calls": 44, "total_us": 608403, "smc_us": 607950, "thermal_us": 274, "other_us": 82, "max_smc_us": 180715, "limit_us": 1000000, "limit_calls": 128, "IRQ_masks_unchanged": 1, "USB_inside_calls": 0, "audio_acceptance": 0}。两轮bounded通过，不代表长期流式播放稳定。
