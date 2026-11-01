# 音频运行期限与停止链路（2026-11-01）

音频尚未完成。没有HAL、长期播放或实际出声验收，全部板级测试仍为静音。仅RAM临时启动，不刷分区，不访问Android /data。最新完整双轮静音stream24基线为v259，v241保留为旧八页基线。

## 当前进展：v258显示审计失败已作定点修复，v259首轮通过

v254仍USB中断，v255通过MAP/START但I2C窗口状态变化被原校验拒绝，v256/v257预启动候选USB中断，均禁用且不得原样重启。v253诊断内核的源码已保留，但v255之后沿用v242旧stream24内核，不能把当前诊断源码冒称这些镜像的内核来源。
v258改为QMP pre-open单次CPU_ON，保持100ms原DSP启动服务时点、300ms后2ms，不提前应用cap；取消自有10ms/TPL16运行期采样事件，保留真实600ms墙钟与温度校验。该轮24页/oracle/19写/绕回、quiet/STOP/UNMAP/PAS、XNU unmap、DMA free及QMP两次ACK/恢复均通过，DSP530080us、原始PAS608520us、QMP1382320us；但最终恢复前显示审计先做连续VSync测量，missed=1导致尚未读DMA字段就退出，完整metadata-stop716943us dirty=1，整轮仍失败禁用。

v259将原显示总线租期获取从VSync连续测量中抽出。普通显示/VSync入口保留220次原采样；仅显式audio-display-dma-only且muted owner-board stream24采用同一总线获取/恢复执行DMA审计。仍读全部66字段、两份间隔20ms样本，保留Demura关闭、8pipe/额外平面、保守ADSP重叠、精确GOP与时钟恢复检查。builder/controller/profile/report显式绑定；错误模式拒绝；报告器逐项要求实际两份2个Demura和8个pipe记录，不能只有PASS标记。
首轮out/audio-display-dma-v259-live整轮通过，DSP530082us、metadata-stop716754us、QMP1385945us，三次完整DMA审计通过；Android新boot ID/熄屏通过，电池33.7→34.4°C。第二轮out/audio-display-dma-v259b-live也整轮通过，DSP520123us、原始PAS608559us、metadata-stop717030us、QMP1375849us；电池34.4→35.8°C，Android新boot ID/熄屏通过。v259成为最新双轮静音stream24基线，仍非HAL/长期播放/实际出声完成。
boot SHA3f1f45c3e2eaaee18f4354a4ae4fdb4ff8180906856ae0d440e7970f1414765f；kernel SHA1d3bf5af38032c6d6d9a93562e58d223422b3751520e7dfa7c9dcac2217e75dd。
75项C构建程序（含66字段故障模型）及328编译输入哈希通过；8项DMA报告测试（含4项导入stream回归）和10项handshake报告测试通过，builder/controller错误配置拒绝通过。前次449项Python全量在v255前后保存，不能宣称它覆盖此次最新改动。

## 已完成修改

- 新runtime bound在typed完成后、最终thermal之后及采样cancel/close/日志之后复核实际墙钟，300000≤us<600000。包括v242b的1529933us在内的超时被固件拒绝，不能由轮次或typed完成豁免。
- 自有10ms/TPL16事件只读RAM，正常530ms无栈，异常较晚阶段最多两份原始栈；停止前关闭。失败路径也尝试释放。不将原始栈称为有效unwind。
- 新USB-QMP-v3停止租期将已鉴定USB轮询的恢复移到QMP关闭之后，仍SetTimer API、100ms周期重排；无IRQ/充电/外国回调或pending链表修改。原始PAS和外层metadata-stop分别<1s，QMP close仍750ms、总2.5s。
- QMP等待再受外层890ms约束，为原100ms观察、ADC和恢复轮询留110ms；仍用真实ACK/descriptor/context/Clock/DAIF和之后轮询进度判定。
- 加入只读分段计时；尝试显式10/50ms启动服务，默认旧100ms路径不变。该优化候选还未有硬件成功证据。
- 同步启动命令返回终态后可直接取暂存日志，不先查result。v249暴露主机遗漏re导入，已修并新增真实失败字符串回归；不能把修复前失败追认为通过。

## 实机记录

| 候选 | 结果 |
|---|---|
| v242 | 首轮24页通过，复测DSP1529933us、QMP2545500us，禁用 |
| v243 | USB Too many links，无运行日志，禁用；Android冷复位熄屏、电池32.1°C |
| v244 | 首轮完整通过；复测数据/DSP/PAS通过但OFF800575us超时，禁用；恢复35.0°C |
| v245 | 数据/DSP/PAS、两次QMP close ACK通过，QMP总2500501us超时，禁用；恢复34.7°C |
| v246 | 一轮完整通过，24页oracle、19次write、FIFO绕回、quiet ACK/UNMAP/free，DSP530168us，外层716678us，QMP2499824us；Android冷复位熄屏、恢复35.0°C。仅一轮，且QMP余量176us，不作为稳定完成 |
| v247 | 10ms候选启动命令约3.896s返回，随后查询超时，无完整日志，禁用；恢复35.5°C |
| v248 | 日志回收版USB Too many links，无完整日志，禁用；恢复35.4°C |
| v249 | 50ms版收到no-such-core 1 0；主机NameError导致未取完整日志，整轮失败禁用；恢复36.1°C |
| v250 | 解析已修，收到no-such-core 1 0和SMP2P-stop-unqualified；取完整日志超时，禁用。恢复Android熄屏，电池36.3°C |
| v251 | 已构建，新增首个owner失败原因及CPU/QMP/AUTH位图、最终固件ADC读数结束回复。Android预检电池36.5°C，超过原≤36°C启动门槛，未重启、未启动该镜像，仍是未实机候选 |

v246分段：ON约2389us，XNU READY137771us，AUTH251148us，数据530168us，真实停止ACK1001829us；metadata-stop开始1783286us，关闭前观察2397682us，观察后2497686us，ADC后2499802us。主要固定等待为停止ACK，不能假装已收到，也不能放宽2.5s。

## 验证范围

v244前439项全量Python通过，v248阶段446项全量Python通过；后续50ms配置另有组合审计检查，v250真实失败回复解析1项通过；之后补入50ms匹配回归。每次成功EFI构建运行75个C程序、328编译输入哈希一致。内核未改，沿用v242源内核SHA1d3bf5af38032c6d6d9a93562e58d223422b3751520e7dfa7c9dcac2217e75dd；原完整make仍有既存symbolset/export错误。

下一步先取到早退的完整RAM日志，定位为何未能启动/完成，再完成24页双轮稳定闭环，之后才进入真正生产者/EOS/排空/停止、长期资源与热管理、现成HAL接入及有限幅出声。不能用延长600ms诊断窗口冒充长期播放。

## v251/v252后续实机

v251首次只在Android预检36.5°C拒绝，未启动；冷却至34.9°C后v251b真实启动，但READY/AUTH通过后仅DMA-granted，未CONFIGURE，实际600453us超时，采样关闭成功；实际固件ADC35°C，温控不是该轮拒绝原因。Android冷复位熄屏，电池35.5°C。
v252错误路径仅在已launch且kernel-live结构/地址/长度在RAM窗口内时保留有界只读内核ring；结果改为audio-owner-failed，不再误称PSCI未启动。实机完整失败日志已取回：XNU在MAP阶段pending=1、published=0、owner_state=2失败，未执行功放命令。Android冷复位熄屏，电池34.8→35.1°C。
这些是诊断取得的新证据，不是音频成功。v251与v252均禁用，不原样重启。下一步在既有协议引擎保留被拒绝回复的有界原始字节，仅失败时输出，区分MAP返回错误、GLINK控制错误与早期初始化；不猜测或重发DMA命令。
最新全量Python448项通过，日志out/audio-first-fault-v251-plan/python-tests.log。现有75C构建测试与328输入哈希核对通过。

## 原始消息诊断与v254候选

checkpoint-audio-runtime-v33-20261101已核对1595源成员及9206证据成员，父v32，保存至v252的全部候选/失败/成功记录。
v253在私有stream/endpoint结构中增加有界64字节诊断，只记录最后完整GLINK帧及重组GPR回复，失败才打印，不改变wire/proof/owner ABI或放宽校验。坏MAP回复被拒绝并完整保留、尾部清零的C测试通过；原2048事务、TX/DONE顺序、碎片、intent复用与DMA失败保留模型通过。
新内核SHA441ec8e49e90817351de2b2ac46eaae0377c0da84a5d6ed0e4dd45d2b90e2f57，27项相关源码与VM按LF归一化一致；CC redwood_audio、LD、STRIP完成；完整make仍既存symbolset/export错误exit2。EFI75项C测试、328输入哈希通过。
v253实机USB Too many links，未取得原始消息，不据此判定拒绝原因；恢复Android熄屏，电池33.9→34.1°C。镜像禁用。
v254保留上述内核诊断，显式warmup_ms=100：最初100ms沿用旧0/100ms服务时点，此后到300ms用50ms；300ms后仍2ms服务，不扩大实际600ms、PAS1s或QMP2.5s。已有8项fast报告/组合fixture测试通过，不能视为实机证明。正在首次实机，状态见out/audio-hybrid-start-v254-live/exercise.json。
