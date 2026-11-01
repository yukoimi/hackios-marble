# 新版 HakiOS 文档与 marble 复用核对（2026-10-29）

## 版本与来源

用户提供入口：https://hackios-web.vercel.app/en 。正文从该网站的公开源码固定下载，
不是凭搜索摘要判断。网站文章是参考资料；其中刷机、分区或固件抓取步骤没有被当作用户授权。
本任务仍限临时 fastboot boot 与 RAM 镜像，不刷分区，不访问 Android /data。

| 仓库 | 实际采用版本 | 说明 |
|---|---|---|
| oosexgang/hackios-web | bdb2e1aa74df233fb0a057e473bba43740b77780 | 47 个文档/导航源码文件，最后提交 2026-10-28 19:32:39 UTC |
| oosexgang/hakios-xnu-kernel，hakios 分支 | 7867dc140043cdc3151886d67735b3894414e7bf | 新版驱动与配置层，2026-10-28 19:06:35 UTC |
| 同仓库 main/legacy-hackios | 88513176b1e96ecf0b764cd62c6f4e6ac71345e3 | 旧版；不能拿 main 判断文档中的修复是否存在 |
| oosexgang/hakios-sm7325 | 04fbfff933f52faf203a55aca9dd5680092e6ec5 | redwood/sc7280 设备和 SoC 层；不是 marble |
| oosexgang/hakios-uefi | 687fa6c6a5a70f43184de92980e0a76eea5f2c4b | Mu-Silicium 公共补丁与分设备平台 |
| oosexgang/hakios-metal | 54a46fadec5bd9684ebd77f3a0834c84b5eb01a6 | 命令队列、缓冲区、blit；README 明确 shader 尚未接通 |

GitHub 的 oosexgang/hakios 地址本次 API 返回 404；新文档部分链接还指向此地址。
实际可获取实现位于 hakios-xnu-kernel 的 hakios 分支。按其 devices/manifest.conf 固定的
sm7325 与 uefi 版本也已核对，未整体合并或改动本地 origin。

固定输入与 SHA256 在 `out/upstream-docs-20261029/`：`revisions.json`、
`hakios-branch-revision.json`、`hakios-branch-sources.json`、`doc-sources.json`、
`kernel-branches.json`。331 个新分支源码文件另存于 hakios-branch/。

参考正文：

- https://hackios-web.vercel.app/en/docs/driver-review
- https://hackios-web.vercel.app/en/docs/driver-review/gpu-display
- https://hackios-web.vercel.app/en/docs/driver-review/adsp-battery-cpufreq
- https://hackios-web.vercel.app/en/docs/driver-review/sound
- https://hackios-web.vercel.app/en/docs/driver-review/wifi-bringup
- https://hackios-web.vercel.app/en/docs/porting/repositories

## 可直接复用的部分

| 部分 | 采用方式 | 本轮状态 |
|---|---|---|
| GPU/display G2 | 读前检查 offset、stride、最后一行和 allocSize；平面尺寸/乘积与 wrapped size 检查 | 已接入候选源码 |
| G5 | 用 64 位计算目的矩形；只遍历屏内行，保留原缩放坐标；彻底屏外直接跳过 | 已接入，并补 source 坐标有符号溢出拒绝 |
| G7 | 放弃创建者引用前保存 allocSize，避免出错路径释放后读取 | 已接入 |
| G10 | 任一缩放行分配失败，按原尺寸清理所有已分配的主/辅助缓冲 | 已接入 |
| ADSP/GLink F1–F4、F6、F8、F11、F12 | 复用长度/边界检查及消息、引用生命周期结构 | 已固定源码；待 marble 链路接入，不开启 redwood MMIO |
| 电量 IOPMPowerSource 发布层与 PMIC GET 消息 | 在获得 marble 真正响应后发布；消息格式可复用，属性编号逐项核对 | 未宣称 SOC 已可用 |
| 设备与 SoC 分层 | 最终整理 marble/sm7475 配置，保留板级差异和实际完成程度 | 暂不为迁移构建系统破坏现有可用镜像 |

显示移植仅涉及 MiniIOSurface.cpp 和 MiniIOMobileFramebuffer.cpp。保留已有 GPU final scale、
硬件计数器/blank-window copy、亮度、黑屏及触摸桥。没有直接覆盖整个上游 framebuffer。
上游 G8 把异步通知放在锁内，文档自己标注尚未用 iOS 实机验证；本轮未叠加该锁策略变更。
G16 的 GPU→CPU cache 一致性在文档中仍是 NEEDS-HARDWARE，并没有一个可直接取用的修复。
因此不能把这次边界修复说成已经找到全部闪块/闪屏的根因。

`tools/marble-display-bounds-test.py` 从实际生产文件提取函数体，无修改地在隔离 VM 中运行。
`out/upstream-display-bounds-v3-plan` 的 ASan/UBSan 测试包括 450 组裁剪像素对比、完全屏外
图层、极端坐标、源区域越界、stride/offset/allocation 边界、四处内存分配失败和重试。
新内核 SHA256：53ad16132669fd3a5de452952faa51628e23c35a3aaa19532a9837ef8335d6a3。
编译/链接完成；原有 check_all_exports 与三个 symbolset 检查仍失败，make 返回 2，
不能称完整构建通过。v1 的 unused-variable 编译失败与 v2 的构建脚本位置错误均保留，
v3 才是可测试候选。

## 不可照搬的硬件与协议

| 领域 | 上游 redwood | 本机证据与结论 |
|---|---|---|
| GPU | Adreno 642L / sc7280 | marble 为 FD725/Gen7；继续已有驱动，不替换成 A6xx 初始化表 |
| 音频上层 | APR，ASM/ADM/AFE，PRI_TDM_RX_0 | marble DT 的 qcom,gpr 使用 adsp_apps，SPF 与 gpr_dlkm/spf_core_dlkm 存在；先适配 GPR/SPF 服务，不能只换 APR 的 MMIO 地址 |
| 扬声器 | redwood 的具体功放、引脚、ACF 与 TDM 时序 | ROM 有 aw882xx_dlkm，DT 有上下功放 reset/pin 配置；必须以本机拓扑和校准数据为准 |
| 电量 | PMIC_RTR_ADSP_APPS；部分属性编号在 redwood 上测得 | marble 同名通道有 DT 证据；编号仍要按本机 qti_battery_charger.c 核对，Mu 15%/25°C 占位不可发布 |
| Wi-Fi | WCN6750，通过其 remoteproc/ath11k 路径 | marble DT 为 cnss-qca6490、PCIe RC0，ROM 含 cnss2；可复用网络栈/ath11k 思路，不能照搬 redwood 启动路径 |
| 振动 | AW86927，I2C GPIO，波形文件 | marble DT 为 qcom,hv-haptics@f000，ROM 有 qcom-hv-haptics；不会向 redwood 的 I2C 地址写寄存器 |
| 蓝牙 | 上游 device.conf 本身标 missing | 没有现成的上游完整驱动可以宣布完成；本机为 qca6490 |
| Metal | buffer/queue/blit 探针；还没有 shader 渲染 | 不拿它替代现有正常绘制的 GLES/CoreAnimation 路径 |

Linux .ko 是 Linux ABI 参考，不能直接作为 XNU kext 加载。用户 ROM 中已校验的固件与校准数据
继续留在本地 RAM 输入包，未发布。模块列表来自已有 catalog.json；是否实际挂接以本机 DT
为证，不能根据刷机包包含某个模块就认定它是本机采用的设备。

## ADSP 停机：新文档缩小了问题，尚未直接解决

v38 实机已返回 PAS auth 成功（EFI0/transport1/secure0），slave-kernel ready=0x6，
stop 请求后得到新的 stop-ack=0x10e。USB 最后进度是 PAS-metadata-shutdown-before；
没有 shutdown-return，故本地停机、载荷恢复、供电回收都没有通过证据。
Android 独立恢复、电池 32.7→33.4°C、熄屏已确认。这不能倒推原地恢复成功。

新版上游同样采用 SMP2P stop→PAS shutdown→AOP off，因而没有证据说再复制一遍 stop
握手就能修复。上游还会提前 QMP load_state:on，并按 handover 管理其代理供电。
本机 Xiaomi 固定 commit 48952ed36228217531482b39d5bef13e7fd808ec 的
qcom_q6v5_pas.c 表明 cape-adsp 的完整启动还包含 QMP、XO、带宽等资源；停止时先
request_stop，再开 SCM 带宽、PAS shutdown，最后回收并 QMP off。仅两个已验收的 NPA
请求不能代表这个完整生命周期。QMP/XO/SCM 带宽是待核对缺口，不是已证明的断线根因。

这些本机 Linux 来源及 SHA256 单独固定在上游快照的 linux-marble/references.json。
不把另一节点的 384 或 Android DT 的 corner 常量直接替换已验证的 Mu 配置 vote256。
下一步应先只读核对本机 AOP/QMP、XO 和 crypto_ddr 的原生状态/所有权，再构建可验收的
lease→handover→shutdown→release。禁止无变化重复 v38。

主机已补上 --pil-smp2p-boot 的提前拒绝；v34b 和 v38 镜像仍标 FAILED。
此前未完成的 retained-resource/cold-reset 方案保留作离线草稿，loader OEM 入口也禁用，
不会把刻意保留的资源伪装为 native release 或本地 restoration 成功。
`out/daily-upstream-audit-v39-plan` 两个 EFI 目标与 11 个 C 检查构建通过，
该镜像 firmware-only，尚未实机启动。84 项现有 Python 测试通过。

## 实机显示回归记录

- `upstream-display-panels-v1-live`：新内核成功进入中文桌面，目视截图有壁纸与 Dock 模糊；
  旧四图标模板因当前图标位置变化未通过，因此未发送预定手势，不能算 panel 回归成功。
  7/7 恢复、blank-window copy 检查、闲置黑屏及 Android 熄屏通过；
  电池 31.4→33.0°C，die 峰 50.3°C。后续使用已存在的唯一设置图标模板门控。
- `upstream-display-panel-focus-v2-live`：唯一设置图标匹配后实际打开通知中心、关闭、
  打开控制中心、底边关闭；两个关闭后桌面模板均通过。实际截图确认中文通知界面和
  控制中心模糊，未见先前红色异常；7/7 恢复、blank-window 检查、闲置黑屏及
  Android 熄屏通过。电池 33.0→34.1°C，die 峰 51.8°C；451 presents，
  counter failures/overruns/fallbacks 均 0，max_copy_us=2915。
- `upstream-display-pages-v3-live`：六次手势完整完成，实际进入负一屏、第二页、
  App 资源库并返回。保存 RWREC004 格式 135×300 的 489 个完整合成帧，
  已目视核对全部八张逐帧拼图及黑/绿候选邻域；这段保留窗口内未见异常黑绿矩形。
  最后一帧全黑对应显式闲置熄屏；初期白色 widget 占位随后被内容替换。
  记录 154.630–181.700s（内核时间），是缩小后的合成源，不是光学录像，
  不能保证未采到的瞬时渲染或屏幕扫描没有问题，也不能据此宣布稳定60 FPS。
  7/7 恢复、blank-window 检查与熄屏通过；电池 34.9→35.5°C，die 峰 53.0°C；
  506 presents，failures/overruns/fallbacks 均 0，max_copy_us=2855。

总体仍在进行中：不宣称音频、无线、蓝牙、相机、传感器、SOC、硬件休眠或光学稳定60帧完成。

## 恢复包源码完整性

v7 的文件哈希校验通过后，额外检查发现 sources.tar.gz 缺少两个被旧 ignore 规则排除的
当前 SMP2P 头文件；新闭包检查又截住此前同类的 display_dma、pil_memory、pil_segment
三份头文件（各构建目录的 source/ 快照仍有它们）。已让 .gitignore 包含所有 marble_*.h，
并让 checkpoint 工具在打包前、后都检查这类头文件的源码闭包。新恢复使用
`out/checkpoint-ui-peripherals-v8-20261029`；v6/v7 保留原状，不能把 v7 的旧源码归档
单独当作完整当前源码。
