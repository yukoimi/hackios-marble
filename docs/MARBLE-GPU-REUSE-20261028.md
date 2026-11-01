# marble GPU／显示复用调查（2026-10-28）

结论：有可复用代码，优先继续使用 HackiOS 的 GLES 路径，并借鉴其他移植的 GPU presenter。
本轮未找到可以直接装到 A725、同时解决渲染缺陷和稳定 60 帧的成品。
本轮只读取电脑文件和公开源码，未操作手机、未启动测试、未修改运行默认配置。

## 1. 先读原仓库 docs：里面已经解决过什么

核对版本为 `oosexgang/hackios-xnu-kernel` 的
`88513176b1e96ecf0b764cd62c6f4e6ac71345e3`。其 docs 共六个文件；GPU 的重要内容在
`docs/ru/notes.md`、`docs/ru/metal.md`、`docs/air.md` 和 `docs/air-report.md`。
README 中某些英文路径并不存在，不能因 `docs/metal.md` 不存在就漏读俄文正文。

| 原文位置 | 可复用内容 | 对 marble 的实际意义 |
|---|---|---|
| [notes.md 407–448 行](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/ru/notes.md#L407) | Adreno 642L → Mesa Freedreno → EAGL → CoreAnimation；首个 framebuffer-fetch 绘制前执行 CCU flush／UCHE invalidate | 是本地现有架构的来源；A642L 修复能否完整覆盖 A725 要另验 |
| [notes.md 492–509 行](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/ru/notes.md#L492) | 独立降采样纹理链修复可变模糊黑带；highp 修复颜色渐变精度 | 对应负一屏、资源库等；本地已有 `vblur` 与 highp 代码 |
| [notes.md 510–523 行](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/ru/notes.md#L510) | 私有渲染纹理，结束后 GPU blit 到 IOSurface；flush fence 推送延迟提交 | 原文描述的黑矩形／缺块很接近用户症状。本地已有 `blit_targets`、`sync_frames`，不能重复计为新修复 |
| [notes.md 524–541 行](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/ru/notes.md#L524) | 为透明 group 保存 vibrant 层覆盖范围和颜色矩阵 | 对应控制中心模块出现不完整；本地已有 `group_draw_begin()` |
| [notes.md 349、368–369 行](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/ru/notes.md#L349) | 分阶段时间戳、`fb_threads`、双帧等待队列；现成 `frames.py`／`latency.py` 与手势场景 | 先测等待 GPU、CPU 缩放、队列阻塞、实际提交，避免只改节拍参数 |

这些是作者在其原设备上的观察，不是 marble 的验收结果。文档给出的设置滚动
56–58 帧／秒，也同时写明显示复制未与扫描同步、面板运行 120Hz（438–443 行）。
它不能证明本机稳定 60 帧，不能作为硬件 vblank／pageflip 已完成的依据。

本地核对：

- 原始 userland 提交 `906425b75e02b4f99eb5d7bb1ef7c9583472d772` 的 `eagl.c`，
  与上述当前主线文件在归一化项目名称、库名、头文件名和宏名后，全文一致。
  结果保存于 `out/gpu-reuse-research-20261028/eagl-comparison.json`。
- 当前 `userland/src/eagl.c` 已保留上述主要机制；marble 默认配置另行关闭 native fetch，
  保留 shared input、最终 blit 和模糊。fetch-copy 是另一个候选，不能混作默认配置。
- `tools/mesa/freedreno-darwin.patch` 已有 `prev_draw_read_fb` 屏障补丁，
  条件是首次从普通绘制切换到 framebuffer read。接下来可审计 A7XX 上的作用范围；
  不能因为补丁存在就断言 A725 的缓存问题全部消失。
- `tools/frames.py`、`tools/latency.py`、`userland/rc.jobs.{fps,gfx,vblur,cc}` 本地已有。
  新版本副本已存档，后续按本机的有界会话和手势桥使用。

## 2. 原项目的 Metal 文档应怎样理解

[docs/ru/metal.md](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/ru/metal.md)
解释了发现 GPU、Metal 对象、编译器和内核 ABI；第 4 节说明 Apple AGX 驱动生成的是
Apple GPU 命令，不能拿来驱动 Adreno。修改设备树的 graphics-featureset 也不是启用 Metal 的开关。

[docs/air.md](https://github.com/oosexgang/hackios-xnu-kernel/blob/88513176b1e96ecf0b764cd62c6f4e6ac71345e3/docs/air.md)
已补上 AIR→NIR→IR3 编译工作，但开头明确区分：编译器不等于 Metal 对象运行时与提交 ABI。
因此，旧 Metal 调研中关于“尚无 AIR frontend”的文字要结合后续 AIR 文档阅读；
也不能反向把“能编译 shader”解释成“完整 Metal 已能使用”。

当前主线 README 把 Metal 列为未完成。这与下面 UniXNU 的独立 Metal 实现是两条来源。

## 3. 最接近现有 GLES 的参考：Mi 10T／Adreno 650

来源：[enesuzun2002/hakios-apollo](https://github.com/enesuzun2002/hakios-apollo/tree/a8fdd858d887fdad335977bbddc8e22f187903c4)，
版本 `a8fdd858d887fdad335977bbddc8e22f187903c4`。

| 源码 | 机制 | 可复用边界 |
|---|---|---|
| [userland/src/eagl.c](https://github.com/enesuzun2002/hakios-apollo/blob/a8fdd858d887fdad335977bbddc8e22f187903c4/userland/src/eagl.c#L1843) | 独立 GL context；IOSurface 导入缓存；`glBlitFramebuffer(GL_LINEAR)` 完成最终面板缩放 | 最直接的用户态参考，避免再手写 GPU 缩放器；需要本机内核导入／队列接口 |
| [hakios.apollo_gpu.cpp](https://github.com/enesuzun2002/hakios-apollo/blob/a8fdd858d887fdad335977bbddc8e22f187903c4/devices/mi-10t/xnu/files/iokit/Kernel/hakios.apollo_gpu.cpp) | 导出三块呈现缓冲；acquire／queue／release；跟踪 GPU fence | 可借其所有权设计；不能直接用服务名、selector 和物理地址 |
| [hakios.apollo_display.c](https://github.com/enesuzun2002/hakios-apollo/blob/a8fdd858d887fdad335977bbddc8e22f187903c4/devices/mi-10t/xnu/files/bsd/dev/hakios.apollo_display.c) | front／pending／reserved 状态；写 DPU source address，等待 CTL flush latch 后释放旧帧 | 状态机可参考；SM8250 DPU 表、SMMU 判断、144Hz 原生时序不可照搬 |
| [hakios.apollo_frames.c](https://github.com/enesuzun2002/hakios-apollo/blob/a8fdd858d887fdad335977bbddc8e22f187903c4/devices/mi-10t/xnu/files/bsd/dev/hakios.apollo_frames.c) | 队列阻塞、GPU 等待、扫描等待、复制、呈现间隔分别统计 | 优先借鉴；其中 vsync 字段计软件 display-link tick，并非显示 IRQ |

它尝试把 iOS 1125×2436 输出缩放到 1080×2400。只加速单个全屏 BGRA 层，其他路径回退。
1080×4=4320 字节一行不满足其 GPU 64 字节 pitch 要求，因此使用 4352 字节 pitch；
移植必须重新检查分配大小、每行跨度与显示读取配置，不能只替换宽高。

重要证据限制：作者在
[deedfba46774 提交](https://github.com/enesuzun2002/hakios-apollo/commit/deedfba46774)
写明 presenter 失败清理修改尚未编译、未实机运行；设备 README 也保留 GOP 地址待日志确认。
所以它是值得复用的源代码方案，尚不能作为经过证明的流畅成品。

另有匹配问题：`inject.c` 承认 dyld interpose 不覆盖 cache 内部调用，因此 EAGL 还用
surface ID 标记下一次 swap。接入前必须验证同一 surface 重用、多个待呈现帧、错误回退时
不会把缓冲配给错误 swap；不能原样继承一个全局 mark 就假定正确。

marble 已有约 60Hz 的 DSI FRAME／LINE 计数资格和 blank-window copy，尚未改显示 source address。
应先复用 GPU 缩放到离屏缓冲，再接现有显示提交；减少 CPU 缩放和独立验证 pageflip 可分阶段进行。
真正 pageflip 前要确认本机 source pipe、CTL、显示 SMMU、地址宽度、预留内存和恢复路径。

## 4. 更完整但改动更大的方案：A740 Metal＋Turnip

来源：

- [UniXNU/Adreno740](https://github.com/UniXNU/Adreno740/tree/5c8c1453c67d6ef6b10ec43ab1f8fc570a2f33e1)
  （原 NativeAdreno 已重定向到这个名称）。
- [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal/tree/ce5e19e33e05e1873d38d818e23cabfe0ac708c3)。
- [官方 GPU 移植说明](https://github.com/UniXNU/UniXNU/blob/120067ccc43ebb5e76ec62c9219a48c897052838/docs/porting/gpu.md)。

项目文档报告 sheng／A740 的 Metal 设备发现、合成和向 firmware scanout 的 GPU 拷贝可用。
这是外部项目报告的硬件结果，本轮没有独立复现实机。
运行时链为 Metal／AIR→metal2vulkan／SPIR-V→Turnip／NIR→IR3，而非只接一个 AIR 编译器。

可利用的具体修复：

- `bridge/patches/0002-resource-wait-storage-dependencies.patch`：资源等待／存储依赖。
- `bridge/patches/0009-keep-final-pass.patch`：保留最终 render pass。
- [0016-tile-stores-within-rows.patch](https://github.com/UniXNU/AdrenoMetal/blob/ce5e19e33e05e1873d38d818e23cabfe0ac708c3/bridge/patches/0016-tile-stores-within-rows.patch)：
  校验 tile store 的 16×4 边界不越过 row pitch 和 buffer 尾部，不满足时放弃可写别名。
  这是 Metal bridge 的布局修复；与本机黑／绿块存在可调查的相似点，尚无相同根因证据。
- 插件设置 `TU_DEBUG=nocb` 避免其观察到的 BR／BV concurrent-binning 死锁。
  这是 Turnip 参数，不能直接拿来替代 GLES Freedreno 参数。
- `GpuScanoutPresenter.h` 展示共享内存 GPU copy，仍包含等待完成；它本身不提供本机 vblank。

构建脚本固定 Mesa `c823a83e8f373a5c36c2b9ed1d7dc98a4f3360ee`；已从 Mesa 官方 GitLab
取得该版本 `freedreno_devices.py`，包含 `0x07030002`／FD725。
这证明设备数据库识别本机芯片，不证明整个 Metal／Vulkan／显示链已在 A725 上验证。

主要接入成本：

1. Darwin KMD 使用 NativeAdreno ABI，本地使用 RedwoodGPU／msm 子集，需要兼容层或替换后端。
2. 新 ABI 为 v2、128 个 submit buffer；不能与旧 v1 的结构体混用。
3. 上游用户态脚本面向 iOS 17，并使用 xcrun、Xcode SDK、codesign；本地是 iOS 16.5
   与 Windows＋Linux 构建环境。需要审计 OS 方法／布局、重新定向构建，不能直接装 bundle。
4. 仍要使用 marble 的 A725 固件、供电和时钟数据。用户提供 Android 包里的驱动不能替代上述 ABI。

适合单独候选分支，优先级低于 GLES presenter 的有界改造；无需从头写 Metal runtime。

## 5. NativeAdreno 最新差异：哪些已有，哪些值得补

本地 ring 来源为旧 `b686b9054f57d393c80195b82568a09f8ca106a2`，并非本轮新发现。
与 A740 当前 `5c8c1453…` 的四份 diff 已保存到研究目录。

| 差异 | 本地状态／建议 |
|---|---|
| 按 client 切换重置 BR／BV context | 本地 `MarbleGPURing.inc` 已按 client＋queue 处理，不要重复改成每提交都 reset |
| 单次 128 IB | 本地桥已允许 128 个，不能以此宣称新性能收益 |
| GMU AHB-fence 检查＋WPTR 有界重写 | 本地旧 ring 尚无此回调；先确认 A725 寄存器并记录 dropped-write，证据成立再补 |
| stalled-fence 快照、分阶段恢复 | 可借诊断；bus drain／MMIO 写入不能直接照搬 |
| BO handle 索引、低开销映射、局部 TLB invalidation | 可审计性能；上游“不每次 submit invalidate”依赖页表不变量，不能直接删本地同步 |

## 6. 排除看似现成的方案

- `kvfsrl/xnu-kernel-peridot-marble` 对照所取主线树，差异只有 README；没有现成 marble GPU 后端。
- `sbccc1888/mesa` 是本地已用的原 HackiOS 基础，不是新找到的 A725 修复包。
- `sbccc1888/ios-party` 的 chiron 记录不构成完整 GPU 桌面验证。
- `iscle/hakios-mt6762` 的设备文档区分软件呈现和 GPU 加速；没有找到新的可复用 GPU 加速后端。
- UniXNU 的 fuxi／myron 参考尚无文档化实机结果，不能等同于已验证 A740／A8XX 成品。
- GopFramebuffer 的 `NativeMobileDisplay.cpp` 明确是未发布服务的 prototype；
  实际可参考的是 `NativeLegacyDisplay.cpp`、`NativeGOPPresenter.cpp` 的 swap 生命周期，
  仍需匹配本地 OS ABI。

## 7. 建议执行顺序

1. 按原 docs 的 `fps/gfx/vblur/cc` 场景建立同配置对比，复用阶段计时；
   核对 `blit=1`、fence flush、group、vblur 实际启用，审计 A725 fetch 屏障。
2. 复用 apollo 的独立 GL presenter／GPU scale 思路，先输出到本机合法离屏缓冲，
   继续使用已验证的 counter-paced copy；只比较去掉 CPU 缩放的收益。
3. 验证真实 swap 序号与 buffer 的绑定、GPU fence、失败释放后，再做本机三缓冲与 DPU latch。
4. 根据日志决定是否移植 A740 WPTR 检查、BO 查找／VM 优化；避免无证据地扩大改动。
5. GLES 路径仍不够时，使用现成 AdrenoMetal＋Turnip 做 iOS 16.5／A725 ABI 候选，
   不从零实现 Metal。

60 帧验收应分别报告：有效动画窗口、完成合成的间隔分布、呈现间隔、超时／回退和显示 latch。
RAM 截图／软件 tick 不能独立证明光学无撕裂；静态 Dock 模糊只能证明该效果路径工作。

## 8. 来源与可追溯性

下载文件、固定 URL、SHA256、失败下载都记录在
`out/gpu-reuse-research-20261028/downloads.json`。其中 docs 六文件已全部存档。
`eagl-comparison.json` 保存归一化规则与相等结果；`apollo-eagl.diff` 保存 presenter 差异。
调查覆盖所取主线、公开 PR／issue、首批 100 个 fork 及列出的 UniXNU 组件，
并非全网穷尽；“未找到”不代表不存在。

报告只依据源码和文档判断可移植性，未把文档内的命令当作操作授权。
