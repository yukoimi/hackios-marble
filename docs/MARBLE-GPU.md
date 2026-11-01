# marble Adreno 725 移植记录

当前状态（2026-10-25）：**Adreno 725 已执行用户态 shader，并接入 CoreAnimation 桌面 GPU 合成**。
实际 renderer 为 Mesa 25.0.7 / freedreno / FD725；marble 使用独立 Gen7 内核服务，
不是仅改原 Adreno 642L / A6xx 的 MMIO 地址。输入纹理使用上传副本，输出保留共享 IOSurface，
规避已复现的负一屏及 App 资源库黑条块。Metal、完整应用兼容性和长期稳定性尚未验证。
以下按日期保留过程记录；早期“尚无硬件加速”等表述只对应当时阶段。最终产物及限制见文末。

依据：Mu 镜像的 marble DT，以及 MiCode/Xiaomi_Kernel_OpenSource
commit `48952ed36228217531482b39d5bef13e7fd808ec`。
参考文件位于 `../upstream-marble/gpu/`，`sources.json` 记录下载路径和各文件 SHA256。

| 项目 | marble 实际配置 |
|---|---|
| GPU | Adreno725v1，chipid `0x07030002` |
| DT compatible | `qcom,adreno-gpu-gen7-4-0` |
| GPU MMIO | `0x03d00000`, size `0x40000` |
| CX debug | `0x03d61000`, size `0x800` |
| RSCC | `0x03d50000`, size `0x10000` |
| isense | `0x03d8b000`, size `0x2000` |
| CX misc | `0x03d9e000`, size `0x1000` |
| GPU 中断（DT SPI 编号） | 300、286；转 GIC INTID 时需加 32 |
| UBWC | mode 4 |
| KGSL Gen7_4_0 的 GMU FW 接口 | `GMU_VERSION(4,0,7)` |
| SQE 固件名 | `a730_sqe.fw` |
| GMU 固件名 | `gmu_gen70000.bin`，源码备用名 `c500_gmu.bin` |
| 安全固件名 | `a730_zap`，已从用户提供 ROM 提取 MDT/b00–b02，结构及 SHA256 已记录；2026-10-24 已实机认证及卸载 |

固件名来自 `drivers/gpu/msm/adreno-gpulist.h` 的 `adreno_gpu_core_gen7_4_0`，
不是根据产品名称猜测；GPU725 不应误选 Gen7_3_0 / a710。
固件来自电脑上的 ROM，未读取手机 Android 分区。ROM分析阶段未访问GPU；后续CX测试的寄存器写入见文末记录。

完整移植的后续验证顺序：

1. 建立可重建 XNU 的 Linux / Theos / iPhoneOS16.5 SDK 环境：已完成，源码内核及四核桌面已实机验证，见 `MARBLE-BUILD.md`；GIC 已改用源码 DT 步长。
2. 取得 marble 的 GPU 固件及其分段并锁定散列：已从用户提供的第三方移植 ROM 取得候选，见下方记录；尚未完成官方来源比对及设备认证。禁止混用 redwood 的 a660 固件。
3. 依据 marble DT 和 Gen7 源码移植 GPUCC、CX/GX 电源与 RPMh、GMU 内存布局、SMMU stream/context 和清理流程。先做分阶段诊断，逐项证明供电、时钟和 MMIO 可访问。
4. 完成 Gen7 GMU/HFI、TrustZone zap 加载、SQE、ringbuffer，先验证写入自有 RAM 页和纯色绘制；退出时必须停 GPU 并撤销指向 XNU RAM 的映射。
5. 对接现有 RedwoodGPU IOKit 接口并验证用户态 freedreno 对 A7xx 的支持，再开放 CoreAnimation 的 OpenGL ES 路径。桌面合成与 Metal 应分别验证。

在完成这些阶段前，保持 CPU 软件绘图；四核可以缓解系统服务与软件绘图争用，不能代替硬件加速。

## 用户 ROM 检查（2026-10-23）

来源：`D:\N12Turbo_移植17 4.0.4.0[27B33].zip`，5700269204 bytes。
SHA256：`1f79e3854f5b68f43c30273eb6f94a4023f7651148e25f3c488538b7b9d2f65a`。
只读解开 ZIP → zstd raw super → 校验 liblp geometry/header/tables → EROFS vendor/vendor_dlkm/odm。
未运行压缩包内脚本或工具，未访问手机。提取工具为 `tools/marble-rom-inspect.py` 和 Debian erofs-utils 1.8.6。

确认包含：

| 层级 | 包内路径 | 用途与限制 |
| --- | --- | --- |
| GPU Linux 内核驱动 | `vendor_dlkm/lib/modules/msm_kgsl.ko` | AArch64 ELF relocatable；含 Gen7_4_0 支持，不是 XNU kext |
| GPU 时钟驱动 | `gpucc-diwali.ko`、`gpucc-waipio.ko` | 包含多个平台模块；需按 marble DT compatible 选择，不能全套照搬 |
| Android OpenGL ES/EGL | `vendor/lib64/egl/libGLESv2_adreno.so`、`libEGL_adreno.so` | V@0615.95；依赖 Bionic、libgsl、libllvm-glnext 等，不能由 dyld 直接加载 |
| Android Vulkan | `vendor/lib64/hw/vulkan.adreno.so` | 同一 Qualcomm build；还依赖 Android mapper/HIDL/nativewindow；不等于 Metal 驱动 |
| SQE | `vendor/firmware/a730_sqe.fw` | 74676 bytes，SHA256 `e67d1829f57fc8326d806234b932dc3b78e28a9940df34c70752f17d5f38b7aa` |
| GMU | `vendor/firmware/gmu_gen70000.bin` | 57272 bytes，SHA256 `b2d3f0e2a98fd6259ab9ffef16df63265542e6cc9a970d53d476e823d432042a` |
| ZAP | `vendor/firmware/a730_zap.mdt`、`.b00/.b01/.b02` | MDT 4044 bytes，SHA256 `b957876634e6a9eab17ae39912ea3b4cb5ba811d0720b04ba0590e118c3cd46a`；分段分别 148/2236/3896 bytes，与 program headers 匹配 |

另含 `a730_zap.elf/.mbn`。上述 ZAP 检查只证明文件结构一致，不是 Qualcomm 签名验证，也未向 TrustZone 提交。
固件名称与已查源码一致，KGSL 模块还同时包含 `qcom,adreno-gpu-gen7-4-0`、`a730_zap`、`gmu_gen70000.bin`，因此是有依据的适配候选；尚不能证明整套固件与当前运行硬件/安全固件组合可用。

模块 vermagic 为 `5.10.160-gki-gd28eeb36ae86`，boot kernel 字符串为 `5.10.237-Fuutao-Qn_miao-source-gc28e50058`。
vendor 属性声明 device=marble、SDK32/Android12；这些是第三方包内部字段，不能用于官方来源认证，也不能只凭 vermagic 差异断言该 Android ROM 无法启动。

材料保存在 `out/n12turbo-rom/gpu-material/`，并有 `gpu-material.tar.gz`；只选取固件、三个内核模块和三个 64 位图形库，不是完整 Android 运行环境。
逐文件大小/哈希见 `gpu-files.json`，ELF/依赖/版本见 `gpu-details.txt`，ZAP 结构见 `zap-structure.json`，分区范围和 SHA256 见 `partitions.json`。
原来的触摸/四核恢复包保持原样，当前仍为 `gpu=0 gl=0 metal=0`。

## 复用项目及首批适配（2026-10-23）

搜索结果保存在 `out/gpu-project-search/`。本轮没有找到可直接用于 marble/HackiOS 的完整 GPU 驱动；
已找到可复用的实际实现，并按提交锁定。此结论只针对本轮检索范围。

| 项目 | 固定提交 | 用法 |
| --- | --- | --- |
| [NativeAdreno](https://github.com/UniXNU/NativeAdreno) | `b686b9054f57d393c80195b82568a09f8ca106a2` | 参考 BO/ring/fence/cache/GMU 生命周期；现有硬件实现面向 sheng/740，不能直接用其 A740 初始化表 |
| [AdrenoFirmware](https://github.com/UniXNU/AdrenoFirmware) | `40fdb8ea87606612ac6f1e80bbb49955d0ce4051` | 导入 GMU block parser、HFI transport/startup/votes；HFI 传输增加明确的 marble Gen7 布局，局部修改见 local-adaptations.json，尚未发送硬件命令 |
| [ArmSmmu4K](https://github.com/UniXNU/ArmSmmu4K) | `4d0d6598b1a36aeec3de01091893fa6660e49388` | 复用独立4 KiB页表、context/program/stream审计和物理页表审计；XNU的CPU页大小不变，实机阶段见下文 |
| [AdrenoPM4](https://github.com/UniXNU/AdrenoPM4) | `445f8fe6d115738352b9d78dc70af6d61aa7f62f` | 仅参考；A740 packet/state 仍须核对725 |
| [HackiOS Mesa 镜像](https://github.com/sbccc1888/mesa) | `b2f27b0042031ee2737f9936a3bd59a5d402d233` | Mesa25.0.7 已有 FD725/chipid `0x07030002` 和 A725 cmdbuf quirk；保留已有 RedwoodGPU IOKit 用户态接口 |

上游源文件及许可证在 `xnu/files/bsd/dev/marble_gpu/upstream/`，`sources.json` 记录逐文件散列。
完整参考代码缓存于 `../upstream-marble/projects/`；未执行这些仓库的安装脚本。
优先完成现有 iOS16.5 EAGL/Mesa 路径，Metal 另行验证。

新增 `marble_adreno.c`，通过 loader 的 `/arm-io/marble-gpu` 传入725芯片与 MMIO 窗口。
存在该板级节点时会阻止旧642L初始化路径进入。诊断参数严格限制为：

- `marble_gpu_probe=1`：读取 RAM APFS 内的六个固件并检查大小/SHA256，调用上游 GMU 解析器，检查 ZAP 分段结构；
  用内核独占 scratch 页生成不启用的 4 KiB 页表，不访问 GPU 硬件。
- `marble_gpu_probe=2`：完成上述检查后，只读 GPUCC 的 CX/GX GDSCR 和 AHB/GMU/HUB 时钟状态；不写 MMIO。

两阶段均不启动 GMU、不调用 TrustZone、不启用 SMMU、不创建 GPU service。
loader capability 为 `marble-gpu-preflight=readonly-v1`；控制器会拒绝旧 loader。
`marble-userspace.py --gpu-firmware PATH` 只把固定散列的文件装入 RAM 支持盘；不带该选项重建时会清除旧固件目录。

本地测试 `tools/tests/marble-gpu.c` 使用真实 ROM：15个 GMU block/4个 payload，验证截断/未映射地址/容量不足拒绝、
ZAP 分段不匹配拒绝、4 KiB 页表真实地址遍历及表池重叠/映射预算拒绝。
这些测试不代表固件已认证、GPU 已能执行命令或 SMMU 已在实机运行。

候选输出独立保存：`out/marble-gpu-loader-boot.img`、`out/linux-build/mach-marble-gpu` 及各自 JSON；
内核构建日志为 `out/linux-build/kernel-gpu.log`。原四核源码恢复包不变。

### 第一、二阶段实机结果

`20261023-230215-marble.log` 和 `20261023-230409-marble.log`：固件检查及解析成功，
生成3页4KiB页表/1个映射叶，不激活；36项检查0失败，4核在线，返回loader时三个次级核均为OFF。
无GPU硬件写入。结构化证据：`out/marble-gpu-preflight-report.json`、`out/marble-gpu-regression-report.json`。

| 第二阶段只读寄存器 | 读值 |
| --- | --- |
| GCC GPU CFG AHB (`0x00181004`) | `0x1` |
| GCC GPU MEMNOC GFX (`0x00181010`) | `0x1` |
| GCC GPU SNOC DVM GFX (`0x00181018`) | `0x80000001` |
| GPUCC CX GDSCR (`+0x9108`) | `0x22f001` |
| GPUCC GX GDSCR (`+0x905c`) | `0x22f001` |
| GPUCC AHB CBCR (`+0x911c`) | `0x0` |
| GPUCC CX GMU CBCR (`+0x913c`) | `0x80002221` |
| GPUCC HUB CX INT CBCR (`+0x9148`) | `0x80000001` |
| GPUCC HLOS1 SMMU CBCR (`+0x7000`) | `0x80000000` |

CX/GX SW_COLLAPSE=1，PWR_ON=0；GMU/HUB/SMMU的CLK_OFF置位。
marble/cape GCC CFG_AHB偏移为`0x81004`，不能用sheng/SM8550的`0x71004`。
第二阶段先检查GCC CFG_AHB的enable位，再允许读GPUCC。

### 第三阶段候选：可回滚CX测试

`marble_gpu_probe=3` 在固件/页表及第二阶段检查后，只对CX GDSCR和本地AHB/SMMU时钟vote作短暂写入。
从marble DT解析CX的hw-ctrl地址为`0x03d9953c`；时序参考锁定版本的`gdsc-regulator.c`，
与同仓库的主线`gdsc.c`是两个不同实现。此阶段不修改GX、电压、PLL、GMU固件或SMMU寄存器。

要求初始CX/GX折叠、硬件控制位关闭且无已有本地AHB/SMMU vote；不满足则跳过。
清除CX SW_COLLAPSE后限时轮询hw-ctrl PWR_ON，按retain-regs设置FF retention；
再开启本地AHB/SMMU vote，只有SMMU/GMU/HUB时钟均运行才读SMMU GR0的SCR0/IDR0/1/2/GFSR。
最后先恢复时钟vote、再恢复CX原值并延时100us，验证原控制位和原电源状态。
任何上电/时钟超时和观测失败均执行恢复，不启动GPU。

候选：`out/marble-gpu-power-loader-boot.img`、`out/linux-build/mach-marble-gpu-power`。
loader另需`marble-gpu-power=cx-restore-v1`；内核另需`MGPU-MARBLE-POWER-v1`标记。
本地`marble-gpu-power.c`验证成功/上电超时/时钟超时/观测失败时的恢复，以及已有硬件所有权时零写入。
这些模拟检查用于测试控制流，不能替代实机电源、时钟和总线验证。

第三阶段首次实机已通过：`20261023-231303-marble.log`，`powered=1 clocks=1 observed=1 restored=1 skipped=0`。
CX GDSCR从`0x22f001`恢复为`0x22f001`，hw-ctrl从`0x5ff4`恢复为`0x5ff4`，
AHB恢复`0`，SMMU时钟vote恢复`0x80000000`。36项检查0失败，4核在线并正常退出。

SMMU读值：SCR0=`0x2f0406`、IDR0=`0x4c017e18`、IDR1=`0x40000016`、IDR2=`0x5511`、GFSR=`0`。
IDR2表明支持原生4KiB页，OAS编码1为36位；因此已把离线映射的物理地址宽度从初始假设40位改为36位。
硬件报告22个context，DT另有21个context override；不能把最高bank当作可用bank。
当前没有分配context bank、stream slot或ASID，尚无GPU设备地址可用。

后续诊断追加只读SMR/S2CR扫描，并复用同一版本的`ArmSmmu4KContext`，
用实际ID寄存器编码离线context寄存器图像，不写context寄存器。
本地检查明确拒绝在这组能力上使用40位OAS。
实际GMU内存映射仍需bank/stream/ASID独占、页表cache维护、硬件TLB验证及退出清理；
再推进GMU内存分配、RPMh/RSCC、HFI启动与ZAP认证，最后才是SQE/ring/Mesa提交。

追加扫描实机日志：`20261023-231751-marble.log`。24个SMR均为0、S2CR均为`0x20000`（FAULT）；
离线context编码成功：TCR=`0x80803520`、TCR2=`0x38011`、MAIR=`0xff44`，未写入硬件。
第二次CX测试同样`powered=1 clocks=1 observed=1 restored=1`，36项检查0失败。
最终源码候选SHA256为`5ae0930ad1240c9d29355202ead17335ee03e6c25f84eb390786b408ad4c66a8`。
这证明可控上电、时钟与只读审计；尚不证明context寄存器可写、GMU可执行或GPU可绘图。


### 第四阶段：context bank / GMU 复位实机审计

`marble_gpu_probe=4` 在第三阶段的有界CX事务内，只读GR1、CB0..20及GMU复位状态。
硬件第22个bank（CB21）不访问。日志`out/20261023-233714-marble.log`：
所有21个非安全bank的SCTLR.M=0、FSR=0、CBAR=0、CBA2R=0、ACTLR=3；
GMU SYSRESET=1，FW_INIT_RESULT=0；没有ASID 0x42冲突。
CX/时钟恢复成功，36项检查0失败、四核在线、退出副核OFF。

未启用bank的SCTLR其他位、TCR/TTBR并非全零：例如CB0 SCTLR=0x3fdf2316，
TTBR0=0x7bffdffffef31f3b。这些不是有效活动映射，不能把任意非零值都判为占用，也不应将它们写回。
固定Xiaomi源码`arm_smmu_write_context_bank()`对未分配bank只写SCTLR=0；
`__arm_smmu_iova_to_phys_hard()`在ATS1PR请求前将IOVA按4KiB向下对齐。
新增参考`arm-smmu.c/.h`的路径、大小及散列记录于`../upstream-marble/gpu/sources.json`。

### 第五阶段：CB0 编程、实际页表审计、禁用及释放

复用固定版本`ArmSmmu4KProgram/Streams/Audit`原始实现，新增`marble_smmu.c`连接真实MMIO、
页表cache clean及全局/CB0故障中断。capability为`marble-gpu-context=cb0-ats-release-v2`，
内核标记`MGPU-MARBLE-CONTEXT-v2`。`marble_gpu_probe=5`要求：

- GMU复位、GX折叠、全部非安全bank禁用，ASID 0x42无冲突；24个SMR无效、S2CR全部FAULT。
- 实际页表图像只包含内核自有RAM的一页映射，物理池和数据不重叠；仅使用CB0。
- 设置并读回DT指定ACTLR=0x32b，按上游顺序编程原生4KiB context，验证实际寄存器和实际物理页表。
- 禁用context、验证TLBSYNC完成、无ATS在途及已定义故障位/IRQ故障，清零TTBR0/1，恢复ACTLR。
  保留禁用的stage1/AArch64配置，TCR/MAIR保留初始化值；不恢复上电后未初始化的寄存器内容。
  CX和本地时钟恢复原状态。

MMIO白名单禁止全局寄存器、SMR/S2CR和其他bank写入；本阶段始终不挂接设备stream、不执行GMU。
程序入口及底层ATS写入回调都检查IDR0.ATOSNS。marble IDR0=0x4c017e18的bit26=1，
含义是“不支持标准地址翻译操作”，因此本机必须跳过ATS1PR；不以软件页表遍历冒充硬件翻译。
即使context成功启用/读回，也不能当作GPU DMA/渲染成功。

首次失败记录为`out/20261023-234405-marble.log`：
未检查ATOSNS就尝试标准ATS，返回PAR=0，没有证明有效翻译；恢复CBAR=0时发生external data abort。
CBAR=0选择stage2 translation，而实际IDR0没有S2TS。该失败候选已单独保存并标记FAILED，控制器拒绝再次运行。
异常后已重启手机回Android，再临时启动修正版；没有在不确定的RAM状态上继续测试。

修正版连续两轮实机日志：`out/20261023-235016-marble.log`、`out/20261023-235049-marble.log`。
两轮均为program status=0/enabled=1，实际页表审计1536次读取/1个叶映射；
disable verified=1、tlb_sync=1、retain=0；结果passed=1、translation=-1（未测试）、released=1、fault_irq=0。
第二轮起始CB0为SCTLR=0、TTBR0=0、CBAR=0x1f300、CBA2R=1，确认上轮释放状态。
FSR原始值0x400不在锁定Xiaomi头文件的已定义fault mask 0xc00001fe内；该值原样保留在日志中，未称FSR全零。
两轮CX/时钟恢复成功、36项检查0失败、4核在线、退出副核均OFF。

候选内核SHA256为`cc823a81a89d0a5531887b8623edf3360a36c78ebe85e179ebbecddfeb3a3872`，
文件`out/linux-build/mach-marble-gpu-context`；loader `out/marble-gpu-context-loader-boot.img`
SHA256为`e399d7b79eb05514117b7c9d5ce1d2aadfaad3cbbc84b49acfee0dbd327feeff`。各有独立JSON。

本地测试：编程/禁用I/O fixture通过86项读写失败控制及超时/拒绝/只读位检查；
x86主机仅省略直接AArch64 MMIO适配器子测，不用替身冒充该子测通过。
物理页表审计fixture通过1632种配置、9704项描述符修改和8192次读取失败控制。
写入边界测试验证全局、stream、其他bank、错误宽度及越界写入均被拒绝，并检查真实marble IDR0不能使用ATS。

### 后续：Qualcomm TBU/ECATS，再推进 GMU

同一版Xiaomi的`arm-smmu-qcom.c`通过`qsmmuv500_iova_to_phys_hard()`调用TBU ECATS。
已缓存该源文件及`arm-smmu-impl.c`，散列加入参考清单，尚未把其硬件路径接入XNU。
Mu DT对应`qcom,qsmmu-v500`/`qcom,adreno-smmu`：

| 区域 | 地址 | 用途 |
| --- | --- | --- |
| TCU HLOS1 | 0x03de6000 / 0x20 | 实现版本及共享状态 |
| gfx_0 TBU | 0x03de9000 / 0x1000 | SID 0..0x3ff，GMU SID5对应此TBU |
| gfx_0 status | 0x03de6200 / 8 | 独立状态窗口 |
| gfx_1 TBU | 0x03ded000 / 0x1000 | SID 0x400..0x7ff |

下一步先审计TBU/TCU状态和所有权，再复用stream挂接/隔离库，为SID5建立可回收映射；
在GMU保持复位时用ECATS验证真实硬件翻译，包含负向地址测试及故障清理。
这些完成后再做GMU固件内存布局、RPMh/RSCC/HFI启动、ZAP认证、SQE/ring及Mesa提交。
当前没有GPU加速，桌面继续软件渲染。

### 2026-10-24：第六阶段 TBU 只读实机结果

`out/20261024-080313-marble.log`确认 gfx0 TBU 空闲：HALT=0x80182000、
VA/trigger/PAR/AXUSER=0、ACK=0x100000；TCU version=0x1000000、external status=1。
HALT 的背景位必须保留，ACK 仅检查低两位，不写 ACK，也不把整个 HALT 清零。
CX/时钟 powered=1/clocks=1/observed=1/restored=1，36项检查0失败。
内核 `mach-marble-gpu-tbu` SHA256 `03cc9dff8131c6476a953e9f396118aae87d038fa0c45390768ca0c8d2342f64`；
loader `marble-gpu-tbu-loader-boot.img` SHA256 `fd2722a1924b98049c277d5f7dcf1f36c76487b6275cc0059d75d303af779dba`。

第七阶段候选接入固定上游 stream attach/fault 库，仅准许 stage7 显式 lease 下
slot0 的精确 SID5 -> CB0 路由；MMIO 拒绝 BYPASS、其他 SID/slot/global 写入。
GMU保持复位，测试映射页首、页尾、未映射地址及负向之后的正向恢复。
`marble_ecats.h` 依照锁定 Xiaomi arm_tbu_impl 协议保留背景位，有界轮询，
清除 trigger 后验证停止、恢复 debug 字段和 halt；清理不明即保留 RAM 并要求重启。
本地主机 fixture 只验证控制流、故障注入及写入边界，不代表实机硬件翻译成功。

### 第七阶段：SID5/CB0 与 ECATS 实机翻译通过

独立候选 `out/linux-build/mach-marble-gpu-ecats` SHA256
`d114a00c63fca1d79c00a4674525c6b26720a62253e5f40289393f0256d0501c`。
构建完成编译和链接；make status=2 仅包含四项已知导出检查失败
（check_all_exports、IOKit/Libkern/Private.symbolset），不称全部构建目标通过。

连续两轮日志 `out/20261024-080912-marble.log`、`out/20261024-080940-marble.log`：

| 请求 | 实际结果 |
| --- | --- |
| 0x60000000 | PAR=0xff00843eac000a82，PA=0x843eac000，与自有数据页一致 |
| 0x60000fff | PAR=0xff00843eacfffa82，PA=0x843eacfff，与页尾一致 |
| 0x60001000（未映射） | PAR=0x803，FSR=0x402；观察并只清除本次 TF 位0x2 |
| 负向后的0x60000000 | 首次即恢复正确PA，无需上游quirk重试 |

每请求 complete=1/restored=1/retain=0，HALT 恢复0x80182000。
SCTLR 实际读回值在关闭 CFIE/CFCFG 后按原值恢复；SID5 经上游 FaultStreams
隔离后 SMR0 清零并读回，全部24个 slot 恢复 invalid/FAULT。
context passed=1/translation=1/released=1/fault_irq=0；disable verified=1/tlb_sync=1/retain=0。
FSR 背景0x400保留，GFSR=0；CX/时钟 restored=1。
两轮各36项检查0失败、四核在线、副核退出OFF。

第三轮携带同样探测启动桌面，日志 `out/20261024-080959-marble-live.log`；
触摸桥ready=1，SpringBoard/backboardd启动。用户本轮确认“触摸、显示正常，无异常发热”。
本阶段证明的是 SMMU/TBU 硬件翻译与路由/故障生命周期；GMU没有执行固件，
GPU没有读写数据缓冲区或提交渲染，桌面仍使用CPU渲染。

### 下一阶段内存布局核对（尚未分配或激活）

现有 `AdrenoFirmwarePlanGmu` 已解析15个块：11个metadata、4个payload。
固定 Xiaomi `gen7_gmu_parse_fw/gen7_gmu_process_prealloc` 对type1/type7 metadata
按 `value` 预分配，TCM区域跳过系统RAM分配。下表为当前固件明确声明的系统RAM需求，
不包含随后动态分配的GMU日志、HFI和页表池：

| IOVA | 有效字节数 | 按4KiB对齐的分配量 |
| --- | --- | --- |
| 0x00004000 | 0xdcac | 0xe000，14页 |
| 0x00100000 | 0x88b0 | 0x9000，9页 |
| 0x00200000 | 0x12a80 | 0x13000，19页 |
| 0x00300000 | 0x6e28 | 0x7000，7页 |
| 0x60000000 | 0x1000 | 1页 |
| 0x60001000 | 0x1000 | 1页 |

合计51个4KiB叶页（208896字节）；XNU主机页为16KiB，实际分配还须按主机粒度处理。
32位IOVA原生4KiB三级表对这些地址至少需要6个页表页；当前4页池只用于旧单页探测，不能照搬。
下一步复用 `ArmSmmu4K` 的权限/表构建和物理审计，先做更大RAM映射及ECATS边界检查，
再把固件payload装入正确RAM/TCM。固定Xiaomi预分配使用READ|WRITE|PRIV，
需要明确处理指令/特权访问和ECATS AXPROT，不能拿当前unprivileged单页测试当作执行验证。

`NativeAdrenoGmuMemory` 的固定icache/dummy/debug/log/HFI布局面向其已验证启动路径，
不足以直接覆盖本固件的六段metadata；可复用其分配/映射生命周期思路，
地址、大小、OAS=36和Gen7初始化仍须以marble资料为准。

### 第八阶段：完整固件 RAM 预分配与权限实机通过

候选内核 `out/linux-build/mach-marble-gmu-memory` SHA256
`9b67d6e4cd157c05bdeb6626f1463e80881edfe421cc8a9568ebbd828cacddca`；
loader `out/marble-gmu-memory-loader-boot.img` SHA256
`de34295bb6041597fde9555b363925578b7e17c7cd97cb1f3531d3c342168e45`。
新loader通过 `marble-gpu-memory=six-ranges-v1` 宣告第8阶段策略；仍不接受第9阶段。

`marble_gmu_memory.h` 接受固定上游解析后的块清单，依据锁定Xiaomi预分配规则
跳过TCM，生成六段系统RAM范围。拒绝不支持的metadata类型、零大小/越界/重叠预分配、
缺失payload backing、容量不足和36位OAS溢出。实际 backing 为内核自有、16KiB对齐
的0x34000字节静态区域，逐4KiB验证真实物理连续性；映射其中0x33000字节（51页）。
本轮只复制1个RAM payload（0xdcac字节），三段TCM payload不写入硬件。
RAM清零后复制固件、SHA256记录、cache clean；测试结束后核对完整RAM镜像未变化。
8页页表池中实际使用6页，物理图审计读取3072个描述符，确认51个叶页且无额外映射。

缓存区明确设置 READ|WRITE|EXECUTE|PRIV，非缓存区设置 READ|WRITE|PRIV 和执行禁止。
这描述的是设备stage1权限；ECATS使用实际AXPROT发起请求，不是修改CPU页权限。
当前显式WB/WA、inner-shareable属性只用于此内存/翻译诊断；未来真实GMU DMA的
缓存一致性和同步仍需独立验证，不能用本轮地址翻译成功代替。

实机日志 `out/20261024-082401-marble.log`、`out/20261024-082424-marble.log`：

| 每轮检查 | 结果 |
| --- | --- |
| 51页逐页页首/页尾 | 102次特权数据读翻译，PA全部匹配 |
| 每段特权数据写翻译 | 6次通过；ECATS不会执行真正的内存写入 |
| 每段特权指令翻译 | 4段缓存区通过；2段非缓存区按预期PF拒绝 |
| 每段非特权数据读翻译 | 6次均按预期PF拒绝 |
| 5个尾部空洞及IOVA0 | 6次均按预期TF拒绝；TCM IOVA没有伪装成系统RAM |
| 负向后正向恢复 | 每段恢复成功，最后额外回测首段成功 |
| 总请求 | 每轮133次，quirk重试0次 |
| 资源释放 | context passed=1/translation=1/released=1/fault_irq=0，SMMU写入45次 |
| 故障与电源 | PF/TF清除后FSR背景0x400，GFSR0，TBU原值恢复，CX/时钟恢复 |
| RAM与CPU回归 | RAM_image_intact=1；36项检查0失败、四核在线、副核退出OFF |

`tools/marble-gmu-memory-report.py` 从每轮原始记录独立检查所有页边界、权限、故障类型、
实际PAR/PA及清理证据，拒绝拿第7阶段单页日志冒充完整内存测试。
主机fixture同时验证真实固件的布局、旧4页页表池拒绝、3162个叶描述符修改及metadata失败。
ECATS主机fixture新增AXPROT/访问标志检查，保留64个I/O故障注入及清理超时检查。

第三轮同候选桌面启动，用户确认“触摸、显示正常，无异常发热”；这不是温度传感器测量。
GMU仍保持SYSRESET=1，没有执行固件，没有GPU DMA或渲染加速。
构建编译/链接完成，仍有四项既存导出检查失败，详见 `kernel-gmu-memory.log`。

### TCM 装载与冷启动所需资源

固定 `gen7_gmu_load_fw/load_tcm` 使用DWORD寄存器偏移。
`GEN7_GMU_CM3_ITCM_START=0x1b400`、`DTCM_START=0x1c400`；
换算为当前GMU窗口（物理0x03d68000，对应GPU DWORD0x1a000）的候选写入范围：

| 固件GMU地址 | 有效载荷字节 | GMU窗口偏移 | 物理地址 |
| --- | --- | --- | --- |
| 0x00000000 | 0x1dc | 0x5000 | 0x03d6d000 |
| 0x10007f98 | 0x28 | 0xcf98 | 0x03d74f98 |
| 0x10007fdc | 0x18 | 0xcfdc | 0x03d74fdc |

以上为锁定源码/窗口推导，现已按下述第九／十阶段完成TCM读写实机验证。
先在GMU复位状态读取TCM，明确所有权和可恢复快照，再用精确白名单装载、读回、恢复；
任何恢复不明都须整机重启。
不能直接套用原redwood/A642L的GMU寄存器偏移。

完整冷启动随后还需要动态GMU日志/HFI分配、实际GMU/HUB时钟频率和电源/带宽票据、
Gen7 register_config/中断配置、GMU device_start、PDC/RSC及HFI的真实响应。
顺序参照固定Xiaomi `gen7_gmu_first_boot`，复用现有AdrenoHfi/AdrenoHfiStartup传输和编码；
板级票据及地址不得猜测，超时不合成ACK，未经真实复位不可回收GMU可访问的RAM。

### 第九／十阶段：复位状态 TCM 事务实现

新增 `marble_gmu_tcm.h`，继续使用固定 `AdrenoFirmwarePlanGmu` 解析结果。
仅接受本固件的三段TCM载荷，合计135个32位字。TCM读白名单为GMU窗口
0x5000..0xcfff，两块各16KiB；写白名单精确到三个载荷范围，仅stage10持有事务时允许。
其他宽度、复位/时钟/中断寄存器均拒绝。

stage9用内核自有32KiB BSS快照完整读取两次，要求逐字一致、写入次数为0。
stage10先作相同检查，再写135字，逐字检查完整32KiB（含未修改区域），
逆序恢复所有已尝试写入的位置（失败回调也可能已到达硬件），最后完整读回原值。
每批读取及每个写入检查SYSRESET=1、FW_INIT_RESULT=0、CX开且GX关闭。
失去该条件后禁止继续恢复；恢复无法完整证实时保留RAM并panic，必须整机冷重启。
TCM操作位于原有SMMU/stream lease成功释放之后、CX电源事务恢复之前。

真实固件主机fixture通过精确地址/宽度/阶段围栏、只读及完整读回/恢复；
1345个故障场景覆盖每个装载和恢复写入的交付前/后失败、读回损坏、barrier及guard失败。
这是软件验证，实机结果另行记录。新loader capability为`marble-gpu-tcm=restore-v1`，
允许stage9/10、拒绝stage11。`tools/marble-gmu-tcm-report.py`同时验证原有133条
ECATS请求及释放/电源恢复，拒绝只有stage8记录或缺少完整TCM读回/恢复证据的日志。

### 第九／十阶段实机结果（2026-10-24）

候选内核 `out/linux-build/mach-marble-gmu-tcm` SHA256
`3e6a8aa60255eef083bd71a09f495006d6539ee5dbcd1c991c748a1ae9057378`；
临时loader `out/marble-gmu-tcm-loader-boot.img` SHA256
`2d6e8be6b35d6c9a0d52bc4841287b72817fe57a0777c1c10811e3404aa88727`。

| 实机日志 | TCM结果 |
| --- | --- |
| `out/20261024-083811-marble.log` | stage9：16384次32位读取，完整32KiB两次一致，写入0 |
| `out/20261024-083835-marble.log` | stage10：32768次读取；135次固件写入＋135次原值恢复；loaded=1/restored=1/retain=0 |
| `out/20261024-083853-marble.log` | 重复stage10：同样通过完整装载、读回、恢复和原值核对 |

每轮均通过原有133条ECATS请求、51页权限/边界检查、SMMU释放和CX/时钟恢复；
RAM固件镜像未变化、36项系统检查0失败、4核在线且退出后副核均OFF。
`out/marble-gmu-tcm-report.json`逐项验证原始测量，全部通过。
CX在轮次之间关闭，TCM上电初始内容确实不同；只要求同一轮两次读取稳定，
以及恢复后完整32KiB与该轮原快照一致，不把跨电源周期的未初始化内容一致当作前提。

随后同候选stage10启动桌面，`out/20261024-084034-marble-live.log`保留启动和输入记录；
用户回答本轮轻点/滑页/长按、显示及异常发热回归“正常”。该反馈不等同温度传感器测量。
恢复包为`../marble-baselines/20261024-gmu-tcm`。仍未释放GMU复位、无固件执行、
无实际GPU DMA、无GPU渲染加速；桌面仍用CPU渲染。

### 第十一阶段：GMU 日志与 HFI 资源

进一步锁定并保存同版 Xiaomi 的 `adreno_hfi.h`、`adreno_gen7_hfi.h`、
`adreno_gen7_gmu.h` 和 `kgsl_gmu_core.h`，SHA256及URL在GPU引用的sources.json中。
`GMU_LOG_SIZE=SZ_16K`；非hwsched路径使用3个队列、每队列4KiB。
队列表仍为17个header保留空间，完整表长210个DWORD（840字节），按16字节对齐后
三个环形队列起点为0x350、0x1350、0x2350；分配整体按4KiB取整为16KiB。
命令／响应优先级10，调试队列优先级40，header中的unused字段清零。

这与已导入UniXNU传输的sheng布局不同。新增显式
`AdrenoHfiInitializeStoppedProfile(..., AdrenoHfiProfileMarbleGen7)`，
原初始化API保留原默认布局。复用已有发送／解析／超时隔离逻辑，按profile选择队列偏移和type。
源pin及原始hash保留在upstream/sources.json，修改后的hash和局部patch位置单列在
upstream/local-adaptations.json，patch为`xnu/marble-hfi-profile.patch`。
启动编码器／电源票据库未修改，本阶段也不调用它们发送命令。

`marble_gmu_resources.h`按小米`gen7_gmu_init`顺序保留固件六段metadata映射，
然后在0x60000000..0x7fffffff中first-fit分配日志、再分配HFI。
本固件得到日志IOVA=0x60002000、HFI IOVA=0x60006000，各16KiB；
GENERAL_8的候选编码=0x60002003，仅计算、不写寄存器。
两个缓冲区使用独立16KiB对齐的内核BSS，逐4KiB验证物理连续性，拒绝彼此及固件backing重叠。
总计8段／59个叶页，仍使用6个页表页。

HFI只初始化并独立审计完整空镜像，日志清零；保存两个缓冲区的联合SHA256并clean cache。
设备映射使用与前阶段一致的WB/WA、inner-shareable；日志/HFI为RW|PRIV且NX。
这仍是翻译诊断配置，不证明实际GMU DMA一致性。
stage11对TCM只作stage9的稳定性读回，不写TCM；GMU始终复位。
loader新增`marble-gpu-resources=log-hfi-v1`，允许stage11、拒绝stage12。

`tools/marble-hfi-oracle.py`核对引用hash，从锁定小米源码原样提取`init_queues`和结构／宏定义，
在测试中独立生成期望值。生成的GPL代码仅在用户态fixture中编译，不进入内核。
完整16KiB初始化镜像逐字节一致；AArch64 QEMU fixture通过3658个页表bit修改、
4096个队列镜像修改、布局／物理重叠拒绝及两个profile的wrap／ACK／timeout／poison测试。
另运行原上游HFI测试（510个wrap、4100个序号交换）和startup测试（66个逐阶段失败），均通过。
这些ACK是测试数据，不是实机GMU响应。

第十一阶段候选`out/linux-build/mach-marble-gmu-resources` SHA256：
`805f9c525f1d583d5a836fd8183070cb4506ac890091f924a72ee416f3db0e8f`。
配套loader `out/marble-gmu-resources-loader-boot.img` SHA256：
`8cb7ee40ae79d63a43e5db1f2c72f7d7931331866406b53b45371c3119288b1c`。
编译／链接完成；四项既存导出列表检查失败保留在`kernel-gmu-resources.log`中。

两轮实机日志`out/20261024-085509-marble.log`和`out/20261024-085522-marble.log`均通过：

| 检查 | 每轮实测 |
| --- | --- |
| 完整物理页表审计 | 3072个描述符，6个页表页／59个叶页 |
| 59页页首／页尾特权数据读翻译 | 118次通过，真实PAR与预期PA一致 |
| 8段特权写翻译 | 8次通过，不执行实际内存写入 |
| 8段指令翻译 | 4段固件缓存区通过，4段非缓存区／辅助缓冲区按NX拒绝 |
| 非特权数据读 | 8次均按PRIV拒绝 |
| 空洞／IOVA0 | 6次按预期拒绝 |
| 负向后正向恢复 | 8次每段恢复，额外首段回测1次 |
| ECATS总数 | 157次，quirk重试0次 |
| 资源内容 | HFI空镜像完整、日志／HFI联合SHA不变，notify/pending/sequence/poisoned全0 |
| TCM回归 | 完整32KiB两次读取一致，16384次读、0写 |
| 清理 | CB0／SID5释放，CX与时钟恢复，固件RAM镜像未变 |
| 系统回归 | 36项检查0失败，4核在线，退出后副核OFF |

`tools/marble-gmu-resources-report.py`独立检查59页权限／边界及全部生命周期证据；
拒绝前阶段日志及11种被篡改的队列、通知、复位、内存、页表和电源记录。
同候选stage11桌面已启动，本轮用户对轻点／滑页／长按、显示及异常发热回归回答“正常”。
`out/20261024-085718-marble-live.log`保存桌面和输入记录，系统／触摸统计在
`out/marble-gmu-resources-regression-report.json`中；用户反馈不等同温度传感器测量。
独立恢复包为`../marble-baselines/20261024-gmu-resources`。
GMU仍未执行，未做真实DMA或HFI响应测试。

### 第十二阶段：GMU 时钟与 command DB 只读诊断

新增 `marble_gmu_preflight.h` 与 stage12。保留 stage11 的8段／59页资源、
157次ECATS、只读TCM回归；在CX事务内、SID5／CB0释放之后观察时钟和数据库。
GMU始终保持SYSRESET=1、FW_INIT_RESULT=0，GX关闭。新接口没有时钟写入、
RPMh／AOP／ICC请求或GMU复位释放入口。

锁定同一小米commit的`clk-rcg2.c`、`clk-regmap-divider.c`、`cmd-db.c`、
`waipio.c`、`icc-rpmh.c`、`bcm-voter.c`、`rpmh-regulator.c`、
`adreno_gen7_rpmh.c`和相关头文件，逐个验证SHA256。GPUCC实际compatible为
`qcom,cape-gpucc`；小米默认GMU启动档200MHz，HUB 150MHz。GMU RCG位于
GPUCC+0x9318／CFG+0x931c，HUB位于+0x93ec／CFG+0x93f0；5位HID分频，
源选择为CFG[10:8]，AHB／CX只读分频位于+0x9430／+0x942c。

时钟接口只允许8个明确的32位寄存器读取，无写回调；两次快照前后检查电源／复位。
两轮实测RCG CMD、CFG和两个分频寄存器均为0；GMU CBCR=0x2221、HUB CX INT=1。
按表解释为XO／19.2MHz、分频1。这是寄存器的名义配置，不是有效频率测量，
也没有证明GMU已经运行。未知选择保留为未知，不猜测频率。

设备树的`/reserved-memory/aop_cmd_db_region@80860000`给出128KiB、no-map的固件RAM。
使用现有uncached memory映射API，只读两次完整镜像，共65536次32位读取。
两轮均稳定、version=1、standalone=0，完整镜像SHA256为
`b29d23b6126ad89b5c88d3a42c53c244d6a8e2e1983812511a8cf10c672a8c35`。
Linux不使用的checksum字段未作认证；数据地址只用于记录，不转换成设备写操作。

初版解析器误设分类号唯一，实机合法数据库被拒绝，原始失败记录保留为
`out/20261024-091501-marble.log`和诊断记录，不能记作stage12通过。
真实目录包含24项ARC、另一个12项ARC元数据、29项BCM及73项VRM。
第二ARC段有地址为零的`cx.mol`元数据。修正版按小米驱动遍历所有段，
检查全部范围，对实际查询结果检查地址／类型；拒绝重复资源名称和越界，不将元数据当可写资源。

| 本机资源 | command DB地址 | 辅助数据 |
| --- | --- | --- |
| gfx.lvl | 0x30050 | 32字节电源档位表 |
| cx.lvl | 0x30000 | 32字节电源档位表 |
| mx.lvl | 0x30010 | 32字节电源档位表 |
| mxc.lvl | 0x300a0 | 32字节电源档位表 |
| CN0 | 0x50038 | unit=140000、width=4、vcd=5 |
| SH0 | 0x50004 | unit=4200000、width=64、vcd=1 |
| MC0 | 0x50000 | unit=3200000、width=16、vcd=0 |
| ACV | 0x50058 | unit=1、width=0、vcd=11，固定票据类别 |

复用UniXNU的`AdrenoHfiBcmAux`解码这4项，不发送票据。不能把ACV的width=0
当成普通可变带宽计算；小米驱动明确将其标记fixed。

软件验证包括4096次DB逐bit变异、截断／溢出／重复资源拒绝、合法多ARC段及地址零元数据，
16个时钟读取失败、8个寄存器变化、前后guard丢失与完整只读寄存器边界。
Linux ASan/UBSan、真实AArch64编译＋QEMU及Windows原生fixture均通过。
`marble-gmu-preflight-report.py`保留stage11全部检查，另检查原始时钟与8项资源／BCM解码；
拒绝前阶段／不完整日志及18种篡改或重复记录。

实测候选`out/linux-build/mach-marble-gmu-preflight-v2` SHA256：
`fb5545d2b30b9539506713fcd3fb285610dc19cdb0d8acdb9bc1c53f864213b3`。
配套`out/marble-gmu-preflight-loader-boot.img` SHA256：
`8d556d7643b128fadda72753909d14fa56fe0723c1a937a97ea7d22f733cad09`。
loader新增`marble-gmu-preflight=clocks-cmddb-readonly-v1`，允许stage12、拒绝stage13。
构建／链接完成，仍有四项既存导出列表检查失败，详见`kernel-gmu-preflight-v2.log`。

两轮日志为`out/20261024-091753-marble.log`、`out/20261024-091806-marble.log`。
两轮各157次ECATS、TCM 16384次只读、36项系统检查0失败、四核及退出后副核OFF均通过；
资源镜像未变、context／电源恢复成功。桌面日志为`out/20261024-092209-marble-live.log`；
用户确认触摸／显示正常、无异常发热。触摸桥本次快照delivered=2780、bad=0，
记录样本release lag p50=2ms／p95=4ms／max=4ms；不含传感器延迟，也没有温度实测。

### 启动前必须适配的票据差异

`tools/marble-rpmh-oracle.py`从锁定的`adreno_gen7_rpmh.c`提取原始
`setup_volt_dependency_tbl`、`tcs_cmd_data`及BCM目录，从`tcs.h`提取原始编码宏。
生成GPL代码仅进入用户态fixture，不进入XNU。用两轮实机读出的BCM数据，在小米默认
初始电源档bus_min=1对应的781250KB/s计算，结果如下；全程硬件请求0。

| DDR票据顺序 | 小米Gen7结果 | 现有通用库结果 |
| --- | --- | --- |
| SH0 | 0x600002e8 | 0x60ba02e8 |
| MC0 | 0x600003d0 | 0x60f403d0 |
| ACV | 0x60000008 | 0x60000000 |

目标驱动为ab=0，只填peak/Y；通用库把同一个值同时填入average/X与peak/Y。
目标固定ACV在非零带宽时要求0x8；通用库默认未设置该位。
另外在一个明确标注的稀疏合成电源表中，目标按主电源最终取整档位寻找依赖，
通用库按原始请求档位寻找，得到0x400201与0x400101；这不是本机电源表触发该差异的证据。
保留原库行为，后续应增加明确Gen7 profile并与小米源码对齐，再验证完整HFI启动格式。

对比fixture只比较ab=0的peak路径。锁定源目录没有初始化channels，测试将不参与peak
计算的average分母channels设为1以避免0/0；没有验证average带宽政策。
在此明确条件下，启用halt-on-error的ASan/UBSan与AArch64 QEMU均确认上述差异；
结果在`out/linux-build/marble-rpmh-compatibility*.json`，不属于硬件票据生效证明。
GMU尚未执行，真实DMA／HFI响应／GPU渲染仍未验证。恢复包为
`../marble-baselines/20261024-gmu-preflight`。

### 第十三阶段：Gen7 候选电压／带宽表的 CPU 编码

在 UniXNU AdrenoFirmware 的 `40fdb8ea87606612ac6f1e80bbb49955d0ce4051`
基础上增加显式 `AdrenoHfiProfileMarbleGen7`，保留原 Sheng 默认行为。
`xnu/marble-gen7-votes.patch` 与 `upstream/local-adaptations.json` 记录补丁和哈希。
电压依赖按最终取整后的主电源档位计算；GPU使用gfx+mx、GMU使用cx+mx，
额外GPU CX票据按本机cx表查找。带宽使用SH0／MC0／ACV及实际CN0辅助数据，
只计算ab=0的peak/Y，非零ACV固定0x8；commit／wait按相邻VCD分组。

`marble-gmu-catalog.py`验证Mu设备树SHA256后提取7个GPU候选档和12档DDR带宽。
加off档共8个GPU条目；GMU采用小米默认off／200MHz／500MHz共3个条目。
这些是候选目录，未完成speed-bin／OPP筛选，不能作为已启用频率或电压策略。
`marble_gmu_votes.h`只生成CPU内存表，没有MMIO、队列通知或启动回调。
Marble profile只允许编码performance／bandwidth；ACD、IFPC和其它启动阶段拒绝。
原`AdrenoHfiStartupRun`在任何回调前拒绝Marble transport，避免误用Sheng的启动顺序。
Gen7尚缺FW版本协商、完整电源／时钟、CORE_FW_START及默认DCVS／带宽请求等适配。

stage13先执行stage12全部检查，再从真实稳定command DB快照生成两张表：
performance消息ID4、58个payload字；bandwidth消息ID3、159个payload字。
日志记录全部217个字及各自SHA256，主机报告与锁定小米原始函数生成的结果逐字节比较。
原始GPL函数仅进入用户态测试，不编入XNU。对照继续限定ab=0；测试把不参与peak
计算的average分母channels设为1，避免上游目录未初始化channels导致0/0。

本地验证：ASan／UBSan与AArch64 QEMU均通过；1024组ARC＋1024组BCM原始源码
差分、4096次DB变异、截断／坏元数据／容量／profile拒绝及输出原子性；
AArch64 HFI transport仅在合成RAM队列验证59／160字包长，无设备或真实ACK。
原始默认profile回归包括510个队列环绕案例、4100次序列交换、4种启动配置、
66种阶段失败，以及1000组ARC＋1000组BCM与未改动原库的差分。

本轮不发送HFI／RPMh／AOP／ICC请求，不写TCM或释放GMU复位。
仍为CPU渲染，不能把候选表编码通过解释为GPU已工作。

两轮完整CLI实测日志：`out/20261024-185937-marble.log`与
`out/20261024-190021-marble.log`；`out/marble-gmu-votes-report.json`确认全部217字
与原始小米函数输出相同，候选表未入队，通知／pending／sequence均0，复位未释放。
每轮保留157次ECATS、TCM 16384次只读、36项系统检查0失败、四核及退出后副核OFF，
context／电源恢复、资源完整性通过。报告器另拒绝217个逐字篡改、9种边界篡改及缺失／重复记录。

桌面日志`out/20261024-190216-marble-live.log`及
`out/marble-gmu-votes-regression-report.json`记录触摸桥delivered=553、bad=0，
8个release-lag样本p50=2ms、p95／max=6ms；不含物理传感器延迟。
用户确认触摸／显示正常、无异常发热；这不是温度传感器实测。
候选内核`out/linux-build/mach-marble-gmu-votes`的SHA256为
`c4f045a742477cc914e9d1a6affef3deef339c800ec856456fe27683ae5b59ad`；
loader启动镜像SHA256为
`c06b2670540656728f8bef2d2f76073720536d9b776aa7bb529745ef4817faad`。
构建／链接成功，四项既存导出检查失败保留在`linux-build/kernel-gmu-votes.log`。
独立检查点为`../marble-baselines/20261024-gmu-votes`。下一步是GMU时钟／供电和
完整Gen7启动顺序；还未验证真实HFI响应、DMA一致性或GPU绘制。



修正版桌面日志`out/20261023-235248-marble-live.log`确认SpringBoard/backboardd启动，
触摸桥ready=1；本轮快照没有收到手动触摸样本，因此不将它记作新一轮人工触摸验证。
桌面使用四核/双显示拷贝线程，仍为CPU渲染，本次会话900秒后自动返回loader。
检查点为`../marble-baselines/20261023-gpu-context`；系统RAM镜像引用原源码恢复包，独立记录SHA256。


## Stage14：实测 GMU/HUB 时钟及无人值守桌面回归（2026-10-24）

`MGPU-MARBLE-GMU-CLOCKS-v1` 使用 marble 的 GPLL0 DIV vote，配置 GMU
200MHz 与 HUB 150MHz。GMU 始终保持复位，GX 始终关闭；未请求电压或带宽。
保存13项配置，短/长窗口硬件计数后恢复根时钟、调试通路和父时钟票据。
硬件状态位与配置位分开核对，恢复失败则保留资源并要求冷重启。

连续两轮实机：`20261024-191854-marble.log`、`20261024-191922-marble.log`。
首轮计数：GMU short=42699/full=1365367，199999810Hz；
HUB short=32026/full=1024028，150000315Hz。
两轮 configured=1、measured=1、restored=1、retain=0、reset_released=0。
`out/marble-gmu-clocks-report.json` 核对原始计数、频率与全部配置恢复。
ASan/UBSan、AArch64/QEMU覆盖读写故障、超时、复位保护丢失和MMIO范围。
报告负测覆盖缺项、重复、篡改计数、篡改状态和旧阶段日志。

新增显式 `oem touch-test`，仅在内核运行且真实控制器就绪时向现有RAM
mailbox发布归一化坐标，拒绝非法序列、越界及与真实触点冲突。
`tools/marble-ui-regression.py` 自动执行轻点、滑动、长按并取实际设备帧缓冲。
`out/ui-gmu-clocks/contact-sheet.jpg` 已核对桌面翻页、设置打开/滚动、
底部返回和正常长按菜单/关闭。日志显示 Goodix ready=1、bad=0，HID分发成功。
这验证 mailbox→bridge→HID→UI，不替代实体手指测试；截图不等同光学扫描输出测量。

**GPU加速仍未启用。** 测得时钟并不能证明供电、电压、GMU DMA、一致性或HFI应答。
下一步建立RPMh请求及恢复所有权，再在保留SMMU映射下启动真正的Gen7 GMU。


## Stage15/16：实际读取 ARC/BCM 请求（2026-10-24）

APSS DRV2/TCS0 0x17a20d00 发出 READ/RES_REQ/8-byte（MSGID 0x108），
不写 rail。每资源重复两次、恢复7配置。首版错误屏蔽 IRQ_ENABLE，导致完成状态
无法观察，已标记失败并通过 Android 完整启动清理；修正版保留 bit0。
两轮日志 194029/194055：cx.lvl index5、mx.lvl index4（本机 DB level256），
gfx/mxc 均off，status=0x10101，restored=1、retain=0。

Stage16 再读取 CN0/SH0/MC0/ACV：0x20000001、0x20281d92、
0x2034a6d0、0x20000008。逐独立 peak/average/VALID 和 ACV mask 比较，
均覆盖锁定小米原函数编码的最小带宽档；CX/MX 也覆盖 GMU200MHz 的 level64。
这是现有 APSS 请求充足性，不是电压测量，也没有新增硬件票据。
两轮日志 195051/195116，报告 marble-gmu-dependencies-report.json PASS。

195321 桌面日志及 ui-gmu-dependencies 截图核对设置打开/滚动/返回；
Goodix ready=1 bad=0，HID ok=1。合成输入与帧缓冲观察的局限同 Stage14。
可恢复快照 ../marble-baselines/20261024-gmu-dependencies，含源代码归档、
内核/loader/报告/截图及 SHA256 manifest。GPU 加速仍未启用。


## Stage17 开发：配置恢复通过，固件启动仍失败

保持 SID5/CB0 和实测200/150MHz时钟，装入TCM后执行GMU配置；IRQ屏蔽、
检查四个RSCC TCS idle，停机时SYSRESET=1、GBIF arbiter halt/ACK，先恢复TCM、
再撤时钟/映射/CX。任一停止/清理不确定则保留资源，不继续释放。
TCM覆盖所有8192字的运行后恢复，故障测试新增到18650个案例。

首轮日志 20261024-200914：原始GBIF_HALT=8，初始严格零检查拒绝，未释放复位；
后按锁定小米源清0并恢复实测8，独立主机和ARM64测试覆盖。
正常原厂固件启动随后导致USB失联/回Android，无FW_INIT或安全释放证明。
失败候选profile均标记FAILED，禁止普通控制器重用。
实时日志 out/gmu-boot-boundary-live/007.log 确认故障紧接SYSRESET=0。

out/gmu-config-only-live/returned.log 则证明全部相同配置在保持复位时成功：
configured=1、reset_released=0、stopped=1、restored=1、retain=0，
完整TCM恢复、时钟/SMMU/CX恢复、36项检查0失败、4CPU正常。
因此配置通过不等于CM3运行。当前定位真实执行/取指/DMA路径，尚未验证HFI。
所有手机启动仍仅fastboot boot，APFS全RAM；没有Android /data或分区写入。


## Stage17：真实 CM3、双流路由、原厂 FW_INIT 和 HFI（2026-10-24）

本地 ITCM 微程序先证明 CM3 执行；DMA 微程序真实读取 IOVA 0x4000，
写入 0x60000000 并由 CPU invalidate 后验证；外部取指版本再跳转
IOVA 0x100001，实际在系统 RAM 执行。诊断 marker 不作为原厂初始化成功。
完整日志分别为 out/gmu-local-live/returned.log、gmu-dma-live-v2/returned.log、
gmu-fetch-live/returned.log。

Mu DT iommus=<... 5 0x400> 的第三值是 SMR mask，实际需要 SID5 与 SID405。
改为两个精确 SMR/S2CR（未放宽任意流）后，未经改写的原厂固件连续两轮完成
FW_INIT_RESULT=0x100。out/gmu-boot-dual-{live,repeat-live}/returned.log
经独立报告器验证：19 配置恢复、完整 TCM 恢复、200/150MHz 时钟恢复、
GBIF halt ACK、双流隔离、SMMU/CX 释放、36 检查与四核退出均通过。
恢复点 ../marble-baselines/20261024-gmu-fw-init，15 文件 SHA256 已核对。

接入现有 AdrenoHfi MarbleGen7 传输；实际生命周期中保留全部外层资源，
CPU 仅以无缓存视图访问共享队列，开始前清理缓存别名，停机后再 invalidate。
新增 HFI_CTRL_INIT 的保存/恢复和 after-FW_INIT callback；任一 callback 失败仍须
真实 reset+drain，失败不构成可以提前撤映射的理由。

首次 HFI 候选因保留的 CTRL_STATUS=1 被本地严格检查拒绝，安全退出。
第二版明确 INIT 后等待状态，再以真实 ACK 类型/返回序号验证。
两轮 out/gmu-hfi-v2-{live,repeat-live}/returned.log：性能表返回
0x103b04，带宽表返回0x20a003；命令队列220/220、应答队列8/8，
notify=2、acks=2、pending=0、poisoned=0，全部资源恢复和36检查通过。
报告 out/marble-gmu-hfi-v2-report.json；负测拒绝缺项、重复、伪ACK类型、
错序号和未消费队列。软件注入 fixture 明确不作为硬件证据。

桌面回归 out/ui-gmu-hfi/contact-sheet.jpg 已人工核对实际帧缓冲：设置打开、
滚动、返回、长按菜单与关闭菜单；日志205826显示 Goodix ready1/bad0、
HID ok1、桥最大lag6ms。这仅覆盖合成mailbox→bridge→HID→UI，
不替代实体手指和光学测量。初始黑色截图发生于桌面就绪前，未计为通过。

仍未发送 CORE_FW_START、DCVS/GX请求或GPU命令；HFI表ACK不等于GPU加速。
下一步核对PDC/RSC和回滚，推进GX、SQE/ZAP及真实GPU提交，再接入iOS合成。


## 2026-10-24：CORE、GX、GPU 地址空间（后续实机结果）

本节更新上节的阶段状态；仍未启用 GPU 加速。

- CORE_FW_START、最低档 DCVS 请求及 prepare-slumber 连续两轮真实 ACK。
- GPU OOB bit30/ACK bit31：GX 实际供电、时钟状态正常，RBBM 可读。最低 220MHz 是请求表值，不是实测频率。
- 故意跳过 slumber 的两轮测试均取得 GX GBIF halt_ack=1，随后 CPU 接管、GX collapse、五项配置恢复及外层资源释放；恢复点 `../marble-baselines/20261024-gx-force-stop`。
- 持有 GPU OOB force 期间 HFI START：sent/returned=0x50010a、真实 ACK=0x41030a；随后 slumber=0x600321，连续两轮通过。不能以 ACK 代替 CP 执行证据。
- 复用 ArmSmmu4K builder/program/audit/streams，为 GPU 建立独立 CB1/ASID0x43，slots2/3 精确 SID0/0x400，来自 Mu DT gfx3d_user 的 `<0 0x400>`。
- 5 个 GPU 范围共48页：SQE、ring、memptrs、pwrup、scratch。SQE 文件跳过4字节，与锁定小米源 adreno_get_firmware 一致；仅使用无缓存 CPU alias。五项 SID0 ECATS 实际物理地址连续两轮正确，外层停机后才 fault streams、禁用 CB1/TLBSYNC、清零根地址。
- `out/marble-gpu-context-report.json` 两轮通过，报告负测拒绝21项缺失/重复、10种错误字段。`out/ui-gpu-context/contact-sheet.jpg` 已审阅；5项合成输入/帧缓冲回归通过。滚动比较只允许±160像素的列表位移，保留原始差值；原版 START 回归的滚动差异仍记为 FAIL。
- GPU 地址空间恢复点 `../marble-baselines/20261024-gpu-context`，保存尚未接入 CP 的 guest 源码与对应内核。

CP 初次候选使用现成 AdrenoPM4 编码器及 NativeAdreno/Xiaomi 的 CP_INIT/fence 布局。
在设置 CP_AHB_CNTL 后、尚未解除 SQE halt 和发布 ring WPTR 前，被健康检查拒绝。
GX drain/collapse 已确认，但外层恢复未完成，故保留资源并自动完整重启 Android。
此失败不计为 CP 执行或硬件加速成功；下一版加入健康寄存器快照定位。

自动回归仅能覆盖合成 mailbox→bridge→HID→UI 和设备帧缓冲；实体触摸、光学显示和温度没有直接测量。


## 2026-10-24：真实 CP 执行，LPAC 阻塞已解决

HWCG-only 与 GBIF/UCHE 候选仍在 SQE_CNTL=1 的写入边界挂起，
分别保存 gpu-cp-hwcg-v2-live、gpu-cp-bus-live 日志；候选标为 FAILED。
旧 redwood A660 驱动已记录：缺少 LPAC SID1 时 SQE 启动即复位；
锁定 Xiaomi kgsl_iommu.c 也把默认 TTBR0 同时映射给 user 与 LPAC。
据 marble DT 增加 slots4/5 精确 SID1/0x401，复用 CB1 同样五个范围，
不放宽任何流 mask、地址或物理内存所有权。

复用 AdrenoPM4 编码器；精确53项 Gen7_4_0 HWCG、5项GBIF和9项
UCHE配置来自锁定 Xiaomi 源，tools/marble-gen7-hwcg.py 可重生成验证。
CX QSB四项在完整GX/CX drain、GX collapse后恢复并读回；不能因超时
或SQE停止请求就释放DMA页。SQE_CNTL写3实际读1，仅记halt_requested，
不宣称halt_observed。最终安全释放依赖实测外层停止和根地址清零。

内核 mach-marble-gpu-cp-lpac-v2，SHA256
6551fce6e857ad5ff681610f321872471f56aa780d2670a0f5f9a106b7bc0613，
连续两轮真实CP：21 dwords、RPTR/WPTR/shadow均21，scratch及RAM marker
0x4d475031，cache-clean fence1，status1、fault0、irq0。随后START/slumber ACK、
GX停机、四流隔离、CB1 TLBSYNC/roots_zero、TCM和时钟等外层恢复，
36检查0失败。报告 out/marble-gpu-cp-report.json PASS；
负测拒绝200项缺失/重复及13类错误DMA/LPAC/停止/恢复证据。
原初LPAC候选已完成DMA但错误要求halt位读回，因此PASS=0的日志保留，
不能改写为通过；新报告独立要求真实执行和完整外层释放。

out/ui-gpu-cp 五项自动回归全部通过，contact-sheet已审阅，
对应桌面日志20261024-222028-marble-live.log。范围仍是合成mailbox→bridge→
HID→UI和帧缓冲，不是实体触摸/光学显示/温度测量。
恢复点 ../marble-baselines/20261024-gpu-cp 含 guest 实际源码、内核、loader、
两轮日志、报告和截图，SHA256 manifest。

CP写寄存器/内存和cache-clean fence已实测；尚未证明shader、光栅化，
也尚未启用iOS GPU合成。下一步ZAP认证、安全模式切换及真实渲染。

## 2026-10-24：ZAP 认证、安全模式切换

严格按锁定 Xiaomi SCM/MDT 协议操作 PAS13。唯一可执行段是 b01 的2236字节，
不是 b02 哈希/证书段。只有 fresh INIT_IMAGE 和 MEM_SETUP 均明确成功后，
才写 DT 保留区 0x8b91a000 的4KB、零填充并完整读回，再 AUTH_AND_RESET。
初始 SHUTDOWN 可能返回 -3145800；错误不证明已有可用 ZAP，也不允许跳过认证。
最终 SHUTDOWN 必须在 GX drain/collapse 后成功，否则继续保留资源。

mach-marble-gpu-zap-v2 两轮报告 out/marble-gpu-zap-report.json PASS，
真实认证与最终卸载成功，外层恢复和36检查通过。五项合成输入/帧缓冲回归
out/ui-gpu-zap/report.json PASS，contact-sheet 已审阅。

复用 AdrenoPM4 与小米 CP_SET_SECURE_MODE(0) 流程，经 GPU 执行后追加
cache-clean fence2。初版首次实测 TRUST_CNTL 1→0，RPTR/WPTR/shadow均28，
fence2、fault0/irq0；第二轮初始状态已经0，被仅接受1的前置检查拒绝并完整回收。
该失败保留于 gpu-secure-repeat-live，不能计为通过。
修正版接受精确0/1初始状态，始终提交命令并要求完成；只把 before1/after0
记作 fresh_transition，不把初始0冒充新转换。CPU从不写 TRUST_CNTL。
初版首轮与修正版首轮的报告 out/marble-gpu-secure-report.json 均PASS。
out/ui-gpu-secure/report.json 五项回归PASS，截图已审阅。
软件 fixture 与报告负测覆盖 SCM 失败、提前复制/卸载、命令失败、错 fence、
故障与寄存器状态；这些软件结果不作为硬件执行证据。

尚无 shader、像素绘制或 iOS GPU合成证据。继续复用 Mesa Gen7 2D路径，
已锁定参考源码于 ../upstream-marble/mesa-reference/sources.json，
Mesa commit c515ec0fe77da9f42f390e442379f2d0629d908c。
无人值守回归范围仍为合成输入和帧缓冲，实体触摸/光学显示/温度未直接测量。

## 2026-10-24：真实 Gen7 2D 绘图

mach-marble-gpu-blit（SHA256 92362b878f44af20e5c99dad63e95c431dee4311d58da1170a7ed54154bc72d1）
连续两轮在已认证、非安全模式的 Adreno725 上执行 Mesa Gen7 2D 包。
RPTR/WPTR/shadow=167，fence=3，fault=0、irq=0；32×32 BGRA8888 的
全部1024像素与预期三色逐一相符，3056个保护字未改写。CPU仅预填不同的哨兵值。
报告 out/marble-gpu-blit-report.json；gpu-blit-{live,repeat-live}/returned.log
同时验证 GX/CX drain、ZAP卸载、四精确SID隔离、CB1关闭/TLBSYNC/根清零及36检查。
未扩展 scratch 映射。参考 Mesa commit c515ec0fe77da9f42f390e442379f2d0629d908c：
A7xx SP_A2D_OUTPUT_INFO=a9bf、CCU_CACHE_CNTL=88e5、TPL1=b2d2、UNORM8 ifmt=0。
out/ui-gpu-blit 五项合成输入/帧缓冲回归通过，contact-sheet 已审阅。

这证明了真实2D绘图；尚无 shader、运行期用户提交或 iOS GPU合成证据。
恢复点 ../marble-baselines/20261024-gpu-blit 保存重构前实际guest源码和匹配内核。
后续重复提交重构的软件ASan/UBSan、ARM64/QEMU fixture均通过，含128次提交跨ring wrap；
fixture仅为软件检查，不能替代实机重复提交。

## 2026-10-24：GPU 常驻生命周期

mach-marble-gpu-runtime-v2，SHA256 a16ad68bf7fda281bbbeda4ae13031f5c317d61b7a454871edf1dff002f6e02d：
独立 kernel worker 持有原有嵌套硬件租约，持续提交同一有界2D测试，运行期取消每次kick的100ms诊断延迟。
桌面60秒会话480次绘图、16次ring wrap，全部像素/保护字核对；45秒四核CPU压力并行会话
339次绘图、11次wrap，CPU压力PASS、4线程、timer drift0、errors0。两轮完整释放报告
out/marble-gpu-runtime-v2-report.json PASS。第一轮日志中段被覆盖，使用未回绕实时164.log与
returned.log的同一条draw64原始记录拼接；complete.provenance.json记录原文件哈希和三个复制片段，
原始日志不改动。不能仅凭缺少初始证据的returned.log判为通过。

运行期的正常退出在停其他核心前等待worker复位/停机/释放；BSD正常关机也在子系统关闭前调用stop。
首次CLI候选在GPU初始化期间请求关机，晚期stop不能可靠等待，保守复位；失败未计为通过。
任何仍有未验证租约的panic/直接loader_return只触发PSCI SYSTEM_RESET，禁止loader复用DMA内存。
CPU压力轮验证了修正后的CLI主动关机，桌面轮验证了定时退出；均完整撤销CB0/CB1、隔离精确SID。

out/ui-gpu-runtime 五项合成输入/帧缓冲回归PASS，contact-sheet已审阅。该单独桌面会话885次绘图后
主机oem stop，最终释放PASS；其初始/最终日志之间缺少draw64进度记录，故不作为第二个完整证据报告，
不补写丢失记录。恢复点 ../marble-baselines/20261024-gpu-runtime 保存匹配guest源码和内核。

GPU现能在桌面运行期间持续完成2D任务，但尚未发布用户态GPU服务，iOS合成仍为软件路径。
后续复用锁定ArmSmmu4K Live库及现有RedwoodGPU/Mesa接口；实体触摸、光学显示和温度未直接测量。
## 2026-10-24：动态显存映射实测

mach-marble-gpu-live-map，SHA256 0db00dd07f94a0ff4bd33dcad9e949a5869154ef9124a918dd3e80dfe77a69eb。
复用锁定 ArmSmmu4K Live 库，在 512MB IOVA aperture 预留空拓扑（258个表页、初始0叶子），
并非预映射512MB。先发布五个基线分配（48叶子），随后独立16KB目标完成64次映射/绘图/解除映射，
128次真实TBU ECATS、每次1024像素与3056保护字核对；最终generation129、48叶子、pos871/fence67。
两轮各追加339次常驻绘图、11次ring wrap，同时45秒四核CPU压力，全部完整停机/隔离/根清零。
报告 out/marble-gpu-live-map-report.json PASS；负测拒绝272条缺失/重复证据、8类损坏字段与批次错序。
IOVA解除映射先完成真实quiescence；任何TLB同步/库poison不确定都保留对象直到reset。
初始批次与64次unmap各写TLBIASID/TLBSYNC，固定生命周期44写加130写，最终174写。

out/ui-gpu-live-map/report.json 五项合成输入与帧缓冲回归PASS，contact-sheet已审阅。
匹配guest源码为 out/gpu-live-map-source.tar.gz；后续service修改不属于此稳定内核。
Mesa25.0.7已完成本地编译（softpipe+freedreno），但尚无用户态shader/桌面GPU合成实测。
首版service在adopt读取被白名单拒绝后保护性复位，保留失败日志，未计为通过。

## 2026-10-25：用户态 shader、CoreAnimation GPU 合成与黑条块规避

`mach-marble-gpu-service-v2`（SHA256
`e7d064b79831c26a28dbd91757154cfba55f92afdf2b40a0e3ecd6e8cd3c20d1`）
发布 RedwoodGPU / NativeRing 服务，复用现有 Mesa freedreno Gen7 用户态驱动和
ArmSmmu4K Live 映射库。真实 shader 测试 renderer 为 FD725 / OpenGL ES 3.2 Mesa 25.0.7，
核对15564像素（内部3864、外部11700），bad=0、GL error=0；三次提交 fence68–70。
`out/marble-gpu-service-report.json` PASS，同时要求 BO 解除映射、完整 GPU drain、
CB0/CB1 释放、四核与36项自检。报告负测拒绝182项缺失/重复证据和8类损坏字段。
匹配内核源码保存为 `out/gpu-service-v2-source.tar.gz`，不是当前任意修改后的源码推断。

`inject.c` 在选择 Mesa screen 前等待服务 ready，避免提前锁定软件 renderer；
实际 CoreAnimation context 日志明确为 FD725，GPU runtime 中产生真实提交与 fence。
首轮 GPU 桌面合成19335次提交，用户确认流畅度明显改善，但报告负一屏和资源库黑/绿块。
`sysmem` A/B 仍出现大面积黑条块，原始故障图保留在
`out/ui-gpu-sysmem-pages/library-three.png`；不能以禁用 GMEM 视为修复。

全部禁用 IOSurface sharing 的 `copy` 模式恢复画面，但增加整帧读回开销。
最终仅禁用输入 IOSurface 的直接共享，上传输入纹理，保留 GPU blit 到共享输出。
marble 的 `default` 与 `copy-input` 使用该策略，日志为：

```text
redwood: marble GPU service ready before Mesa screen selection
redwood-gl: an OpenGL ES 3 context for CoreAnimation, by Mesa, off-screen (OSMesa): OpenGL ES 3.2 Mesa 25.0.7; FD725; shaders OpenGL ES GLSL ES 3.20
redwood-gl: marble profile=default share=1 fetch=1 blit=1 inputs=0 outputs=1
```

证据定位到 CPU 产生内容的导入源 IOSurface 路径；尚未精确区分缓存一致性、布局或 stride 根因。
这是有 A/B 和截图回归支持的规避方案，不是对所有渲染缺陷的完整证明。

`tools/marble-gpu-pages-test.py` 自动执行负一屏、返回桌面、第二页、App资源库、
返回、长按菜单和取消，每轮8动作，退出时请求完整停机并保存返回日志。
两轮 copy-input：6671提交、末次summary1600帧、平均合成9295微秒。
三轮 default：9716提交、末次summary2400帧、平均合成9135微秒。
两者0 GL errors，40动作图像与已审阅的全copy GPU参考比较通过；default最大MAE
0.0863/255、超过24级差异的像素比例最大0.000717（排除顶部130像素时钟区）。
合成耗时不是屏幕呈现FPS。最终三轮 contact-sheet 已审阅，未复现上述黑条块。

`out/marble-gpu-composition-report.json` PASS，核验原始日志SHA256及逐段复制来源、
真实FD725 context、提交采样序列与fence、BO映射generation、共享输出、GPU停止顺序、
外层恢复、四核和36项检查，以及BGRA与PNG完全一致。日志重组只复制精确连续重叠的原字节，
不补写缺失记录；`complete.provenance.json` 保留来源、偏移、长度和散列。

基础五项回归对旧软件截图为4/5，菜单差异15.6487超过原阈值12，原失败报告完整保留。
独立 `out/marble-gpu-visual-review.json` 与首轮已审阅GPU截图比较：五项在原基础阈值内，
滚动按原有惯性停止位移规则注册；菜单MAE0.00961，四项菜单正常显示。
真实黑块故障图的负测MAE15.738、超过24级差异像素24.60%，被页面检测阈值拒绝。
这不覆盖原软件参考失败，也不把不同renderer的模糊背景判定为像素等同。

最终文件：

- loader：`out/marble-gpu-service-loader-boot.img`，SHA256
  `18e70f9e04d0ef8c90f9d48dfdd48f2c1866d7976d2d952c02e31a985f6954ef`，已临时启动验证。
- 支持盘：`out/marble-root-gpu-accelerated-launchd.apfs`；内含最终用户态库及默认配置。
- 用户态库：`out/linux-build/userland/libOSMesa.8.dylib`、`libredwood-gl.dylib`、`libredwood.dylib`。
- 匹配最终库的guest源码：`out/gpu-accelerated-userland-source.tar.gz`，SHA256
  `69056388982ef7f2b3950b9d98e26b758c83ba38df4ae4b7bf14c25e5b026026`。
- 新独立恢复点：`../marble-baselines/20261025-gpu-accelerated`，manifest记录每文件SHA256；
  大系统APFS沿用本机既有文件，路径/大小/SHA256单独列为外部依赖，未伪称已复制。

临时启动流程（已经解锁的 marble，禁止改成flash）：

```powershell
python tools/marble-run.py boot --image out/marble-gpu-service-loader-boot.img
# 若停在UEFI菜单，进入HackiOS USB loader；首次或重启后先上传RAM系统盘：
python tools/marble-run.py ramdisk out/marble-system.apfs
python tools/marble-run.py run --kernel out/linux-build/mach-marble-gpu-service-v2 --ramdisk out/marble-root-gpu-accelerated-launchd.apfs --cpus 4 --seconds 180 --fb-threads 2 --input-mailbox --gpu-probe 17 --gpu-runtime --gpu-composition --detach
# 到期正常returned后保存日志，再reboot回Android。
python tools/marble-run.py snapshot
python tools/marble-run.py reboot
```

所有实机操作仍是临时boot和RAM APFS，无Android分区刷写、擦除或/data操作。
无人值守验证范围是合成mailbox→bridge→HID→UI及采样帧缓冲；无法替代实体触摸、
光学显示或测试期间温度测量，也不能排除未采到的瞬态渲染问题。
本轮不做超频、DVFS或无限压力测试；长期稳定性、功耗、Metal与更多应用兼容性仍待验证。

## 2026-10-25：瞬态渲染缺陷重新验收（取代旧静态通过结论）

用户确认设置退出时图标会闪绿色、负一屏和资源库会闪黑块，静态截图随后恢复。
因此上节静态页面 PASS 不代表动画正确，输入拷贝规避不能作为完整修复。
Dock 必须保留背景模糊；纯白 Dock 是失败，即使日志声称 GPU ready。

新增固定 96 MB RAM 环形逐帧源 IOSurface 记录（空间降采样，保留每个呈现源帧），
`marble-recording-review.py --sheet-all` 输出所有保留帧及黑/绿候选邻域。
这是显示源而非光学 scanout，约保留最后 20 秒；不能证明未保留区间或单个物理扫描周期没有缺陷。
像素计数仅排序候选，不自动判视觉通过。合成输入、源画面检查也不替代实体触摸及运行期温度测量。

| 实验 | fetch / 输入共享 | 连续帧结果 |
| --- | --- | --- |
| 原 default / copy-input | 1 / 0 | 设置退出窗口变黑；`display-animation-default` 最大近黑像素 56454 |
| copy | 1 / 全拷贝 | 仍黑，最大 51080 |
| nofetch-copy-input | 0 / 0 | 仍黑，最大 56520；仅关闭 fetch 不够 |
| nofetch | 0 / 1 | 设置动画保留内容，Dock 模糊；原轮最大近黑像素 1074 |
| GPU 写后 CPU cache invalidate | 原 default | 仍黑，最大 52218；不是已证实根因或修复 |
| GPU writer 元数据动态共享 | 原 default | 仍黑，最大 59543；未出现预期动态切换日志 |

EDR clear 和同 context surface alias flush 实验均无改善。失败源码独立存于
`out/display-ownership-experiment-source.tar.gz`，四个相关文件已精确回到锁定源码版本；
不把未生效的 ownership/缓存试验带入候选，也不覆盖其他既有改动。

`out/display-pages-nofetch-shared-retry`：原 service-v2 内核和原用户态，
18 次自动滑动，441 个保留帧已逐一列图审阅，覆盖负一屏及资源库往返；未见先前黑块。
近黑计数最大 43/75922（计数非通过条件），Dock 白色比例 0，GPU 完整释放并 returned。
`out/display-animation-nofetch-shared-retest`：3 次设置开关，299 帧涵盖全部三次退出，
逐帧列图未见黑窗口或绿色图标；最大近黑像素 1072，设置图标区域绿色计数为 0。
这些是有界候选证据，尚不声称用户所报间歇绿色闪块在所有场景彻底消失。

`userland-display-v2` 将 marble 默认改为 fetch=0、inputs=1、outputs=1，
保留 FD725 GPU 合成与输出 blit；copy-input/shared 等显式实验配置仍可选。
它是复用现有 Mesa/EAGL 兼容路径的组合规避，不是新的完整 Adreno 驱动。
独立支持盘 `out/marble-root-display-v2-full.apfs`，原稳定盘与原库目录未覆盖。
完整滑页、设置上下滚动、两次设置开关已完成 legacy / immediate / vsync 三轮。
`immediate` 为取消软件 beat，`vsync` 为另加 CoreAnimation 软件通知，均非面板硬件 vblank 实测。

| 同一 full 工作负载 | GPU 提交 | 平均合成耗时 µs | 软件呈现间隔 <35ms 比例 |
| --- | ---: | ---: | ---: |
| legacy | 16067 | 2125 | 76.24% |
| immediate | 15960 | 2129 | 75.29% |
| vsync（软件通知） | 16123 | 2124 | 75.35% |

比例仅汇总完整处于 marked workload 内的约5秒统计窗口，排除暂停计数；不是面板 FPS。
单轮数据不支持换节奏有明确收益，故最终保留 legacy。
这三轮保留帧的设置图标区域绿色计数均为0，最大近黑像素1250/1250/1271，
候选图与动画缩略记录未见旧黑窗口；计数仍不是全面视觉认证。
新编译默认库另跑 `out/display-v2-pages`，18动作、441帧，Dock非白、完整释放。
`out/marble-display-v2-report.json` 对六轮的真实GPU提交/fence、映射generation、硬件启动/停机、
四核自检、原始日志拼接、截图BGRA/PNG一致性和记录身份进行严格审计，全部PASS。
`visual_pass` 和 `transient_artifacts_pass` 仍为null。
正常支持盘（无自动手势）`out/marble-root-display-v2-launchd.apfs`另完成75秒运行，
2347次真实提交，正常release/returned；此轮只保存末尾日志，未冒充完整启动证据。

两轮审计最初因漏读generation269/391失败。原始字节表明kernel完整行被接在launchd半行后，
并非缺少映射。解析器现接收原样完整suffix，排除带时间戳的IOLog副本，仍拒绝损坏、重复、
缺失generation；原失败报告保留。新增4项并发前缀／损坏／重复测试，全部14项测试通过。
旧静态composition报告在解析调整后也重新通过，未用其覆盖瞬态验收。

正常加载仍使用原service-v2内核和已验证display-loader，独立用户态库SHA256：
`e93305818a63219abd3f42f5bd1c98e15e864aab4c0be0538f25c63ff05b3fe3`。
`out/gpu-display-v2-userland-source.tar.gz`沿用原精确源码并仅替换本次eagl.c；
已核对guest编译输入eagl.c/gl_osmesa.c散列与档案一致。
`out/display-v2-animation-comparison.png`取原故障与候选各最后一次exit-begin约0.45秒的实际源帧，
仅用于展示已捕获的黑窗口改善，不能代表间歇绿色闪块在所有应用中修复。

一次白 Dock 回归源于新 EFI 启动后漏做 touch-probe，导致 RPMh guard 正确拒绝 GPU 启动。
`marble-run.py` 现在为 input-mailbox run 自动执行已有幂等 touch-probe，未放宽硬件 guard。
`marble-display-test.py` 要求硬件 renderer、ready、非白 Dock 和完整 release，否则运行失败；
`visual_pass` / `transient_artifacts_pass` 保持 null，不能用脚本退出码冒充视觉认证。
日志连续性测试 10 项通过，旧静态报告仍保留，但不用于覆盖本次瞬态失败。

当前第 7 项仍在验收，第 8 项没有新增已启用日常硬件功能。
ROM 包确认含 `images/dsp.img`、音频/无线配置及固件，可复用协议和数据，Android .ko/.so 不能直接用于 XNU。
marble SPMI 地址与上游不同，不直接启用旧 buttons/battery 初始化；继续禁止 Android /data 访问。

## 2026-10-26：通知中心／控制中心失败与发热停止

用户在 display-v2 上再次报告：左上拉下出现红色、控制中心不完整、间歇闪屏。
`out/display-v2-user-panels/notification.png` 已复现红色。因此前面的有界滑页结果
不能推广到两个下拉面板，也不能证明全部闪屏修复。

原因之一已在兼容层确认：关闭对 CoreAnimation 宣告的 framebuffer-fetch 扩展时，
自己的 function45 vibrantColorMatrix 补偿也被同一条件关闭，红色遮罩被当成普通图片画出。
现将扩展宣告与内部补偿分开。三种内部背景读取 shader 改为先通过 GPU 复制背景，
再用普通 sampler/texelFetch 采样另一张纹理，保留原色彩矩阵和模糊效果。
group_over 现在在 group_flatten 后才设置 program，避免 flatten 恢复 CA program 后画错。
这些是已识别的代码问题，不代表所有闪屏均已找到根因。

| 面板实验 | 实际结果／限制 |
| --- | --- |
| `display-panels-native` | 恢复内部矩阵，红色消失、控制中心出现；仍抓到时钟上方小黑块 |
| `display-panels-copy` | 初次 shader 改写误放在 vblur_program，仍走 native fetch；不作为拷贝路径通过证据 |
| `display-panels-copy-v2` | 正确改写三个 shader；首次通知中心无红／黑块、控制中心模块与背景模糊出现 |

copy-v2 的 373 个保留源帧（5904.927–5949.226 秒）已生成全部六张逐帧列图并审阅，
未见先前红色、大片黑块或设置区域纯绿色；近黑计数最大42、设置区域纯绿0仅是排序统计。
第二轮合成手势页面不匹配：notification-1 实际是控制中心，control-1 实际是桌面，
不能算两轮面板通过。仍有动画更新不连贯的可能，源帧记录不等于光学 scanout。
`out/marble-display-panels-copy-v2-report.json` 仅审计硬件生命周期／原始证据 PASS，
`visual_pass`、`transient_artifacts_pass` 保持 null。

用户随后报告手机异常发烫。已停止实机测试，确认 GPU `released PASS=1 active=0`、
GX collapsed、context restored、loader returned 后立即回 Android。
退出后电池读数33.9°C不能代表测试时芯片或机身峰值，更不能否定用户的发热观察。
用户随后确认已降温；后一次 Android 电池读数27.5°C。
此前连跑多轮和无运行期温度证据的900秒展示会话不再作为当前无人值守流程。

当前电脑端候选 `userland-panels-scissor`（尚未实机验收）：背景尺寸不变时复用纹理，
仅复制当前 scissor 与附件的交集，并保留原 framebuffer 坐标；尺寸变化才重新分配。
背景复制的 GL 错误在第600帧后也继续检测，失败拒绝该效果并写日志；报告额外拒绝
NOT MADE、shader link failure、backdrop error，不能把初期0 errors算成后期无错误。
主机测试直接提取生产代码和三个 shader，在 VM 的 Mesa llvmpipe GLES 环境检查：
42组、10908个颜色通道、3个 shader编译及像素结果、负坐标／越界／空scissor、尺寸变化、
独立READ framebuffer、READ_BUFFER和unit7绑定恢复，全部通过。此为主机软件Mesa验证，
不是Adreno硬件验证，也不是功耗降低实测。证据在 `out/backdrop-host-test/`。

新增 `xnuloader/marble_thermal.h`：依据已提取 marble DT 和固定 MiCode 源码，只读两组
TSENS v2 的19个CPU/GPU通道，检查控制器启用、样本VALID、温度编码、critical/watchdog。
不写TSENS、不改阈值、校准、供电或充电设置。
初版额外依赖TRDY和CC_MON导致拒绝启动；原始诊断保留在
`out/thermal-loader-diag-probe.log`。硬件版本0x20080000，TRDY=8、CC_MON=1，
每通道VALID已置位。现严格按上游 `get_temp_tsens_valid()` 用每通道VALID读取；
CC_MON是转换周期监视状态，不能混同watchdog bit31或温度critical。
Android sysfs随后读到CPU/GPU约34.5–42.8°C，与编码量级一致，尚未证明跨系统同步校准。

温度保护版要求启动时19路读数有效且最高低于40°C；运行时每秒采样，
任何无效读数／最高达到55°C请求现有stop mailbox正常退出，锁住后续启动直到固件重启。
GPU runtime在host和loader策略均限制最多90秒，不能再沿用旧loader绕开监测。
软件阈值是本实验的保守退出条件，不是设备热设计认证；片上温度不代表电池或表面温度。
当前先验证只读采样和退出保护，再安排一次短时GPU验证；禁止恢复无监测连跑。

在温度与渲染验收前，不宣告GPU完成，不切换到日常硬件测试。
真实VSync后续以MDSS接口计数／扫描线和硬件事件为证据；现有ca_vbl软件通知仍不算硬件同步。
新增参考记录在 `../upstream-marble/display-power-reference/*sources.json`：MiCode TSENS固定提交，
以及现成LineageOS SM8450 display-drivers固定提交3b30ba7df6608c97c0ba2d55d679a6b5987c4c66；
显示寄存器地址还需与marble DT／固件当前状态逐项核验，不直接套用写寄存器代码。

### 后续无人值守续跑：启动温度保护拒绝，未新增GPU实机通过

此前 raw hello 在源码内核上出现 initproc failed；随后完整 CLI 支持盘虽正常返回，
却约0.4秒就主动关机，运行期温度样本为0。`out/thermal-cli-check/thermal-report.json`
正确判为失败，不能把这一轮称为持续监测验证。另建的 sleep20 CLI 支持盘仍需完整系统盘。

本次在 Android 读到电池31.3°C、CPU/GPU约33.0–39.6°C后，临时启动温度loader，
上传系统盘耗时96.373秒。上传后最高片上读数40.0°C；69秒后仍为40.0°C，
两次只读probe均被严格低于40°C的条件拒绝。18秒单核CLI实验因此没有启动XNU，
保存 `out/thermal-idle-check/` 后立即回Android。没有提高门槛或启动GPU。

为避免每次监测验证都先传输2.6GB，新增静态睡眠init和独立4MB APFS：
`tools/marble-thermal-idle.c`、`tools/marble-thermal-idle-image.py`。
使用现成libmini的select睡眠封装，20秒后正常halt；loader预算仍18秒、单核、GPU关闭。
无dyld、iOS共享缓存、设备初始化或压力负载；这不是新的iOS系统盘。
编译输入的guest libmini与主机源码在CRLF规范化后逐字节散列一致，Mach-O无动态库依赖。
`out/marble-thermal-idle-minimal.apfs` SHA256：
`15eb3220b7369c5ca1eef1fbc43847569c5413b5bfc796f0cd15bbbb862a0ddd`。

Android休息后另一次临时启动，在loader约21.3秒处最高仍40.0°C，新的小盘实验也被
preflight拒绝，未上传run payload、未运行XNU/GPU。原始证据在
`out/thermal-idle-minimal-check/`；手机再次返回Android。
所以这个小盘目前只完成构建／Mach-O检查，尚未完成实机启动和持续采样验收。
后续冷却条件实际满足时，用新启动且未上传resident系统盘的loader执行：

```powershell
python tools/marble-run.py boot --image out/marble-thermal-monitor-loader-boot.img
python tools/marble-thermal-idle-test.py --out out/NEW-IDLE-EVIDENCE
```

测试器仅一次尝试，保存拒绝或returned证据，然后回Android。
GPU显示测试器也补齐启动拒绝的清理；即便录帧传输／日志拼接抛异常，也会先核验
returned尾部的active=0、GX collapsed/restored、context PASS/roots_zero/tlb_sync，
证据成立才自动回Android。不会因处理证据失败而无故长留loader。

源码审查另外确认：`wdt_arm()`只对`platform_redwood`执行Haven SMC，marble分支直接返回。
因此当前marble没有已经验证的上游虚拟看门狗，不能假设睡死会自动复位。
没有启用未经唤醒验证的WFI/WFE，也没有套用redwood的SMC、时钟或PMIC设置。
新增Linux v6.1 architected event-stream参考仅用于离线研究，不是已启用的省电功能，
更不是已确定的发热根因。GPU轮询和EFI忙循环仍需后续测量与有界修复。

本轮20项Python测试、温度解码原生测试、RAM-only策略原生测试通过；新Python脚本编译通过。
`marble_thermal.h`已加入git白名单，避免恢复档案遗漏实际loader依赖。
scissor GPU候选仍保持原散列和42组host Mesa通过结果，Adreno/瞬态渲染/运行期温度未新增通过。
真正硬件VSync和第8项日常功能都未启用；继续按GPU正确性与热保护、VSync、日常功能的顺序。

### 冷却后一次小盘验证：运行期采样通过，空闲功耗仍不宜继续GPU测试

保存上述恢复点期间，Android电池降至30.5°C，所读CPU/GPU通道最高38.5°C。
在这个实际变化后另做一次全新临时启动；没有改温度门槛，也没有自动重试温度STOP。
`out/thermal-idle-minimal-cooled/` 证明独立4MB APFS成功挂载，静态init打印
`sleeping 20 seconds, GPU disabled`，单核、gl=0、gpu=0，18秒预算后正常returned并回Android。

`thermal-report.json`采样审计PASS：19路全部有效，18个运行期样本，最大间隔1,000,104µs。
但最高读数从preflight的38.5°C上升，首个运行期样本50.6°C，峰值54.6°C；
运行期最热通道均为控制器0/传感器12（CPU通道），距55°C退出门槛仅0.4°C。
这不是电池或机身温度，未触发STOP也不证明STOP实机执行链已验收。
它证明GPU并非这次升温的必要条件，不能据此断言GPU不增加热量，或已确定唯一根因。

因此不以“温度报告PASS”为继续90秒GPU测试的充分条件。当前先排查XNU CPU空闲、
固件监控核忙循环与现有频率/唤醒行为，未进行新的GPU实机运行。
代码审查确认XNU现有cpu_idle本身已有普通WFI路径；并非可直接用“加一条WFI”解决。
还需区分空闲驻留时间、待处理中断、WFE近定时器路径、固件监控循环与频率状态，
在没有证据前不改电压/频率或进入新休眠状态。

当前结论更新为：温度连续采样和正常返回已通过一次有界无GPU验证；
空闲温升/功耗未通过，GPU scissor候选的Adreno与瞬态渲染验收仍待做。
手机留在Android，VSync和日常功能继续排在GPU正确性与热问题之后。


### 2026-10-26：单核 SIGPdisabled 忙等修复与黑屏空闲短测

新增 opt-in `marble_idle_probe=1`，记录 cpu_idle 入口、禁止/近定时器分支、
WFI 次数和 CNTVCT 驻留时间，不在空闲热路径打印。沿用每秒睡眠的 cpuload 线程报告。
第一次诊断 `out/thermal-idle-residency/` 抓到 11,920,834 次入口全部被禁止，WFI=0。
随后新增的 CNTP_CTL_EL0 只读访问被 hypervisor 拦截为未定义指令，触发 panic，
loader 正常接回并回 Android。该候选 manifest 标记 FAILED，保留原始证据，不能使用。
诊断读取已缩减为现有内核使用的虚拟定时器/CNTFRQ/ISR/MPIDR。

根因位于 GenericARMCPU::initCPU：首次信号握手仅在多核分支发 SGI，
单核 GIC 不启用 SGI，因而 cpu_signal 的 SIGPdisabled 从初始化后一直未解除。
单核分支现在直接调用现有 cpu_signal_handler，完成相同握手。
没有绕过 cpu_idle 检查，没有修改电压、频率、PMIC 或新的休眠寄存器。
此缺陷解释无GPU单核基线发热，不能冒充四核GPU原始发热的唯一解释。

`out/thermal-idle-handshake-fix/`：18秒、单核、静态 sleeping-init、GPU关闭，
preflight 37.7°C，18个运行期样本最高38.9°C（之前同类旧版峰值54.6°C）。
17组空闲观察中 disallowed=0、signal=0；约16.016秒测量区间 WFI 驻留99.995%，
最长一次约205ms。温度及空闲报告均通过，无panic，按预算returned并回Android。
这仍是短测，不能宣称持续GPU功耗或热设计已验收。

用户要求空闲黑屏：Android通过休眠键熄屏，dumpsys display确认mScreenState=OFF、
亮度0.0。XNU空闲测试新增fb_blank=1：切换graphics console、清黑全部面板像素，
日志确认boot framebuffer black。该功能是黑像素，不等于实现DSI/面板断电。
GPU画面验收不启用fb_blank；结束后回Android熄屏，不能放置长时间静态桌面。

新内核out/linux-build/mach-marble-idle-fix SHA256：
8f3f52e354e0ddfef11f93b5b32cfe27c948b4d83e66acc5af5f801ca2cd77e0。
新loader out/marble-idle-probe-loader-boot.img SHA256：
7291c2fd2ceb86668e110a9d6b31e7eaf47125f6be3063dffdd22bd9da2c0ab6。
实际修改源码归档out/idle-fix-source.tar.gz SHA256：
ada435d4b86720fc6c253f9bdf38d1ea123f2d50453e1bf3e4d65c238be3dfc6。
构建完成链接；make仍只有既存4项export-list检查失败，日志已审阅，无新增编译/链接错误。

重建新增补丁xnu/marble-idle-probe.patch，xnu/apply.sh在redwood补丁后应用。
测试器默认使用新idle-fix内核和黑屏诊断参数，需要新版loader允许这两个参数。
18秒小盘不上传2.6GB系统盘；每次只尝试一次，温度不足拒绝后回Android。


四核复核 `out/thermal-idle-smp4-check/`：18秒、黑屏、无GPU；最高39.6°C，
18个运行期样本，三个副核returned后PSCI状态全部OFF；回Android后screen/actual均OFF。
部分副核整段停在尚未返回的WFI内，当前计数只累加完成的WFI，不能用0次完成计数
断言该核忙等，也没有给四核套用单核的99.995%驻留率结论。

随后一次scissor GPU尝试 `out/display-panels-scissor-guarded/` 未通过：
系统盘2.6GB上传95.809秒，准备阶段调用loader已有oem clearscreen保持黑画面。
内核预算75秒，panels job等待从45秒缩为20秒；GPU固件仍为用户ROM已固定哈希版本，
EAGL库散列未变。冷启动preflight39.6°C；第一运行期样本53.8°C，下一样本49.5°C，
主机据45°C保守门槛请求stop。采样门槛是事后响应，不能保证峰值不超过45°C。
未取得GPU ready/桌面、录帧或完整released/GX/context返回证据，随后fastboot getvar
等待60秒超时，设备自行重新出现在Android。只读boot reason为bootloader，
无法凭此确定重启原因，不能编造已审核GPU释放。未自动重试，也未强制重启活跃GPU。
最后调用Android正常休眠键并确认mScreenState=OFF、mActualState=OFF；电池读数31.8°C。

这个启动阶段最热通道controller0/sensor10，来源尚不能在iOS CPU启动与GPU之间隔离。
只读EPSS请求为大核2342MHz、prime2822MHz（非有效频率测量）；Android只读sysfs
确认policy4对应4/5/6、policy7对应7，可用最低频率分别633600/787200kHz。
尚未写EPSS/电压/PMIC。下一步先设计有恢复机制的板级启动频率约束并隔离启动负载，
不得反复运行同一热失败镜像。该内核manifest的gpu_status=FAILED已在payload生成器
离线验证会拒绝GPU runtime；单/四核小盘空闲用途仍独立保留。

显示测试器新增保守温度阈值、读数停更退出、异常原因先落盘；若USB丢失后Android
已独立出现，只做Android熄屏，绝不把这种重启当成GPU释放PASS。所有异常和原始日志
独立保存。scissor渲染正确性、真硬件VSync、第8项日常功能仍未新增验收。

### 2026-10-26：CPU_ON 后可恢复降频与 iOS 启动隔离

本轮详见 MARBLE-CPU-THERMAL.md。新增 marble 专用、显式 --cpu-cap 的请求 lease：
验证完整 LUT，只降低大核/prime 请求，保留更低的原请求；不写小核、PMIC、电压。
初版在 CPU_ON 前设置，实机发现固件会覆盖该值。已改为四核上线后、GPU/userspace
开始前，通过RAM mailbox让EFI降频，再由XNU核验。退出时四个大核全部OFF才恢复，
读回审核所有请求和perf-lock mirror，包含无需写入的分支；失败锁住下一轮。

out/thermal-cpucap-v3-idle-smp4：实际把2342400/2822400 kHz请求降低为
1209600/1171200 kHz，18秒四核黑屏 sleeping-init、GPU关闭；18个运行采样
（每次含19路TSENS）最高39.2°C，恢复全部PASS，正常返回Android并确认屏幕OFF。
这里的频率是OPP请求，不是有效时钟测量；不能把短空闲当持续功耗验收。

out/thermal-cpucap-v3-ios-cpu 首次在大盘上传后40.4°C冷启动检查拒绝，未运行XNU。
独立Android熄屏冷却后，新一次 out/thermal-cpucap-v3-ios-cpu-cooled preflight39.6°C，
完整iOS、GPU关闭，降频核验通过并进入SpringBoard启动。30个运行采样最后到46.3°C，
controller1/sensor4（小核区域），触发本实验新增的保守45°C芯片退出门槛。
主机最后实时日志只见44.7°C，完整拼接日志才含46.3°C与STOP，因此以完整证据为准。
内核正常returned、请求全部恢复，Android screen/actual OFF；预算60秒但未完成60秒。
尝试前电池29.7°C，返回后一次Android电池读数30.9°C，不能代表iOS内连续或峰值温度。

短时芯片46.3°C不等于电池过热，不能单凭这个读数断言危险。本轮证明请求限制与
可恢复退出可用，仍不证明原始GPU发热完全修复。未新跑GPU，也不把无GPU46.3°C和
旧GPU53.8°C当严格可比的降温差值。当前新内核保留小黑屏idle用途，iOS/GPU启动被
manifest锁住，等待有实质改进的新候选。后续先排查EFI小核忙轮询和继承频率，
补充可靠电池温度，再继续GPU、真VSync、日常功能。没有放宽既有冷启动门槛。

### 2026-10-26：EFI 小核等待候选（原离线阶段）

详见 MARBLE-EFI-IDLE.md。新增显式 --efi-idle 的本核计数器事件流等待，替换
XNU 运行期间 EFI 的忙等暂停，并记录 WFE 等待耗时、最大轮询间隔和寄存器恢复。
不改小核 EPSS、电压或 PMIC；须先通过 64 次 loader-only 自检，再运行 18 秒小盘。
本次仅构建、故障测试和保存独立恢复点；ADB 仍 unauthorized，未启动新 XNU/GPU，
没有实际降温或触摸时延验收。旧热停 manifest 和运行计数保留，不解锁旧 iOS/GPU
启动档案。当前 Android 屏幕状态无法重验，不能声称已再次确认熄屏。

### 2026-10-26：EFI 等待实测与短时温度策略

后续 ADB 已授权。v1/v2 的三次尝试分别因芯片40.8、40.4、40.0°C被旧冷启动
门槛拒绝，均没有运行XNU/GPU。v2新增loader命令等待的短WFE lease，30秒待机观察
累计等待29.824秒、最大轮询间隔559us，无晚唤醒和恢复失败；不等于功耗测量。

根据用户说明，芯片短时40–50°C不作为电池过热证据。v3新增明确opt-in的18秒
黑屏小盘实验：起始芯片<50°C，>=55°C或连续>=50°C采样跨度达到10秒请求退出；
失效/停更温度也退出。仅限固定sleeping-init、无GPU、CPU/event lease，以及
180秒内的Android电池读数<38°C，返回后电池需<40°C。没有解锁旧失败GPU档案。

out/efi-idle-v3-idle-smp4实际四核运行18秒通过：18个运行期样本最高39.6°C，
最大采样间隔1,000,109us；WFE累计17.630528秒，最大轮询间隔465us、late=0。
CPU请求与mirror、等待寄存器恢复均通过，三个副核OFF，内核正常returned。
电池启动前31.8°C、返回后32.6°C；手机回Android且screen/actual OFF已确认。
本次新增XNU 1次、GPU 0次。证据和校验清单见out/efi-idle-v3-plan。

该结果仅验证短时空闲路径，未证明GPU发热或瞬态色块已修复；没有连续iOS电池
温度，不能保证中途峰值。下一步为可靠电池遥测与独立受限负载/启动候选，再继续
GPU渲染正确性、真实硬件VSync和日常功能，不重复旧热停候选。
