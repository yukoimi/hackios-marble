# marble 开发交接（2026-11-01）

以根 README 的状态表为当前结论。旧日期文档记录当时的候选和失败，不表示
现在每一个功能均可用。所有实机过程采用 `fastboot boot` 和 RAM 支持盘，
没有访问 Android `/data`，没有刷写/擦除分区。

## 两种源码状态要分清

当前 `xnu/files/bsd/dev/marble_audio_native_kernel.h` 及相关 stream/endpoint
包含后加的有限原始回复诊断。这些代码完成模型与构建检查，但采用这些代码的
v253/v254 实机失败，不能说它们就是 v259 双轮成功所用的内核。

`reference/v259/kernel-source/` 保存 v259 所沿用的 stream24 内核源码快照；
`reference/v259/mach.json` 保存其历史哈希。v259 的 loader 使用当前已修复的
audio-DMA-only 审计。公开源码的主机配置、行尾及日志脱敏不等于历史镜像的
逐字节重建，任何新构建均需自己的编译清单、内核清单和新实机验收。

## 音频最新可重复结果

- v259/v259b 同镜像两次冷启动，24 页、5888 输入/6144 输出帧，19 次补入，
  队列绕回和一次欠载补零恢复；原独立 oracle 逐页匹配。
- 功放全程 HMUTE / 90dB；没有实际可听输出。
- READY/AUTH、DATA/TX 退役、quiet ACK、STOP/UNMAP、PAS、XNU unmap ACK、
  DMA free、QMP OFF/两次 close ACK、显示内存审计及恢复完成。
- v259/v259b：DSP 实际 530082/520123 us；完整 metadata-stop
  716754/717030 us；QMP wire 总计 1385945/1375849 us。
- 返回 Android 并确认熄屏；端点电池分别 33.7→34.4、34.4→35.8°C。
  端点读数不代表运行期间的电池峰值。

报告器与实际脱敏日志在 `tools/tests/fixtures/marble-audio-display-v259*.log`。
合成解析器 fixture 只验证解析器，不得当成独立实机成功。

## v259 修复了什么

v258 已走完数字音频停止链路，但恢复前的显示 DMA 审计错误地先要求
连续 VSync 测量没有 missed edge，因而没有读取隔离字段就失败。
v259 抽出同一显示总线时钟获取函数，仅在显式 muted owner-board stream24
模式下跳过无关的连续帧率测量。

仍保留全部 66 字段、两份间隔 20ms 样本、Demura/8 pipe/额外平面、ADSP 重叠、
精确 GOP 和时钟归还检查；普通显示/VSync 路径保持原采样。构建、controller、
profile、报告器通过 `audio-display-dma-only` 显式绑定。

诊断运行期仍为 300ms≤实际墙钟<600ms，原始 PAS 与完整 metadata-stop
分别<1s，QMP close<750ms，总 wire<2.5s。未放宽这些边界。当前版本不启用
自有 10ms/TPL16 runtime sampler；实际时间与温度检查仍在。

## 下一步：持续播放，再接系统音频，再出声

1. 增加独立的持续播放模式，正确管理 producer/EOS/排空/暂停/恢复/停止，
   处理队列背压、欠载静音、DMA 复用与缓存同步；先保持功放静音。
   不能把旧诊断的 600ms 门槛直接加长后称为生产播放。
2. 接入已有 `userland/src/audio_plugin.c` → `/dev/redwood-audio` →
   `redwood_audio_set_sink`。当前 marble 诊断只是显式 device-write 输入，
   没有注册成可用的系统音频 sink。
3. 有限幅开启真实输出，保留 ROM Music/OTP、音量/高通/原 speaker guard、
   健康监控和可靠停机；验证反复打开/关闭、双声道、音量和温度。
4. 显示继续补硬件 IRQ、原子 pageflip/缓冲区所有权及稳定帧节奏，重新验收
   设置退出、负一屏、资源库、通知中心和控制中心的瞬态色块。
5. 按键真实边沿、亮度滑杆、真实电池 SOC、硬件休眠/唤醒、网络、蓝牙、
   传感器、摄像头逐项完成；提取到固件或套用 redwood 地址均不算完成。

## 本机材料与可复现性缺口

- `out/` 是生成物，不在 Git 中。旧文档出现的 `*-plan`、`*-live`、checkpoint
  是原开发机的历史路径，公开仓库不包含数十 GiB 的这些副本。
- 公开的 `reference/v259/` 是所需命令/身份/源码参考，不含 EFI、内核二进制、
  系统盘、ADSP 包或闭源固件。哈希是输入身份记录，不是下载地址或签名。
- Mu-marble.img 来源于现成设备 UEFI，未取得匹配源码；不能宣称 UEFI
  已实现完全可从本仓库重建。
- 原 EFI 格式打包曾依赖旧 v211 的未打包 loader 模板。该模板二进制不随库
  发布，需要从固定来源/兼容源码重建，尚需去除这一 bootstrap 依赖。
- Windows host 构建器仍有 `../toolchains/`、7zip、LZMA SDK 等布局要求。
  `tools/marble_local.py` 只解耦设备序列号及 adb/fastboot 路径，不承诺所有
  Windows 控制器已经跨平台。`MARBLE_ANDROID_SERIAL` 必须明确配置。
- 完整 XNU make 仍有既存 symbolset/export 检查 exit 2；CC/LD/STRIP 成功
  不等于完整 make 成功。继续按 `MARBLE-BUILD.md` 检查日志与新产物。

## 输入来源与测试

输入身份见 `reference/v259/inputs.json`，获取/提取工具见
`ios165/`、`tools/marble-rom-inspect.py`、`tools/marble-peripheral-material.py`、
`tools/marble-adsp-package.py`。本仓库集成固定 userland 源码；
`common/setup.sh` 检测集成标记后不会另行拉取 userland 覆盖本地改动。

`python tools/marble-host-tests.py` 运行公开 Python 回归及可独立运行的 C 模型。
依赖本地 ROM/内核/镜像的用例逐项声明并以 skip 显示；不会为了 CI 上传闭源
输入或将它们误报为通过。恢复大历史归档需要原开发材料；公开仓库不包含这些归档。

## 开发机退役交接

原开发工作区在源码发布核对后清理，旧的本地历史归档与编译虚拟机不再保留。
公开仓库保留必要源码、输入清单与 v259 参考；上述私有输入和 bootstrap 缺口仍需后续开发者自行补齐。
原始 Mu 镜像的只读拆解工具为 `tools/marble-inspect-original-image.py`，运行
`python tools/marble-inspect-original-image.py Mu-marble.img OUTPUT_DIRECTORY`；
该工具仅向输出目录写入拆解结果，不改动原镜像，也不连接手机。
