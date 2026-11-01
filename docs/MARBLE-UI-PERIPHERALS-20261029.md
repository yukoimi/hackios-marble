# marble：中文、2×界面与外围进度

本轮用户要求：去掉异常黑框/黑壁纸，中文系统，修复控制中心停顿，继续外围驱动。
工作尚未完成。以下只记录已经取得的证据；提取固件不代表驱动已启用。
手机继续 fastboot boot 临时启动；不刷/擦分区，不访问 Android /data。

## 已验证的改动

- 独立 `out/marble-system-zh-v1.apfs` 补入本地保留 IPSW 的简体中文资源。
  `out/ios-zh-v1/verified.json` 校验 182 个文件、4,853,737 字节，并保留空 lproj
  目录供 CoreFoundation 发现语言。未覆盖英文系统盘。
- RAM 用户偏好 AppleLanguages 为 `zh-Hans-CN,en`，AppleLocale 为 `zh_CN`。
  `ui-zh-v2-panels-live` 及后续截图已显示中文桌面、日期与控制中心。
  中文设置应用已在后续 v25/v29/v31 截图中确认；实际拼音组词/候选输入仍需验收。
- 中文支持盘的 RAM 时区链接改为 `Asia/Shanghai`；持久 RTC 不变。
- 2× 显示报告 414×896 点、828×1792 像素，原生缩放 2。原先排除区域沿用
  3× iPhone X 的 209.333×30，UIKit 找不到匹配状态栏，显示 rdar:45025538 红条。
  `marble-ui-layout.c` 仅对这一组明确几何参数，将区域改为系统 Split828 自带
  的 227×32（原点 93.5,0）。方法类型及 20F66 构造函数地址均核对。
  `ui-zh-geometry-v3-panels-live` 截图恢复原生状态栏；v5 运行记录再次确认
  227×32 区域。未用覆盖色块隐藏诊断条。Preferences 也加入该兼容库。
- 恢复上游已有 `libredwood-media.dylib mediaserverd` 注入规则；从原源码重建
  `inject_media.c/camera_device.c/camera_isp.c`，源码快照和命令在 `ui-media-v9-plan`。
  本地 Windows 支持盘构建器此前漏掉这项上游默认规则。实际日志确认 portrait
  filter prewarm 等方法通过原有 ABI 检查并替换；没有启用 marble 相机驱动。

## 控制中心的有限对照

同样两轮设备时钟手势、同样 2×GPU、同样头两秒统计窗口：

| 目录 | 媒体服务退出次数 | 第一次打开中央画面变化 | 第二次打开中央画面变化 |
| --- | ---: | ---: | ---: |
| ui-zh-reset-v5-panels-live | 10 | 1008.2 ms | 1487.6 ms |
| ui-media-v9-panels-live | 0 | 355.7 ms | 210.6 ms |

由 `tools/marble-panel-report.py` 生成 `out/ui-media-v9-panel-comparison.json`。
这是缩小的 RAM 最终合成图中央区域平均通道变化 >1 的首帧代理指标，不是物理
触摸延迟、光学扫描测量或稳定 60 FPS。第一次关闭没有充分确认，不能宣布完整
面板验收。v9 后半段截图确认回到中文桌面，控制中心截图保留模糊。

v9 的七项资源恢复、黑屏、thermal 报告通过；电池端点 34.2→35.8°C。
后续结果保留在各独立 live 目录，不覆盖之前失败现场。

## 黑壁纸：静态兼容回退已显示，原生 Poster 仍未修复

- 增加 414×896@2 的原生 Stripe 模板并改默认蓝色 Stripe，单独镜像
  `marble-system-zh-stripe-v2.apfs`；仍然黑，不能作为修复版。
- SpringBoard 原生 restore-default 请求返回 accepted=1，但截图仍黑；接受请求
  不代表正确显示。
- 原生图片 setter 在 SpringBoard 主队列中同步调用导致黑屏转圈，v6b 为失败版。
- 独立进程 v7 补载 SpringBoardUIServices 后 v8 能找到 API，但同步调用仍未返回。
  v11 加入主队列运行后，确认公开 API 的地址为 0x1a34000b8；反汇编发现其先
  同步查询 ManagedConfiguration 的 isWallpaperModificationAllowed。
- 先前图片/选项参数格式不匹配 20F66；已改为两张 NSNumber appearance 字典。
  私有 unchecked 实现不在导出表，v12 的 dlsym 正确失败，未调用。
  v13 只在公开 wrapper 地址匹配时解码其 BL 目标，验证为 0x1a33ffe0c 后调用。
  此入口只用于未配置管理服务的独立 RAM 系统壁纸请求，不改系统策略代码。
  v13 返回 accepted=0，截图仍黑；不是成功版。
- 图片来源是本地 IPSW Clownfish 的 Background.HEIC，离线转 PNG 并裁成 828×1792；
  只在本机保存，不是从网络下载图片或把图片叠在 UI 上。
- 首个 PNG 支持盘超过输入 mailbox 前的 8 MiB 限制，构造器在启动 XNU 前拒绝。
  失败证据保留为 v6，随后缩小图片；preview 现会在操作手机之前检查盘大小。

## ROM 外围材料与电池审计

`out/peripheral-material-v2-plan/catalog.json`：ROM 312 个 Linux 模块，选择 48 个
与外围相关的模块作为 ABI/源码参考；音频、蓝牙、传感器等材料共 254 文件、
86,661,792 字节，主机解包后逐项大小/SHA256 校验。未将 ELF/Linux 模块冒充 kext。
另从裁短的 FAT16 bluetooth.img 提取 113 个固件/配置文件；每个分配簇均限制在
实际镜像长度内，未假定镜像缺失部分是可用内容。

PmicDxe SHA256 `c19051e7fb0ca47f7b504cd87e637e9b76393cbf50278040a212e169997f5f75`：
FG 表 RVA 0x2c3d8，revision 0x10018，GetBatteryStatus 为 0x2016c。二进制将
SOC 写成常数 15、温度写成常数 25，并访问偏移 16；旧公开结构只有 16 字节。
因此既不能把这些常数当真值，也不能直接用旧 ABI 调用。此次未调用 FG getter。
审计与参考头文件保存在 `out/pmic-direct-v1-plan`。

内嵌 uefipil.cfg 的 FULL_ADSP2 指定 adsp2、PAS 1、0x9fd00000/0x3100000，
与已有 DT/ROM 重定位计划一致，但这只是配置。PILProxyDxe 是代理层，旧公开
PIL 协议 GUID 未在本次扫描的 PE 中找到；不能推断同名代理就是可调用加载器。
需读取实际协议发现结果后决定可复用的加载/电源生命周期。

音频、Wi-Fi、蓝牙、传感器、真实电池遥测、硬件休眠仍未完成，不能宣称日常功能全好。
禁止重新启用已失败的整个 SMEM 区域扫描。保留现有温度保护和资源恢复验证。

## 本地验证

UI/媒体/helper ARM64 构建成功，日志保存在各 plan 目录；45 项既有 Python 回归通过。
2×源内核和 loader 延用已验证版本，没有因界面工作改变时钟或电压。
此前源内核编译/链接完成，但上游导出检查仍失败；不把整个 make 称为成功。

## 本轮后续：壁纸与中文验收

- v14 原生 color setter 返回 accepted=1，仍然黑。v15 实机查询发现
  `_wallpaperViewForVariant:0/1` 都为空，`_SBWallpaperSecureWindow` 存在且可见。
- v16 `--ui-wallpaper-static` 把本地 IPSW 的静态背景放入该原生壁纸窗口，
  不接管图标/应用窗口。屏幕几何及 20F66 方法类型/地址匹配才启用。
  `ui-wallpaper-static-v16-panels-live/ui/native-ready-end.png` 已显示壁纸，Dock
  有背景着色和模糊。此轮通知中心压力触发 die 55°C 保护，资源恢复 7/7，
  Android 熄屏；电池端点 37.1→37.8°C。不能当作完整面板/温控验收。
- `ui-locale-v17-live` 再次显示静态壁纸及中文 Spotlight/键盘按键“空格/搜索”，
  自动序列、黑屏、资源恢复 7/7、thermal 通过，电池 36.1→37.2°C。
  这尚不能证明拼音候选词输入正常。
- v17 的设置页截图实际仍是桌面：News 的异步注册会改变图标排列，不能按
  中文/英文简单判断坐标。已撤回该坐标假设；rwtouch 增加上游 battery_test
  已用的 `SBSOpenSensitiveURLAndUnlock(prefs:,true)`，从独立进程启动设置。
  新候选 v18b 正在独立验收，不能预先称设置页已通过。

## ADC：真实电池温度/电压首次读取成功

- supplied Mu 的 ADC table RVA 0x2b938，revision 0x10004，与保留 EFIAdc.h
  一致。GetChannel/AdcRead 分别为 0x4074/0x40d8，通道结构 8 字节，结果 20 字节。
- `daily-adc-v4-discovery-live` 只查通道，7 项存在，无转换；旧 FG 的常数
  15%/25°C 继续禁止当作测量值使用。
- AdcRead 经 1c5b0→DAL VADC 1ced0→1d1c4→24e28：配置、轮询、读结果、关闭。
  SPMI 读重试 5 次；现有设备配置转换超时为 500000us。v5 本地 100000us 限制
  正确拒绝，未转换。v6 使用该原有超时，并要求总耗时 <600000us。
- `daily-adc-read-v6-cool-discovery-live`：BATT_THERM result VALID，37°C，
  382266uV，code 5892，977us；VBATT VALID，4431mV，4431291uV，code 22767，899us。
  Android 电池前/后 36.6/37.0°C，电压约 4.44V，数值接近。
- 这是正常 ADC 转换，会有 ADC mux/enable 写入及关闭，不是“PMIC 零写入”。
  未修改充电、JEITA、阈值、校准、电压或时钟。仍只有 idle-only 探测入口，
  尚未接入 XNU 电源服务或运行中的连续电池监控，SOC 未解决。

## 原生 PIL 与只读 ROM RAM 文件系统

- supplied Mu 的 modern PIL GUID 1f4f1da3-4d8d-4894-ba2a-13d3207f9739 确实缺失；
  ADSPProxy/SLPIProxy/GLink 存在。不能用旧 EFIPIL.h 的 name-only ABI 调用 modern 表。
- 复用 Project-Silicium/Device-Binaries marble PILDxe，固定提交
  a1501e0c2e642d6ac0d8f52c85af2a67b75a7799，原始 SHA256
  1cac0e5c3e73c2dd756bcbe19344efa6ecea2e1f45e88fdc6255cc2e79fbff64。
  同提交 PILProxyDxe 与 supplied Mu 完全一致；PmicDxeLa.patched 也一致。
- 原生 entry 会自动加载 AUTO 中的 ABL，因此不运行原始二进制。
  `marble-pil-prepare.py` 只修改副本 RVA 0x15d8 的一条指令，跳到 0x1640，
  越过 AUTO 的解析与执行。派生 SHA256
  de49a2969d4dfe0a84f3be4b4cdc156be32120aaceb19b100e9130a22b21db42。
  LoadImage/StartImage 前校验派生体积和全文件指纹，调用期间有 30s 固件 watchdog。
  安装尝试后禁止启动 XNU，必须先冷启动新 loader。没有刷固件或启动 ABL。
- v7 实机安装成功，modern revision 0x10003、六个函数地址全部匹配；watchdog 清除。
  v8 通过第 6 槽 CHAR16 名字接口复制 0x118-byte config，未调用 load/PAS。
  第 5 槽是数字 subsystem ID，不能混淆。报告 `daily-pil-config-v8-report.json`。
  ADSP2 的 PAS1/0x9fd00000/0x3100000 匹配；SLPI 原生路径是 slpi，实际 Android
  选择 slpi2，后续必须修正，不能直接启动。
- `pil-ram-files-v1-plan` 将用户 ROM 已校验的 27 个 ADSP2、24 个 SLPI2 文件打包
  为 30057264-byte 只读 RAM 目录，逐文件 SHA256 与原提取 manifest 匹配。
  SHA256 2848fc433f124550c95d72e5de2697858d68be3070ddf2b5d8816448eca9dc19。
- EFI RAM 文件系统提供独立软件 handle、唯一类型 GUID、只读 SFS 和设备路径。
  不提供物理块读取或任何文件/块写入。包含边界/溢出/重叠/EOF/seek/只读策略测试，
  Windows 测试及 Linux ASan/UBSan 通过。
- `daily-pil-ramfs-v9c-discovery-live` 原生 PIL OpenFile helper 从 RAM 打开
  adsp2.mdt=5964 字节、slpi2.mdt=5724 字节，完整内容指纹匹配；opens=closes=5，
  reads=2，file_writes=block_reads=block_writes=0，四项临时协议全部卸载。
  电池 35.4→35.5°C，返回 Android 熄屏。没有 DSP 启动，也不是音频/传感器功能通过。
- v9 O1 构建暴露 embedded FAT 容量限制；v9b 也未生成 boot image。
  v9c 使用显式 `--efi-opt Os`，保留原固件布局，编译、镜像检查及实机探测通过。

继续缺失：DSP 的装载/认证/停机生命周期、真实 SOC、电源服务、音频、QCA6490
PCIe/MHI Wi-Fi、蓝牙、传感器和硬件休眠；静态壁纸兼容不等于原生 Poster 修好。

## v10b 失败与后续缩小阶段

- `daily-pil-adsp-v10b-cycle-live` 为一次原生 ADSP2 装载/2s/停机尝试；USB 失去
  响应，未取得关键命令输出或停机记录。Android 自行恢复，电池 37.6→37.5°C，
  熄屏确认。bootreason 是 bootloader，不能据此判断是否 watchdog 或走到了哪一步。
  此次 DSP 是否启动/关闭未验证；不能按成功或“没有任何硬件动作”处理。
- 对应 loader-boot.json 标记 FAILED，控制器拒绝重用；完整 cycle 的 USB 命令和
  主机选项已经禁用。未在同样条件下重试完整启动。
- 主机现在在超时时保存 fastboot INFO 原始输出，并保留原命令异常，避免随后
  日志传输失败覆盖前一个错误。
- v11b 单独验证 0x9fd00000..0xa2e00000：完整 51MiB 属于 EFI type0 reserved，
  不与 0x81200000..0x85200000 上传区相交，逐 4KiB AT 检查恒等映射。只查翻译，
  未读取该区域内容。分阶段 INFO 能传回主机；正常恢复 Android 熄屏。
- `pil-header-only-v3-reloc-verified-plan` 为另一份明确隔离的派生 PILDxe：
  保留原先跳过 AUTO 的指令，在被跳过的 AUTO 区放入 `mov w0,#7;ret`，并把
  ELF pipeline 的 PAS-init callback 指向该拒绝桩。原 callback RVA0x23d0，
  新桩 RVA0x15dc；验证 0x12dc0 的原有 DIR64 base relocation。认证实现未修改，
  该派生流程在认证前固定中止。SHA256
  cc310ec13454b595bfc3d5302e41de9eb5eecc97cb490412db47cca6d5b2fc44。
- v12b 实机原生 MDT import：5964 字节逐字节匹配 ROM，native_running=0，
  然后调用原生 3838 的 non-running metadata 清理路径，四个缓冲指针归零。
  临时协议全部卸载，opens=closes=9，file/block writes=0。
- v13 再单独调用原生 ELF parser 1ed8：parse_rc=0，27 个程序头，ELF/header
  副本存在，随后的原生清理通过。PAS_init=auth_reset=dsp_start=0。
  这把后续排查范围缩小，但不能证明 v10b 一定在哪个 SMC 上复位。
- supplied Mu 的 ScmDxeLA 已只读提取到 `scm-native-v1-plan`；SHA256
  e6ff705509161f8b0a063b929422b35ea3c997dac9917b3683bac2e647095812，
  供下一步核对 77ed108d-8524-4b8b-9d2e-34987aecb9c1 的 Call2 ABI。尚未新发 SCM。

## 中文设置页尚未完成验收

- v18b 独立进程的 URL 请求没有取得有效打开证据，不能把它称为通过。
- v19 图像模板能在两种图标布局中识别设置，但实际点击时布局又变，截图仍是
  桌面。该轮自动序列通过不等于设置页面通过。
- v20 在 SpringBoard 内核对 `launchApplicationWithIdentifier:suspended:`
  类型 B28@0:8@16B24 后发起启动，accepted=1，Preferences 进程拉起；截图仍黑。
  此轮 die55°C guard 停止，资源恢复 7/7，电池 37.4→37.8°C，Android 熄屏。
- v21b 加入有界、未挂起主线程的栈采样和 app stderr→console 路由。Preferences
  构造函数、2×几何兼容均执行；2/5/8/12s 主队列回调有响应，5/8s 主线程在系统
  runloop/Mach 等待中。不是已证明的主线程死锁。需要继续区分 scene/view 构建、
  前台激活和绘制缺失。截图仍黑，不能宣称中文设置已完成。

## v22–v24：前台窗口与合成证据

- v22 有界主队列诊断确认 Preferences applicationState=0、UIWindowScene
  activationState=0，唯一 UIWindow 可见、key、414×896，PSSplitViewController
  已加载。144 个视图节点包含设置列表/中文标签。因此不是已证明的主线程死锁或
  未创建设置界面。画面仍黑，中文设置未通过视觉验收。
- v22 资源恢复 7/7、空闲黑屏、thermal 通过，电池 34.6→36.1°C，Android 熄屏。
- v23 确认 Preferences 非 secure context commitId=14，UILabel/图像图层
  contents 非空。SpringBoard 图层总预算先被桌面耗尽，未取得完整 host 路径；
  v24 改为选择 switcher/root-scene 窗口、每窗有界遍历和精简输出。
- v23 die55°C guard 停止，未通过热验收；资源恢复 7/7，Android 熄屏，
  电池37.3→37.9°C。保留失败证据，不自动重跑同版。
- 修正通用 userland/build.sh 的 rwtouch CoreFoundation 链接依赖。

## v14–v15：resident SCM 清点与可用性查询

- v14 新增 idle-only `oem scm-info`，精确匹配 supplied Mu ScmDxeLA 的
  modern table RVA0xd130、revision0x40001、10个函数；实际 Call2 RVA0x3cb4，
  排除误用旧0xd0d8/0x1858表。清点本身不调用协议方法或SMC。
- v15 只经精确表查询0xc2000601，arginfo=1，目标02000201/202/205/206；
  四项均status=0、transport sentinel=1、secure result=1，耗时6/2/2/2us。
  表内Call2自动平衡TPL；30s watchdog已清除。
- 返回结果结构已按反汇编确认：results[0]是transport sentinel，results[1]
  才是secure x1；不可把sentinel当PAS返回值。此轮未调用被查询的PAS函数，
  没有初始化/认证/复位/启动DSP。
- v14电池36.1→37.0°C，v15电池37.9→38.0°C，两轮均返回Android熄屏。
  因温度升高，后续GPU测试先等待降温，不放宽阈值。
- 45项Python回归通过；两版EFI构建及7项host C检查通过。

## v24b–v26：黑屏缩小到最终 GPU 缩放

- v24b SpringBoard SBMainSwitcherWindow 中 CALayerHost 的 context ID 与
  Preferences 的 UIWindow context 完全相同，host frame414×896、hidden0、
  opacity1；app context commit26。进程、前台、视图和context挂载都有证据，
  但本版截图仍黑。die55保护退出，恢复7/7，Android熄屏，电池36.9→37.2°C。
- v25保持GPU合成，只移除最终GPU缩放helper（回到现有CPU缩放路径）。
  `out/ui-cpuscale-v25-live/preview.png` 首次完整显示中文设置页面，包括
  “设置/飞行模式/通知/声音与触感/通用/显示与亮度”。不是仅从标签推断。
  因此最终缩放路径是明确的故障范围；不能再称为未创建设置窗口。
- v25依然触发die55保护，完整自动序列未完成，不能称整体热/滚动验收通过。
  资源恢复7/7，Android熄屏，电池37.9→38.0°C。原始失败证据保留。
- v26候选复用GPU缩放时重新绑定外部IOSurface，并增加缓存GL名字复用和
  828×1792→1080×2400的实机自检。只构建helper，源内核不改。
  此时仅完成构建和主机handoff/ASan/UBSan/llvmpipe检查，尚无实机通过结论。

- v26十项FD725自检通过（含整屏），重新绑定后实际页面仍黑，仍属失败候选。
  die55退出，恢复7/7，电池36.9→37.8°C，Android熄屏。
- v27有界9点CPU源/输出对照，早期绝大多数抽样different=0，但源RGB本身为黑；
  同进程CPU图案自检正常。这把客户端交接纳入调查，尚不能证明具体硬件根因。
  该对照不是原子快照；记录seed前后值，不能把所有不同样本当缩放计算错误。
  同样die55退出、恢复7/7，Android熄屏，电池37.8→38.0°C。
- v28为独立内核候选：共享全局GPU地址空间中，只保留初次reset，后续依赖Mesa
  每条命令流自身restore；保留ring缓存prologue和fence。不改本地默认源文件，
  VM镜像构建后源文件也恢复。尚待硬件结果，不能推广为通用context策略。

## v16b–v17：原生认证元数据与 PAS 初始化/关闭

- v16b验证原生state+0x40的认证缓冲（不是pool副本+0x30）：4KiB对齐、
  恒等映射、PAR attribute0x44（normal noncacheable），5964字节与ROM完全一致。
  原生metadata清理成功，未调用PAS。电池36.9→37.0°C，Android熄屏。
  v16首次在Android电池38.1°C时被预检拒绝，无临时启动，原失败记录保留。
- v17在精确modern SCM表、四项availability、元数据/真实ADC/TSENS守卫之后，
  单独调用PAS1 init-image0x42000201/arginfo0x82，随后shutdown0x42000206。
  两次EFI status=0、transport=1、secure result=0，耗时21475/570us，dirty=0。
  原生metadata四项指针清零、临时协议释放、watchdog清除，电池37.1→37.0°C，
  返回Android熄屏。
- 不经过native23d0的错误dump/reset路径5f30；没有payload复制、认证复位或DSP执行。
  这缩小v10b失败排查范围，但不能反推v10b一定失败于哪一阶段。音频仍未启用。

## 最新边界与待测候选

- v28共享VM/context-reset对照没有消除黑屏；再次触发die55退出，恢复7/7，
  电池37.6→38.0°C，Android熄屏。默认源内核仍为62d046a0...，未推广此候选。
- v18加入可选PAS memory-setup(1,0x9fd00000,0x3100000)后shutdown；首次实机
  在真正PAS调用前被ADC BATT_THERM=38°C守卫拒绝。当时mem-setup没有运行。
  metadata/四个临时协议清理成功、dirty0、读写计数无物理块访问，Android熄屏，
  电池37.8→37.8°C。需降温后复测，不能把“请求过”算通过。
- v29独立userspace候选：GPU缩放客户端在15秒后才打开GPU，隔离初始化顺序。
  后续实机结果见下；不能从一次成功直接认定根因。
- checkpoint-ui-peripherals-v2-20261029已创建并验证1944文件；包含到v28失败和
  v17成功的证据，v18首次守卫拒绝发生于该快照之后。整个工程仍IN PROGRESS。

## v29/v31–v33：GPU 缩放恢复与正确的启动验收门槛

- v29 的中文设置和滚动设置均有完整画面；原稳定内核不变。最终 GPU scaler 至少
  480 offers、479 完成，1 次晚到超时丢弃后恢复，disabled=0。抽样源/输出有颜色
  且相符。native-locale 全序列、空闲黑屏、七项资源恢复和短测 thermal 报告通过；
  die 峰值54.6°C、电池37.4→38.0°C，返回 Android 熄屏。不是稳定60 FPS证明。
- v31 将固定延时换成 compositor 首帧 glFinish 完成后发布的 Darwin state=1。
  producer 和 consumer 均status0，scaler十项自检、注册和实际完成有日志，中文
  设置截图正确；不是仅靠CPU回退显示。但 die55 提前退出，完整序列没有通过，
  恢复7/7，电池37.6→38.0°C，Android熄屏。
- v32 改为通知中心/控制中心序列。初始截图仍黑，通知中心标记截图实际是启动
  Apple logo；这是失败验收，不能当控制中心画面。unlock请求在SpringBoard主队列
  长时间排队，原测试25秒计时不等于界面就绪。scaler至少120完成，die55退出，
  恢复7/7，电池37.7→38.0°C。recording.bin只有合法空头、0帧，不能声称录到了动画。
- v33 新增 opt-in UIKit UIApplicationDidFinishLaunchingNotification → Darwin
  state → rwtouch wait-state 测试门槛，不再把固定warmup视为就绪。实机observer=1、
  native通知status0，consumer确实等到state1，随后解锁和壁纸附着。但在native-ready
  截图前die55退出，控制中心/连续动画仍未验收。恢复7/7，电池37.8→38.1°C，
  Android熄屏；近期不再温热时重复GPU启动，保留失败记录与全部保护阈值。

## PAS v18c/v19：内存设置与首段 ROM 装载实机通过

- v18/v18b 均在ADC温度守卫拒绝，没有PAS调用；降温后v18c通过。Android预检
  36.5°C，ADC37°C；PAS init、memory-setup、shutdown 均EFI0/transport1/secure0，
  分别21807/1122/996us。dirty0，metadata和协议释放通过，返回Android熄屏。
- v19 首次Os构建超过原FAT空间，未操作手机。v19b改用明确的Oz编译，保留原
  firmware布局、BootShim、DTB及103个其他固件文件；八项host C检查通过。
- v19b用原生PILDxe3020装载adsp2.b01，原authentication metadata保持不变；独立
  116字节metadata视图限制phnum=2，只可访问首段。重定位状态显式绑定并恢复。
  在成功PAS memory-setup之后，保存目标原8KiB，原生装载6760字节，1432字节
  尾部清零；读回与ROM一致，原8KiB恢复且校验通过。1次RAM file read，446us。
  PAS init/memory/shutdown 为21709/1120/907us，全部secure0；metadata四指针
  清零、11次open/close平衡、无物理块读写、dirty0。电池36.9→37.0°C，Android熄屏。
- v20c 完整装载候选已构建：25个LOAD区域、24个文件共22712520字节；显式补齐
  native3020跳过的filesz=0 BSS，语义对照保留的marble Linux mdt_loader.c。先校验
  边界、重叠、所有split文件和映射，备份0x3100000字节，装载/读回/恢复后shutdown。
  此时尚待实机；后续结果见下。不会进入native329c（包含PAS auth-reset与fatal dump/reset路径），
  不执行DSP；音频、网络、蓝牙、传感器仍未可用。

## v34/v35：实际桌面和控制中心可见，关闭手势未完成

- v34 在 SpringBoard 启动通知之后，再以实际设置图标模板确认桌面，避免在黑屏/
  Apple logo 上注入测试。截图可见中文桌面、静态壁纸及 Dock 模糊；控制中心模块
  和中文“未在播放/专注模式”完整可见。仍可见左上黑色背景块和空白小组件。
- v34 至少360次GPU最终缩放完成，disabled=0。短测thermal通过、die峰54.6°C，
  电池37.0→37.3°C；七项资源恢复、空闲黑屏、Android熄屏均通过。
- 控制中心关闭截图仍是控制中心，不能把ui_actions_sent=4当作完整功能通过。
  v35将上划起点改为2398（旧2375）后，通知中心已实际返回桌面，控制中心仍未
  关闭；独立图标模板复核确认二者结果。v35 thermal通过，die峰54.2°C，电池
  36.7→37.6°C，恢复7/7。后续控制器另存动作发完与实际返回桌面的验收字段。
- v35只读界面树发现 SBFStatusBarLegibilityView 及其原生渐变视图标为opaque，
  没有背景色/contents；这只是定位证据，还不是黑块根因。v36加入渐变颜色alpha
  诊断和严格几何/类名下的候选opaque校正，未隐藏状态栏或移除渐变；结果待验收。
- GPU呈现仍是空白期CPU拷贝；140帧v34降采样录像不是面板FPS/光学撕裂证据。

## v20f/v21/v22/v24：完整 ADSP2 固件装载与内存属性资格

- v20c在目标内存访问前拒绝；v20e又被ADC38°C守卫拒绝。v20f明确诊断出内存
  属性分界：5888页Normal NC(0x44)，6656页Normal WT(0xba)，分界0xa1400000。
  原Mu MemoryMapLib把分界后的区域标为Display Demura，而附加DT把整个
  0x9fd00000+0x3100000定义为adsp_region；不能直接放开属性校验或覆盖显示区域。
- 从已固定Lineage显示源码取得 sde_hw_color_proc_v4.c、sde_hw_dspp.c、
  sde_hw_sspp.c，URL/hash保留在display-demura-audit-v1-plan/sources.json。
  复用其Demura status位与SSPP只读寄存器定义，地址严格来自本机DT。
- v21b通过已验证的显示总线时钟租约，两次读取两个Demura状态和八个SSPP源，
  20ms间隔稳定：Demura均禁用，仅pipe0取数b8000000/1080x2400/stride4320，
  其他管线/额外plane为0，均不重叠DSP范围。时钟引用恢复；电池35.9→36.0°C。
  后续probe增加30秒watchdog，并以精确单GOP形状作资格，未知管线一律拒绝。
- v22完整装载前、PAS前及恢复后均重新确认上述显示形状；只接受本机明确的
  NC/WT地址分界，保留dc civac和原内存备份。24文件读回、25区域清零、原内存
  恢复全正确，PAS shutdown secure0、dirty0。但耗时20.037486s超过当时5s预算，
  整轮仍PASS0。电池36.8→37.0°C；数据正确与整个候选通过分开记录。
- v23/v24将本地正常DDR诊断读取/清零改为精确边界的64位访问，避免非缓存区
  逐字节bus交易；不改变全局memcmp或任何MMIO。增加16×16对齐、头尾边界与
  坏字节检查。完整操作预算15s，仍保留30s watchdog和所有温度/存储保护。
- v24实机通过：save664762us，native load12257us，readback1264761us，
  restore1307746us，总3493908us；24/24文件一致、25/25清零、原内存恢复，
  三次显示资格均通过。PAS init/memory/shutdown=21160/1123/1419us，secure0，
  metadata清理、57次open/close平衡、无物理块访问，电池36.1→36.4°C，Android熄屏。
  stage-report.json全部通过，但audio_acceptance明确false：尚未auth-reset或执行DSP。
- 原生PILProxyDxe的ADSP/SLPI GUID实际绑定8160/8170表和1750/1874回调，
  不能错配为8130/8140。实机已匹配。借用原生1dcc查询DT元数据，PAS1/PAS12
  配置均存在，各2个resource、vector_count0；尚未执行任何NPA投票或代理事件。
  v25b补充有界资源名字/配置level打印并实机通过：两个PAS均为
  /vcs/vdd_lpi_mx 与 /vcs/vdd_lpi_cx，各configured_vote=256，vector_count=0。
  没有实际投票；电池36.8→37.0°C，Android熄屏。需要继续单独验证资源生命周期、
  安全停止和DSP执行；不能据此声称音频/传感器/网络已经启用。

checkpoint-ui-peripherals-v3-20261029已验证2761文件；v20f及以上新结果在其后，
需要新的独立快照保存，不能修改v3清单伪装覆盖全部结果。

## v36/v37：渐变诊断实际执行，控制中心收回仍未通过

- v36在状态栏检查执行前触发die55保护；没有颜色诊断或opaque修改日志，
  不能视为修正已执行。恢复7/7、Android熄屏，电池37.1→37.6°C。
- v37将一次性检查安排在SpringBoard完成启动2秒后，旧38秒timer仅作兜底。
  实机确认两个SBStatusBarLegibilityGradientView的原生layer是CAGradientLayer，
  colors=31。旧16节点上限导致valid=0、mutated_nodes=0；仅前16节点alpha
  为0..0.025，不能据此前缀推断全部节点。顶端黑框仍可见。
- v37真实中文桌面、壁纸和模糊Dock可见；通知中心上划返回桌面，控制中心
  650ms上划后仍未返回。四个命令均送达，但ui_sequence_complete=false。
  输入日志显示控制中心关闭手势670ms，bridge release_lag=1ms，HID队列16us，
  不支持把该次失败归因为USB拉长手势或输入桥长时间阻塞。
- v37短测thermal通过，电池36.3→37.0°C；7/7恢复、空闲黑屏及Android熄屏。
  硬件counter下486次空白期拷贝、failures=0、overruns=0；仍非IRQ/pageflip或
  光学稳定60FPS证明。246帧录像只覆盖部分测试时间，不能覆盖全部手势。
- v38b把颜色校验上限扩为64并要求遍历全部节点，保留精确类/414×100几何/
  无背景色的资格；原生渐变颜色不改。另加只读时区日志定位UTC显示，待实机。

## v26：NPA全部表项匹配；尚未创建客户端或请求供电

- 从用户Mu内嵌NpaDxe提取并固定SHA256
  9ced898ce160f69aede9ce1ad93dfef9223e9c280dc3ec99616861eae56e9c09。
  只读审计在out/npa-abi-v1-plan，协议79d6c870-725e-489e-a0a1-27e7a5d0cb35，
  表RVA120b8、revision10003。v26实机全部54个入口精确匹配。
- CreateSyncClientEx入口1448是无This六参数，输出句柄写到x5；wrapper即使
  underlying返回NULL也返回EFI0，后续必须另验句柄。Complete1474和Destroy15e4
  均有真实资源回调路径，不能因为未显式IssueRequiredRequest就声称完全无副作用。
- v26没有调用任何NPA函数，只再次确认两个PAS的DT配置和ADC读数，Android恢复
  熄屏，电池37.7→38.0°C。此后先冷却再做GPU测试，没有放宽温度保护。
- v27候选仅有界读取已初始化资源图及插件/客户端/状态字段，不调用NPA或插件，
  构建及10项host C检查通过，待硬件。音频、Wi-Fi、蓝牙、传感器和真实SOC仍未完成。

## v38b/v39b：黑框素材根因与上海时区修正

- v38b的31个颜色节点全部验证，alpha=0..0.05，四个opaque节点确实校正，
  顶端黑色区域仍在；未把该候选当作修复，后续v39b关闭alpha修改。
  控制中心上划仍未收回。恢复7/7、thermal通过、Android熄屏，电池36.5→37.4°C。
- 离线直接查看stock-background-2x.png，发现纯黑顶端已经包含在素材里；先前仅沿
  状态栏视图排查遗漏了该输入。这张图来自分层Clownfish的背景纹理，不能当完整
  原生Poster画面。wallpaper-static-v2-plan裁去顶端128像素，再aspect-fill到828×1792；
  不遮挡状态栏、不隐藏时间，左上180×80抽样14400像素中纯黑数为0。
- v39b实际desktop-confirmed.png已经显示无顶端黑框的中文桌面与模糊Dock，时钟06:41、
  日历周四29，与本机上海日期相符；仍有两个白色小组件占位，不能声称桌面全修复。
- v38b日志libc localtime=上海，NSTimeZone却GMT。RAM时区link此前直接指向
  /usr/share/zoneinfo.default/Asia/Shanghai，改为保留原生TZDIR前缀的
  /var/db/timezone/zoneinfo/Asia/Shanghai后，v39b实机CF_zone=Asia/Shanghai、
  CF_offset=28800。没有改UTC epoch或持久RTC；旧系统镜像hash不变。
- v39b在首次控制中心截图前触发die55，测试失败；没有完成手势trace或关闭验收。
  seven restores全过，Android熄屏、电池36.7→37.0°C。保留原温度限制，先冷却，
  下一版不启用已无必要的全窗口状态栏树诊断。不要自动热机重试。
- v27/v27b只读NPA资源图拒绝。v27b确认首个DAL link为fe5fbd44（4字节对齐），
  通用ADC字读取器要求8字节，因此在真实对象读取前拒绝。v27c另加NPA专用的
  对齐字拼接读取，逐字和跨页仍检查恒等RAM映射；没有放宽ADC或MMIO路径。
  其构建通过，等待冷却后单独硬件验证。两次拒绝均恢复Android熄屏，无NPA调用。

- v27c后续实机通过：完整遍历82个link并正常终止，找到两个LPI资源；两者
  state=256、active_max=480、client_head=0、supported_clients=0x844，create/destroy
  plugin callbacks均为NULL。没有创建客户端、投票或DSP执行；电池36.9→37.1°C，
  Android熄屏。这也表明不能把“未投票”解读为当前资源level=0。

## v40/v41b：控制中心非边缘上划通过；底部系统手势仍待修复

- v40去掉不再需要的全窗口状态栏树诊断，保留小范围UIWindow事件观察。
  控制中心可见；底部上划未关闭，空白处轻点已返回桌面。上划触点895.5pt到达
  SBControlCenterWindow，24ms后窗口收到cancelled；当时CCUIDismissal状态Began、
  两个system gate状态Ended。该记录只定位分发分支，不证明系统识别器就是根因。
- v40 thermal通过、die峰53.8°C、电池35.5→36.0°C；资源恢复7/7、黑屏/Android熄屏。
- v41b只读观察CCUIDismissalGestureRecognizer的setState，保留原实现，未更改
  enabled/cancellation/recognizer policy。body起点2160px=806.5pt（模块下空白处）
  的650ms上划，状态0→Began→Changed→Ended，translationY=-486.5pt，
  velocityY=-644.364pt/s；真实control-close.png已返回桌面，图标模板通过。
  此次body序列通过不代表底部edge路径修好；后续须同镜像edge对照。
- 同截图天气小组件已绘出“无天气数据”，日历已绘出星期四29/今天无日程。
  先前白色占位在这一轮异步完成，不能据此认定过去每次白块都是GPU画错。
- v41b thermal通过，电池35.9→36.6°C，恢复7/7、空闲黑屏、Android熄屏。

## v28/v29：零请求客户端实机通过，实际投票释放后未恢复原基线

- v28同时创建两个required(0x40)客户端，检查各自resource/type/零pending及链表头，
  逆序Destroy（包含native隐式Complete），两条原空链表及state256/max480/aggregate0
  均恢复，active0/dirty0、watchdog清除，51us。ADC37°C；Android36.8→37.0°C熄屏。
- v29显式从resident PIL_ADSP DT确认两资源level256，再请求100ms；两次调用均EFI0，
  aggregate=256、actual cached state=256。两个Destroy均EFI0，链表头回0，但原状态
  检查失败。整轮PASS0、dirty1，保留watchdog，未启动DSP/未进入XNU；host拒绝盲目
  再调用协议，由watchdog复位后观察到Android，再熄屏，电池35.9→36.4°C。
- v29恢复记录使用短路表达式：打印max=0、aggregate=ffffffffffffffff可能只是后续
  字段未读取的默认值，不能当真实资源值；cached state=0也需完整独立读回确认。
  原生PILProxy释放1b8c调用Complete后Destroy；Vcs plugin24b8和NPA a1b0支持
  “释放最后请求后重新聚合成0”的解释，但尚不是已接受的物理供电状态证明。
- 实际投票在host和最新v30 loader双侧关闭，原v29失败证据保留；不通过修改
  检查条件把原状态恢复失败改成通过，DSP auth-reset仍未启用。
- 新增独立NPA证据检查器，验证唯一最终状态、不同非空句柄、逆序释放、两资源
  恢复、明确请求次数/level及watchdog/5s预算；不能从PASS文本单独推断音频可用。
  9项负例/阶段测试通过，全部既有Python检查合计54项通过，EFI两目标及10项C检查通过。

checkpoint-ui-peripherals-v4-20261029已创建并逐项校验，含v39b与NPA v27c前后
的4273项文件记录。v40/v41及NPA客户端/投票工作在
其后，需新v5保存；v4仍明确IN PROGRESS，不能当已完成外围移植。

## v41c/v42：底部控制中心关闭兼容候选连续两轮通过

- 同v41b镜像只改起点为edge=2398px，识别器在13ms、translationY=-5.5pt时从
  Possible进入Failed；body起点的对照则完整Ended。确认输入分类与系统手势分支
  是可复现实验范围，未通过修改动画时长掩盖失败。v41c thermal通过，电池35.9→36.2°C。
- v42的SpringBoard在主队列每250ms发布Control Center原生窗口hidden/alpha及
  单调时钟时间；backboardd只在DOWN时接受2秒内的新鲜可见信号，且只对原来
  注记为edge-tip的底部触点改为本地pan分类。位置、时间、MOVE/UP语义及原生
  CCUIDismissal策略不改，其他应用/顶部/侧边仍保留原分类。源码选项默认关闭。
- host C覆盖隐藏、过期、未来、未初始化、越界以及全65536个Y位置的拒绝边界。
  builder同时重建producer和原生HID consumer，support构造器检查两侧marker，
  避免只升级一侧。程序/源码闭包在ui-control-local-v42-plan。
- v42实机visible0→1，修正1个finger，原生关闭recognizer完整Began→Changed→Ended，
  translationY=-576.5pt、velocityY=-919.319pt/s，之后visible回0；真实截图返回桌面。
  scope仍为合成输入经过实际HID/UI和GPU，不是机械手或光学触摸证明。
  thermal通过、峰53.8°C、电池35.3→36.0°C、7/7恢复、黑屏/Android熄屏。
- v42b不改镜像，测试控制中心edge关闭→实际匹配设置图标(414,1700)→中文设置
  完整显示→普通底部手势回桌面。两次返回图标模板均通过，control-local分类仅1次，
  未干扰应用返回。thermal通过、峰54.2°C、电池35.9→36.7°C，7/7恢复及熄屏通过。
  未据此宣称所有动画稳定60FPS或所有外围可用。
- marble DT明确battery_charger通过PMIC_RTR_ADSP_APPS、远端lpass、
  msm/adsp/charger_pd提供服务；真实SOC需ADSP/PMIC GLink链路。不能误用另一机型
  的本地FG SRAM寄存器方案，也不能把Mu FG协议中的15%/25°C占位数据发布给iOS。
- v30b保留实际vote禁用和NPA dirty命令防护，修正恢复诊断为逐字段独立读取、
  显式read_mask，不再因短路漏读而打印貌似真实的默认值。零请求生命周期第二轮
  通过：两条read_mask=f、state256/max480/aggregate0全部实际读到，57us、active0/
  dirty0、watchdog清除，独立证据检查器通过；电池35.9→36.0°C，Android熄屏。

## v31/v32：原生电源释放与强制冷复位的独立验收

- v31只读核对实际VCS对象：两资源definition/node data指向同一rail，resource回指、
  driver RVA2190和代码指纹匹配；rail缓存corner及NPA state均为256。config为空时，
  精确固件默认min0/max480/init256，未据此假设物理电压。电池34.6→35.0°C，Android熄屏。
- v32使用独立native-off-reset-v1能力：按resident DT各请求256，保持100ms，逆序
  Destroy并独立读回两资源。空客户端、aggregate0、state0/max480、read_mask=f和
  rail corner0全部实际成立；100229us。原状态恢复仍明确PASS0/restored0/dirty1。
- 新native-release记录PASS1只表示原生释放到零、需要后续冷复位；未篡改缓存恢复
  256，未弱化原检查器。loader保留watchdog并阻断后续修改/XNU；host重审日志后明确
  请求冷复位，验证Android boot ID改变后才接受reset闭环。电池33.9→34.6°C，屏幕OFF。
- exercise.json保留npa_client_cycle_accepted=false、original_restoration=false，
  npa_native_release_with_reset_passed=true、npa_cold_reset_verified=true。
  无DSP启动，无音频验收，物理电压未测量；旧npa-votes和v10b仍禁用。
- Python检查共59项通过；EFI两目标构建及10项host C检查通过。此阶段不证明音频、
  无线、蓝牙、传感器、SOC或休眠完成。下一阶段需在正确电源租期内启动DSP，并在
  PAS shutdown和显示内存所有权确认之前禁止恢复其载荷内存。

## v33e：供电租期覆盖完整ROM加载与停机后恢复

- 新增独立`pil-powered-stage`，在两个native256请求仍存续时导入并读回全套ROM。
  49MiB原内容备份保留；先取得PAS shutdown成功，再重新审核Demura/SSPP所有权，
  最后恢复并逐字比较原内存、清理metadata及RAM文件协议，之后才逆序释放供电。
- 内层PIL不再清除外层供电租期的watchdog。停机或所有权不确定时保留备份、供电
  和watchdog，拒绝继续清理目标内存。该逻辑先在不执行DSP的路径上验收。
- `out/daily-pil-powered-v33e-live`实机通过：24文件/24读回匹配、25零区、PAS
  init/memory/shutdown均EFI0/transport1/secure0；原内存恢复、57 opens=57 closes；
  NPA总租期4114096us。供电原始缓存未原地恢复仍保持原判失败，独立释放→冷复位
  验收通过，Android boot ID改变、屏幕OFF，电池32.1→32.8°C。DSP_start=0。
- 新代码使原FAT容量不足的v33/v33b/v33c构建均被离线拒绝，未操作手机。
  v33e采用显式Oz/LTO后保持原布局，其余103个固件文件相同。额外修正ADC转换缺失
  通道不得成功返回；0初始化只作BSS存储，必须由本轮真实转换确认温度及电压有效。

## v34b：首次独立短时认证/停机失败，路径再次禁用

- 外围代码扩展后增加显式`--firmware-only`镜像，编译时拒绝整个XNU入口；桌面仍
  使用已验证的旧镜像。v34普通镜像因FAT不足离线拒绝；v34b是该独立外围版。
- v34b使用合格供电租期及保留的完整载荷备份，在认证前登记GLink观察者，目标
  保持500ms后PAS shutdown。USB进度确认auth-reset调用返回EFI0，但secure结果和
  观察记录尚未取回；最后记录为PAS-metadata-shutdown-before，随后USB断开。
  不能宣布认证接受、DSP服务就绪或停机成功，也不能把Android返回当资源恢复通过。
- Android随后独立出现，屏幕OFF，电池32.5→33.1°C；restoration_verified=false。
  没有启动不确定内存的恢复，没有原样重试；旧完整native v10b继续禁用。
  `--pil-bounded-boot`/对应OEM命令在host和loader均禁用，v34b镜像清单标记FAILED。
- 源码表明正常停止需要SMP2P stop-bit/stop-ack。此缺口是待验证方向，尚不能当作
  本次USB断开的已证根因。后续先做不启动DSP、不发送IPCC的AP拥有结构准备。
- 已固定Xiaomi同一commit的smp2p.c、qcom-ipcc.c、qcom_scm.c及实际
  `qti_battery_charger.c`，SHA在`out/dsp-smp2p-v1-plan/references.json`。
  DT smem数组[443,429]按Linux INBOUND=0/OUTBOUND=1解释：AP写429、DSP写443；
  stop是master-kernel bit0，ready是slave-kernel bit1、stop-ack是bit3。
  IPCC由DT指定ed18000、client3/signal2；尚未写该寄存器。
- 原生SMEM的35a0/35a4锁钩子均为ret，不能在DSP并发运行时直接复用其分配器。
  新AP准备实验仅在新鲜、未执行任何DSP/PIL/NPA的固件会话内使用精确私有分区ABI，
  分配后必须冷复位。不能重开整个SMEM扫描或误用Glink表末尾RPM teardown当初始化。

## v35b/v36/v37：AP自有通知结构和完整供电组合实机通过

- v35b通过原生私有分区分配器仅创建AP写出的429条目。resident backend表精确为
  RVA6448及四个已审计函数，分配前429/443均不存在。实际区域80900000/2097152，
  APPS/ADSP分区80acf000/196608，free_before16480；原生分配344字节返回80ad3070，
  free只增加360（16字节分配头+344字节对齐容量）。没有写任何共享分区元数据偏移。
- 只对原生返回且落在该私有分区内、恒等NC映射的340字节写SMP2P头，最后发布
  version=1；AP0→ADSP2、16槽、唯一master-kernel/value0。逐字读回相同，两个已有
  GLink条目478/479的地址/大小保持不变。20us、无IPCC/DSP；冷复位boot ID确认，
  Android熄屏，电池32.3→33.1°C。
- v36从原Mu精确提取IPCCDxe，SHA e91f717f5a3a3e73701324fc4d8476416712af21a8a569a05fce0bb4befb30c1。
  resident protocol GUID52858ad7-8b16-4137-9c64-dfe04d942e80的五槽全部匹配，
  原生已初始化配置确认APPS client8、MMIO ed18000；与DT及Linuxsend偏移c一致。
  在340字节完整发布后仅向ed1800c写30002（client3/signal2），不读写IRQ配置，
  50ms后自有头不变、温度采样40.4°C，50057us。delivery_verified=false、DSP_start=0。
  随后冷复位及熄屏通过，电池32.6→33.1°C。
- v37把同一准备/通知与NPA租期、完整ROM校验、PAS shutdown和内存恢复连接。
  实机总命令4.173s，NPA租期4115820us；所有读回、停止后恢复及文件清理通过。
  SMEM和NPA均保持reset-required，所有外层watchdog权属保留，最后明确冷复位并
  验证新boot ID，电池33.2→33.2°C、Android熄屏；仍未执行DSP。
- 对失效/过期/缺失/乱序/方向错误的证据继续拒绝；控制器在恢复后也会以非零
  退出码返回实验失败，不再只在JSON里写error却退出0。当前Python共84项通过，
  EFI两目标与11项C检查通过（含SMP2P头边界和ready/stop-ack状态转换）。
- 下一候选v38只在独立外围镜像中开放新的SMP2P握手启动入口；旧v34b直接停机
  路径保持禁用。目标是ready后发送stop、验证新ACK、清stop位，之后才PASshutdown；
  失败则保留载荷备份/metadata/供电/本地观察者/watchdog，由整机冷复位恢复。

## v38 实机结果及新文档复用

- v38 的 PAS auth 返回 EFI0/transport1/secure0，ready=0x6；发送本轮 stop 后，
  实际取得新 stop-ack=0x10e。最后 USB 进度为 PAS-metadata-shutdown-before；
  没有成功返回，没有本地内存或供电恢复证明。Android 独立恢复，电池32.7→33.4°C，
  熄屏确认。不能把独立重启当作 restoration 成功，v38 镜像标 FAILED。
- 用户随后提供新版网站文档。已固定网站版本和内核 hakios 分支（main 仍为旧版），
  以及拆分出的 sm7325、uefi、metal 仓库；详细核对见 MARBLE-UPSTREAM-DOCS-20261029.md。
  新文档不能授权分区写入或 Android userdata 访问，原安全范围保持。
- 新版上游同样先 SMP2P 再 PAS，说明不能用“少了 stop 握手”解释全部问题。
  本机 Linux 还要求 QMP/XO/带宽资源；它们是待核对缺口，未证明断线根因。
  主机已禁用 --pil-smp2p-boot，loader 禁用旧执行路径及未完成的 retained-reset 草稿。
  v39 firmware-only 离线构建两目标与11个 C 检查通过，尚未启动实机。
- 按上游 G2/G5/G7/G10 接入表面读范围、屏外图层裁剪、分配失败清理与释放后读取修复。
  生产函数450组裁剪像素测试及边界/失败路径通过 ASan/UBSan；新内核编译/链接完成，
  原有四项 export/symbolset 检查仍失败，不能称全部 make 通过。
- 新内核临时启动后，通知中心/控制中心开关及桌面返回通过；负一屏、第二页、资源库
  六手势完整完成，489个缩小合成帧均目视核对，保留窗口内未见异常黑绿矩形。
  两轮7/7资源恢复、blank-window检查、闲置黑屏和Android熄屏通过；电池分别
  33.0→34.1°C、34.9→35.5°C。无clock/voltage升高；未宣称光学稳定60 FPS、
  原生Poster、音频/网络/蓝牙/传感器/SOC/硬件休眠完成。

## v40–v44：原生 AOP QMP 通道生命周期

v40c完成QMP/82项NPA资源图只读盘点；v44补上原生事件进度、已UP远端的core通知
协调和关闭后的状态收敛。两轮同镜像在5245/5221us内通过Open→connected→Close→
local-disconnected及描述符恢复；本地client/回调全释放，仅余原生remote-only节点。
冷复位boot ID变化、Android熄屏通过，电池分别35.9→36.5°C、36.7→37.0°C。
前面拒绝/未收敛的v41–v43均保留失败；没有改成成功。

没有业务TX、NPA/PAS或DSP执行，仍未完成音频/真实SOC。已进一步确认Linux XO
实际来自RPMH xo.lvl，crypto_ddr为MASTER_CRYPTO→SLAVE_EBI1，不能直接等同于
固件XO占位符或arbiter state1。完整证据、复测指令和边界见
`docs/MARBLE-ADSP-QMP-20261029.md`。

v45进一步复用GPU阶段命令数据库解析器，完成两次稳定RAM快照，实读`xo.lvl`
ARC地址300b0/slave3/version4096/aux32；262144字节读取及快照释放通过，5811us。
无时钟/RPMh/NPA/QMP/PAS请求，不是XO租期验收。电池34.5→35.1°C，Android熄屏。
EFI两目标、13项C检查、103项Python检查通过；源码/证据另存外围增量checkpoint v2，
已验证的v8和外围v1均保持不可变。

后续v46–v49原生RPMh零命令客户端与ICB路由盘点详见
`MARBLE-ADSP-NATIVE-LEASE-20261029.md`。两轮客户端恢复、完整1018条路由
已验证；XO/带宽业务租期、DSP/音频及真实SOC仍未完成。
