# marble ADSP 原生租期接口审计，2026-10-29

本阶段仍未启动 DSP，不代表音频、真实 SOC 或完整日常功能已完成。
所有实机启动均为 fastboot boot；不刷写、不擦除、不访问 Android /data。

## 原生 RPMh ABI 与所有权

原 Mu RpmhDxe SHA256：
`7f917c9e9c64ba11bee99f3d56f5474d39dac210f5a2dfad2059dd1d4afcd3c6`。
GUID `e1d0b573-f29f-495d-9352-ca0dd42a0a74`，table RVA e0f8，revision10001。
24 个函数槽全部验证，并逐条匹配 10 段、382 条原生指令；不根据旧头文件的
相同 revision 假定整体 ABI 相同。`tools/marble-native-lease-code.py`可从固定
SHA 的原 EFI 重新检查签名，范围清单保存在原生审计目录。

DRV 配置固定为 software2/hardware2、每 TCS16 条命令。槽1为合法性查询，槽2
为命令容量查询，槽5为 regular create，槽8为 delete，槽9为纯缓存查找；均无
This 参数。槽0 Init 不调用，槽17返回 UNSUPPORTED，槽18发命令未开放。

native4ef4直接覆盖同一资源/DRV的 local vote；创建不同 handle 不等于投票隔离。
因此没有直接向 xo.lvl/300b0 发送3或将其作为 MMIO 地址。ClockDxe 的
`/pmic/client/xo`目前仍是 NPA 占位符。旧公开 ClockSources.c仅作流程参考：
NPA handle尚未创建时 SourceNPARequest会跳过请求，不能拿函数返回成功当实物
时钟已持有。PwrUtilsDxe中的xo.lvl用于命令数据库元数据查询，尚未证明是租期入口。

## v46 / v46b：拒绝证据保留

v46指令和配置匹配，但首次缓存快照拒绝；未调用 RPMh 方法，未创建客户端。
v46b补充诊断发现原生堆对象末尾为1c/9c，按4字节对齐；8字节对齐读取器过严。
改为复用已有跨字、恒等映射读取函数，不放宽地址窗口、环路或数量检查。
两次均独立恢复 Android 并确认熄屏；失败结果保持不变。

## v47：零命令 regular 客户端，两轮实机通过

同一镜像 `out/daily-rpmh-zero-v47-plan/loader-boot.img` SHA256：
`cfb34cd62e5d607e9912934cfbbc3d9af5a192cca35ffb09d0600bb68e199830`。

| 证据 | 完整生命周期 | 电池 Android 前→后 |
| --- | ---: | ---: |
| daily-rpmh-zero-v47-live | 277us | 35.6→36.2°C |
| daily-rpmh-zero-v47b-live | 290us | 34.7→35.0°C |

两轮均32资源、32个 per-DRV vote对象、5个已有客户端、空闲池原为0。
两次操作前快照稳定。原生查询确认 DRV2有效/容量16、XO缓存不存在。
新客户端追加到 active末尾，四个内部队列为空，req_id0、无 callback/explicit
commands。确认成员资格后才调用 Delete；原有 active成员顺序、完整客户端对象、
其内部队列头、资源表和全部 per-DRV vote对象逐字恢复，global和manager指针不变。

原生 Delete 将新对象放进 native pool，pool1；这是驱动的分配池保留，不能宣称
全部 heap 原样恢复。没有释放原生所有的对象，没有再次调用已删除 handle；
只核对原生 pool中的地址成员资格。没有遍历或声称恢复硬件 TCS/物理电压；
缓存相同不代表物理读回。没有发命令、投票、NPA/QMP/PAS调用或启动DSP。
watchdog_clear0、dirty0，之后明确冷复位，Android boot ID变化和屏幕OFF确认。
Android电池端点不能当作固件期间连续峰值。

纯 C 测试覆盖4字节对齐、链表环/数量/尾部、跨资源vote重复、资源排序、
request/callback/explicit残留拒绝和指针溢出。真实精简日志另作解析器 fixture。

## 原生 ICB 下一步

ICBDxe SHA256 `6cd27554f8edcf3bbe41473448354e55cd57461f0c29abbe18a5a8b6fdca596a`。
GUID `5824f9de-17d2-4b1f-a5fe-5eb77fa53093`，table c108，revision10000，只有
Init槽13f4；禁止重复 Init，不虚构协议 Create API。

native42a0由c338配置取resource_index对应树，43fc查找
`key=(firmware_master<<32)|firmware_slave`；节点为key/route/left/right，
route为count/pairs。两段27条指令与原EFI匹配。现有代码只做两次完整RAM元数据
快照和逐字比较，不调用原生函数，不读节点所指向的硬件寄存器。

v48触及256节点上限而拒绝，未创建ICB客户端或投票，Android电池35.3→36.1°C、
屏幕OFF。后续v49将硬上限设4096、树数8、深度32、每route16跳；仍检查环路、
重复节点、BST排序、空指针和映射，明确允许合法 master0/slave0。
不能将部分树当完整图，不能将旧SoC `icbid.h` 或 Android43→512编号直接用于
原生固件接口。硬件身份仍须板级资料核对；业务带宽租期尚未接受。

v49实机完整通过：一棵树1018条路由、45个master编号和65个slave编号，
两份完整元数据快照逐字相同，耗时13637us。诊断镜像SHA256：
`b890c361a49decaad5d7ca63ee3bc57888f00b0bae68e15f7fa86b1c60e2cbd7`。
`out/daily-icb-inventory-v49-live`保存全部证据；原生函数调用0、客户端0、投票0、
MMIO写0、DSP_start0，返回Android电池36.8→37.0°C、屏幕OFF。

固件编号23→0存在四跳路径（index191），43→512不存在。旧公开icbid.h中
CRYPTO=23/EBI1=0与此候选一致，但该头并非marble的原始构建输入，不能独立作为
硬件身份验证。Android的MASTER_CRYPTO43/SLAVE_EBI1512属于另一编号域，不能直接
传入原生ICB。后续仍需核对节点硬件配置及NPA扩展创建ABI、零请求释放，再发业务请求。

当前114项Python测试、15项C检查、EFI marble/generic两目标均通过。
本阶段没有重建XNU，继承的kernel symbolset/export问题不在本阶段宣称修复。

## 本地复用材料和入口

审计目录 `out/adsp-native-lease-audit-v1-plan` 保存额外提取的 CmdDbDxe、
PwrUtilsDxe、PmicGlinkDxe和充电相关EFI、SHA清单、完整反汇编和原生签名范围。
references固定Rivko旧固件commit20bb8ae36c93fc16bbadda0e0a83f930c0c8a271的
ClockSources/ClockDriver/ClockVoltage、NPA和ICB头及实现。旧SoC仅作结构和流程
指南，寄存器地址、端点编号、native ABI均必须独立匹配 marble。

```powershell
python -X utf8 tools/marble-native-lease-code.py
python -X utf8 tools/marble-gpu-preview.py --out out/NEW-live `
 --boot-image out/daily-rpmh-zero-v47-plan/loader-boot.img `
 --ramdisk out/daily-rpmh-zero-v47-plan/test.apfs `
 --system-disk out/marble-system-zh-v1.apfs --discovery-only --rpmh-zero-cycle
```

Host拒绝混合其他探测、UI或PIL载荷。客户端失败且所有权不确定时保留watchdog，
不继续调用驱动。旧v34b/v38 DSP执行与停机路径继续禁用。

## 后续 v50–v60：真实 XO 和首个带宽请求

前文是历史阶段记录。以下是新增结果；DSP、音频和完整外围仍未完成。

- v50b：四跳端点名称与 marble 内核拓扑一致，固件 ID 为23→0。总线元数据
  NULL名/base0仅标为virtual，不视为实际NoC寄存器身份。
- v51/v52：零请求客户端创建成功，但旧代码误将NPA table[14]（查询可用性）
  当作销毁入口调用，导致重启。两版失败保持，host按镜像SHA禁用。
  已改为类型明确、offsetof/原生RVA共同检查的table[15]/offset78/15e4。
- v54/v54b：修正后零请求创建/销毁通过；原有NPA客户端、八端点请求成员和
  RPMh缓存原样恢复。原生链表容量可能增长10并保留，不宣称恢复堆分配。
- v56：隔离软件资源`/marble/clock/xo-test`，三客户端六步required/suppressible
  合并及释放通过，RPMh调用0。资源定义保留到显式冷重启，Android电池32.7→33.4°C、熄屏。
- v57/v57b：真实RPMh XO ON请求通过，两轮均sleep1/wake2/active0集合分别请求
  command DB地址300b0、数据3；运行集合completion后调用原生ChurnAll完成。
  原有32资源/32投票对象/5客户端完整保留，只新增自己的XO资源和客户端。
  用完不发零票，保留到强制冷重启，因为固件在缓存建立前的真实XO硬件票未知。
  电池32.3→33.0°C、33.4→34.0°C，均确认boot ID改变并熄屏。
- v58/v59：真实crypto→DDR 1MB/s type3峰值请求成功，CompleteRequest后自有请求
  Ib/Ab归零，DestroyClient成功，原有NPA客户端与资源对象恢复。
  但逐字检查未通过：五端点历史带宽字段改变，RPMh新增两个缓存项，ICB客户端
  请求序号增加。两轮均保留失败，等待watchdog恢复Android，电池33.9→34.4°C、
  34.8→35.0°C，熄屏。不能把它们当作完整带宽租期恢复成功。
  v60继续采集新增缓存的原生投票值，不放宽实际投票恢复条件。

所有原生方法的table下标均包含revision槽0：RPMh Create[6]/offset30/16ec、
IssueCommand[19]/offset98/1890、ChurnAll[11]/offset58/17b4。
FinishAMC[13]/17e4不是Churn。NPA向量请求[18]/offset90/162c，完整请求[3]/
offset18/1474；1644是查询句柄创建，不能当作向量请求。

新增代码SHA摘要绑定来自固定SHA的Mu原EFI，不是ROM外的任意二进制。
`tools/marble-rpmh-xo-code.py`与`tools/marble-icb-bandwidth-code.py`可以重验。
摘要算法复用public-domain B-Con SHA256，来源commit和本地修改有清单记录；
用于公开指令的相等检查，不作为固件签名或认证。

`--firmware-only --adsp-only --efi-opt Oz --efi-lto`为独立依赖诊断镜像，
裁掉旧OEM处理器使EFI保持原嵌入FAT容量内；原BootShim/DTB/磁盘几何保持。
该镜像明确拒绝XNU/GPU启动，不替换已验证桌面镜像。

## v61–v65：HOLD 是硬件票不回落的原因，使用发布的 post-init 入口

v61b/v62在读取额外硬件拓扑时干净拒绝，没有发带宽请求；拓扑数组可以重复引用
同一节点，v63按对象地址去重读取，原128项数组上限保留，得到19个不同节点。
v63严格比较显示：原有RPMh资源和投票、八端点当前聚合、全部BCM当前聚合、原有
NPA客户端都恢复，但新缓存5001c仍是20000001，按请求前聚合计算应该是0。
这轮明确失败，没有把历史字段放行后就接受非零残留。watchdog恢复Android，电池35.7→36.0°C、熄屏。

原生ICBDxe的3734将旧total_bw复制到state+30作为history；master状态40、slave状态20。
history变化有指令依据，不是队列残留。关键在3a84：sw_state2/HOLD时只向硬件提交
非零聚合；3/FLUSH、4/RUN才提交零票。参考ul_int.c也有相同条件。

重新审计发布的Init槽：13f4→2aa4→4088，**这是已初始化驱动的post-init/RUN
转换**，不是重新建图。真正建图在2854，驱动入口1438调用；之前“Init不可调用”的
保守边界现按新增代码证据细化。新入口不调用2854，不直接改c328状态变量。
2aa4持有原生root mutex，4088设置FLUSH、遍历拓扑并刷新，最后进入RUN；随后按
原生流程处理两个standalone节点。全部相关指令、互斥包装及完成处理另有10段SHA绑定。

v64仅调用该已发布协议，2→4转换实机通过，2889us。19个硬件节点当前聚合、
原有NPA客户端不变；调用正确的RPMh FinishAMC/DRV2处理完成事件，投票和客户端
pending队列已排空。无DSP和显式带宽请求，没有原样恢复固件HOLD状态的宣称；
RUN和缓存保留到明确冷重启，Android boot ID变化，电池33.7→34.3°C、熄屏。

v65在新的单一命令内部先做同一原生RUN转换，再以转换后的状态为基线测试1MB/s
请求/Complete/Destroy。两个阶段独立日志校验，不能把RUN后的基线描述成原始
固件硬件恢复；仍要求冷重启。v65/v65b两轮通过：申请和完成各一次，NPA客户端、
八端点live状态和19个BCM live聚合恢复；44个RPMh资源/投票值完整保持。只有ICB
原生客户端请求ID前进2，history和池容量按原生算法保留，没有改写这些驱动字段。
带宽子阶段2537us（第一轮）；两轮电池33.9→34.0°C、34.9→35.4°C，显式冷重启
boot ID变化，Android熄屏。

v66新增组合依赖测试：发布的RUN转换→两路配置LPI256→物理XO3→1MB/s带宽，
共同保持10ms并检查现场状态，再Complete/Destroy带宽、释放LPI到已验证的零请求
状态。XO和固件RUN保留至强制冷重启。旧独立接口不接受这些混合状态；新的内部
mode只在同一受控命令中使用，并检查native RUN4、两路NPA active/dirty及XO请求。
尚未调用DSP执行。v66/v66b两轮组合验收已通过：共同持有10ms时XO active票收到
确认、两路LPI资源/aggregate均为256、带宽请求Ib1000000/Ab0；带宽完成/销毁后
原有RUN/XO/LPI基线缓存完整保留，随后两路LPI原生释放并确认零角落缓存，XO仍为3。
两轮均显式冷重启、boot ID变化、Android熄屏，电池33.5→34.2°C、34.7→35.0°C。
这只是依赖组合验收，不能称DSP、音频或全部外围完成。

v67完成离线RAM输入包：4096字节头部区、118784字节固定SHA的PAS-refusal/no-AUTO
PILDxe和30057264字节原ROM RAM文件集，总计30180144字节。C解析器固定版本、
精确几何和两项既有FNV指纹，拒绝截断/溢出和两项输入的字节变异；实际本地包
已由C读取验证。输入SHA/偏移另存manifest，不通过文件系统访问手机存储。
尚未为此包开放新的DSP命令。

当前全部143项Python测试、21项C检查（含真实RAM包变异拒绝）、marble/generic
EFI编译通过；62段原生代码签名与固定SHA的Mu原EFI相符。未在本阶段重建XNU。

## v68：在全部依赖共同持有时验证 ROM/PAS 无执行阶段

v68的首次通用ADSP诊断镜像EFI达121344字节，离线打包因原嵌入FAT容量拒绝；没有
启动手机。新增`--owned-stage-only`（要求adsp-only和firmware-only）裁掉其他诊断
命令，得到90624字节EFI。原盘几何、BootShim/DTB和其他103个固件文件保持。
host检查镜像profile和专有capability，拒绝用此专用镜像跑其他试验。

新单一命令`adsp-owned-stage`接收固定SHA的MADSP001 RAM包：先检查GOP/DPU源与
ADSP carveout分离、SCM可用性，再原生ICB post-init→保留watchdog安装PAS-refusal
PILDxe→SMP2P准备→LPI256→XO3→1MB/s，保持所有依赖时装载24个ADSP2文件、
读回/BSS检查、无执行PAS shutdown、恢复49MiB原内存并释放文件协议，随后才释放
带宽和LPI。XO、SMEM和固件RUN保留至明确冷重启。旧DSP命令仍未开放。

v68b第一轮完整通过，外层LPI租期4152461us、USB命令4.602s；电池31.9→32.7°C，
boot ID变化、Android熄屏。无DSP auth/reset；不能从此宣称DSP或音频完成。
当前146项Python测试、21项C检查、marble/generic EFI和62段原生指令核对通过。
v68c重复测试也通过，USB命令4.594s，电池32.8→33.4°C、显式冷重启、Android熄屏。
新的v5恢复点另存当前源码和所有成功/失败证据；v1–v4保持不可变。

## v69：新的完整依赖下仍未通过执行后 PAS 停机

新执行门只在原生RUN4、两路LPI256、真实XO3和实际带宽请求共同持有后设立；
流式`owned-dependencies-ready`先于PAS auth，旧v34/v38入口保持禁用。
v69使用同一MADSP001输入，加入500ms观察、新鲜SMP2P ready/stop-ack后再停机。

实机流式证据：PAS auth EFI0/transport1/secure0、ready6、stop请求1、fresh stop-ack10e；
随后仅有`PAS-metadata-shutdown-before`，没有shutdown-return，USB返回Too many links。
原始记录在`out/daily-adsp-owned-boot-v69-live/command-failure-00.txt`；后续oem log
等待超时，不能把该次超时当作第一失败原因。Android独立恢复、熄屏确认，电池
31.6→32.4°C。原地停机、内存恢复及资源释放均未通过，绝不从Android恢复倒推成功。

失败镜像SHA256 `ab23e1ba24cbbd9bdfeb4141b8a93c304303d56b6907f77e5a1018ccf7ed11c9`。
host按SHA禁用；源码dispatcher和capability也重新禁用该执行入口。
不重复同一镜像。仍可用v68b无执行版及其v5恢复点。

现有验证只证明1MB/s带宽请求和依赖接口正确，不能证明该数值覆盖Linux在SCM
auth/shutdown期间请求的最高带宽。AOP/QMP `load_state:on`尚未接入；继续从本机
固定Linux源码核对业务消息、传输完成、所有权和off/冷重启流程。之前QMP只验证
无TX的开关通道，不能当作已具备load_state控制。DSP服务/GPR/音频仍未完成。
