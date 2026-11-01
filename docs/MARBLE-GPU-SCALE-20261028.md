# marble：复用 GLES presenter 的最终缩放候选

2026-10-28。本轮已实施、构建并实机测试，仍未完成稳定 60 帧或全部渲染缺陷修复。
不刷分区，不访问 Android `/data`；系统盘、支持盘与 iOS `/private/var` 均在 RAM。

## 实现及来源

复用调查见 `MARBLE-GPU-REUSE-20261028.md`。本轮借用 apollo 独立 GL context／
`glBlitFramebuffer(GL_LINEAR)` 最终缩放、UniXNU GopFramebuffer 带 sequence 的
offer／complete 生命周期；没有照搬 SM8250 MMIO、DPU 或 A740 电源参数。

`MarbleGPUScale.inc` 在实际 SwapEnd 的 retained IOSurface 与显示线程之间提供交接。
用户态 `marble-gpu-scale` 独立进程使用本项目 `redwood_gl_create(3)`、
`redwood_gl_surface_texture()` 和同一 FD725 Mesa；不在 backboardd 新建并发 DRM 连接。
独立输出为 1080×2400 BGRA、4352 bytes/row，64 字节对齐。
GPU 完成并确认同一 sequence 后，内核逐行复制到已有 staging；保留原来的
DSI 计数、空白期 memcpy、屏幕黑屏与截图锁。没有新增 DPU flip 或中断。

仅优化单层、完整源矩形、完整目标矩形、无 plane／offset 的 BGRA 帧；
其他情况和没有注册 helper 时完全走原 CPU 缩放。
每次任务只允许被领取一次；拒绝错误／重复／旧 sequence。
50ms 截止后丢弃该结果，未领取的 source 立即释放；已领取的 source 保留至
相同 sequence 的晚到确认或 worker close。晚到确认以前不能发下一份任务或重用输出。
累计三次超时或任何 GL／fence 失败禁用本轮优化。源与输出均使用 MiniIOSurfaceCopy/Drop。

## 已定位并修正的候选问题

v1 的首帧正确，同一缓冲更新后像素错误。启动前 FD725 自检失败，helper 没有注册。
原因对应现有 EAGL 发布协议：`glFinish` 不更新 IOSurface seed；CPU 读过的输出
缓存行需要在 GPU 下次重用前失效。本轮 v2 复用 EAGL 的 IOSurface Lock/Unlock
写入发布步骤，9 组 FD725 实机像素用例全部通过。

v2 一次冷启动迟到即永久禁用。v3 增加交互 QoS、队列／工作／总耗时统计，
实现准确晚到确认与安全恢复。复测 seq105 在约51ms截止，晚到工作约61ms，
结果明确丢弃；之后一直恢复到至少 seq480，未把旧结果交给新 swap。

## 离线验证

`tools/marble-build-scale.py --out out/NEW-plan`：

- 生产 blit 在 host GLES3/llvmpipe 下运行15组，37,008通道，误差阈值2，错误0；
  非对称 RGBA、上下方向、边缘、升降采样、行 padding、3次更新、继承 scissor。
- 直接 include 生产 `MarbleGPUScale.inc` 的锁／surface shim 测试：成功、重复／旧确认、
  已领取／未领取超时、晚到丢弃后恢复、关闭竞态、GL失败、三次截止，引用平衡。
  ASan/UBSan通过；这些不是 Darwin 调度或 GPU 硬件验证。
- ARM64 helper 链接 `/redwood/lib/libOSMesa.8.dylib`，不链接 Apple OpenGLES；
  构建脚本校验链接 Mesa 与支持盘运行 Mesa SHA256 相同。
- 内核编译／链接和 GIC／Mach-O metadata 验证通过；make仍因继承的
  IOKit/Libkern/Private 导出表检查失败退出2，不能声称整个构建无错误。
- Python `test_marble*.py` 共44项通过，包括2×参数与原保护条件。

实际测试内核：`out/gpu-scale-v3-plan/mach.marble-source`，
SHA256 `a1b05e52500f7efe4fa72b9bf82c766322042d809617ed78cdd285285a3bfe36`。
重编译的 `gpu-scale-v3-verified-plan` 含生产交接测试；helper hash相同，
内核因构建身份变化而hash不同，不能将其直接标记为同一实机二进制。

## 3× 同配置对照：未证明提速

两份支持盘使用相同 EAGL、shared＋fetch-copy、错误轮询每帧、45秒warmup、
两轮设备时钟手势、四核cap、硬件计数节拍；唯一主要差异是是否注册scale helper。
`out/gpu-scale-v3-control-animation-live` 是关闭helper的对照，
`out/gpu-scale-v3-animation-retest-live` 是开启helper复测。

窗口预先固定为每个 open/exit begin标记后的前2秒，至对应end截断。
它包含app启动／静止期，不能作为持续动画或光学刷新率。
由 `tools/marble-scale-report.py` 从原始 recording.bin 重算：

| 配置 | 第一次打开 | 第一次退出 | 第二次打开 | 第二次退出 |
|---|---:|---:|---:|---:|
| 3× CPU缩放对照 | 16.20 | 47.24 | 27.84 | 42.43 |
| 3× GPU最终缩放 | 12.73 | 47.24 | 26.29 | 44.69 |
| 2× GPU最终缩放实验 | 17.71 | 59.54 | 43.49 | 55.48 |

单位均为固定窗口内完成合成次数/秒。不是面板FPS，不是因果统计，样本仅各一轮。
3× GPU最后周期记录：480 offers、479完成、1次超时且晚到丢弃后恢复。
helper累计到480帧的API墙钟均值约4.67ms，不是GPU单独执行时间。
对照477次、候选485次空白期复制，等待失败／超界／非同步回退均0；
这些总数受运行长度与静态帧影响，不能拿总帧数直接比较性能。

## 2×内部渲染实验：性能提高，但画质验收失败

复用内核已有 `fbscale=2`，逻辑375×812points不变，内部750×1624，再由GPU
放大至原1080×2400panel。默认仍3×；2×清晰度更低。
`marble-run.py --render-scale 2` 仅允许完整GPU短测保护条件；loader仅接受
精确单个 `fbscale=2`，拒绝任意值／重复参数，并通过getvar确认对应能力。
第一次使用旧loader被白名单拒绝，XNU未启动，证据保留在
`out/gpu-scale-v3-2x-animation-live/refused.log`。
之后临时镜像 `out/gpu-scale-2x-loader-v3-plan/loader-boot.img` 仅增加该参数
与能力报告；不改温度阈值、时限、存储限制或任何MMIO操作。

`out/gpu-scale-v3-2x-animation-retest-live`：两轮开合完整，最后周期至少480次
缩放全部完成、无超时；helper至480帧均值约2.14ms。
539次空白期复制，超界／失败／非同步回退0，最大复制1689us。
电池前后30.4→32.2°C；这是Android端点读数，不能证明iOS电池温度峰值。

**逐帧发现回退：设置顶栏红色、部分过渡与桌面壁纸黑色。该配置不能作为完成修复，
不能因接近60的局部窗口标记通过或替换原默认。**
已审阅`frames-384.jpg`，保留完整569帧录像与所有编号sheets。

`out/gpu-scale-v3-2x-pages-live`：两轮负一屏／桌面／第二页／资源库往返完成，
最后周期至少960次GPU缩放全部完成，无超时。固定pages-begin/end全窗口
47.19次完成合成/秒，median16.62ms、p95 33.19ms、最大433.18ms；
中间有settle等待，不能证明连续动画47fps或稳定60。
1033次空白期复制无超界。壁纸黑色回退仍存在；overview仅作定位，不算视觉通过。
电池32.3→33.4°C，die采样峰50.3°C。七项资源恢复、RAM黑屏和Android熄屏均确认。

后续`out/gpu-scale-v3-2x-nofetch-animation-live`两轮完整，红栏／黑壁纸仍存在。
放大`ui/exit-1-end.png`可读红栏文字 `😳 rdar:45025538`，因此它是有明确文字的
诊断界面，不能继续简单描述为GPU随机红色块。当前推断是2×设备布局／框架
兼容问题；尚未定位产生该文字的UIKit具体方法，不能把推断当作已证明根因。
壁纸黑色是否同根因也未证明。nofetch没有解决它们。
同规则open0/exit0/open1/exit1为13.78/44.49/31.18/44.28次完成合成/秒，
周期记录至少359次完成、1次超时后恢复；441次空白期复制无超界，
电池34.0→34.5°C。保留nofetch失败证据，不用其较低速率替代对照结论。

## 下一步

### 828×1792／2×布局对照（v4）

`out/gpu-scale-v4-layout-plan`仅改变显式2×候选的逻辑布局为414×896points，
不改变默认3×。测试在`out/gpu-scale-v4-2x-layout-animation-live`。
内核SHA256 `62d046a097818dede3cd576b99fc8f86f919e5d26ed112e96cb4549aa2df8af9`。
ARM64构建、相同生产handoff/像素主机测试通过。
MobileGestalt实测width=828、height=1792、scale=2，
`main-screen-class`与`ArtworkDeviceSubType`仍为`(none)`。
这只证明两项查询没有返回答案，尚未证明UIKit具体怎样选择布局。
红色`rdar:45025538`诊断条和黑壁纸仍出现，**此布局也未通过视觉验收**。
已目视核对`ui/open-0-end.png`，该捕获仍停在设置展开过程，不能将标记end
自动解释成应用已经完全展开。

两轮合成手势完成；最后周期480次缩放全部完成、无超时；
498次空白期复制无超界、无非同步回退，最大1898us。
固定open0/exit0/open1/exit1窗口16.91/58.21/37.11/56.16次完成合成/秒，
均包含静态／重复帧，不能作稳定动画FPS。电池32.3→33.3°C、die采样峰50.3°C；
七项恢复、iOS黑屏及Android熄屏均确认。

`marble-scale-report.py`另外统计同一窗口中央区域的精确变化次数及平均通道变化>1次数，
排除状态时钟和底部指示条。比如旧375×812／2×第一退出窗口保留88帧，
仅67次中央像素精确变化、26次平均变化>1。因此59.54次合成/秒尤其不能单独
表述为59.54fps流畅动画。变化阈值本身只是诊断指标，不是光学验收。

### 后续方向

nofetch及另一尺寸对照已完成；下一步应定位UIKit状态栏诊断的具体触发条件、
屏幕分类与壁纸布局资源，不能仅隐藏诊断条或以黑壁纸换帧率。
保持3×默认，GPU最终缩放仍只opt-in。之后以窗口内真实变化帧进一步分开
app冷启动、着色／模糊、合成、等待、复制，决定是否值得继续优化presenter。
日常功能不在本轮验收范围内；此前按键／亮度能力未因本轮改动宣称新完成。

实机始终有90秒总限、die55°C退出、四核CPU cap与EFI idle、Android电池新鲜预检<38°C。
各轮结束后获取恢复日志、回Android并确认屏幕OFF；不把die温度当电池温度。
