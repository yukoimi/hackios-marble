# 准备材料与下载入口

本清单适用于本仓库的 **marble 开发快照**，不是 redwood 的即刷即用教程。
下载入口于 **2026-11-01** 核对；`latest` 链接将来会变化，实际构建应记录自己的版本和哈希。

## 必需材料

| 材料 | 用来做什么 | 获取入口 / 本仓库要求 |
|---|---|---|
| Redmi Note 12 Turbo / POCO F5（marble） | 实机测试 | 已解锁 bootloader；可回到 Android/fastboot，已授权 USB 调试。其他机型不在本次验收范围 |
| adb / fastboot | 读取状态和 RAM 临时启动 | [Google 官方说明](https://developer.android.com/tools/releases/platform-tools)；[Windows 下载](https://dl.google.com/android/repository/platform-tools-latest-windows.zip)、[Linux 下载](https://dl.google.com/android/repository/platform-tools-latest-linux.zip) |
| Python 3.11+、Git | 构建、打包、报告与拉取源码 | [Python](https://www.python.org/downloads/)、[Git](https://git-scm.com/downloads)；离线 Python 回归使用标准库 |
| 匹配的 Mu-marble.img | 手机 UEFI/启动基础 | 原作者未提供匹配源码/可核实公开下载。需要自行取得原适用镜像；本仓库不虚构下载链接、不分发此镜像 |
| iOS 16.5 / 20F66 iPhone X IPSW | 本地提取 iOS userspace | [Apple CDN 原始 IPSW](https://updates.cdn-apple.com/2023SpringFCS/fullrestores/032-85130/6AB0D10A-8AF9-4F6C-8CF5-A1B315CABE88/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw)，6165698198 字节；`ios165/fetch.sh` / `fetch-from-ipsw.py` 使用同一来源 |
| marble 适用 ROM/设备固件 | 提取 GPU、ADSP、功放与设备树参考 | 使用自己取得的适用固件。本次输入来自用户提供的 4.0.4.0[27B33] ROM，未核实可公开下载的原始来源；不能承诺任意官方 ROM 可替换同一组哈希 |

Mu 输入 SHA256（`marble-image.py` 保留此身份检查）：

```text
77733522406161dd0c621b204d34e2b42b377f71e6474dfe4a928e3eefc5c328
```

本次 vendor ROM SHA256（不是 iOS IPSW 的哈希）：

```text
1f79e3854f5b68f43c30273eb6f94a4023f7651148e25f3c488538b7b9d2f65a
```

固件具体文件名、大小和哈希见 [vendor-materials.json](../reference/v259/vendor-materials.json)。
需要自己提取，不把 `.ko` 当作能直接加载进 XNU 的驱动。

## 编译时需要

| 材料 | 固定版本 / 入口 | 放置与说明 |
|---|---|---|
| Linux 编译环境 | Debian 13，或兼容 Linux x86-64；[Debian 官方云镜像目录](https://cloud.debian.org/images/cloud/trixie/latest/) | Linux 主机可直接构建；Windows 可使用自行配置的隔离虚拟机。已有 VM 不必重新下载 |
| Linux iOS toolchain | [L1ghtmann test-210562a 下载](https://github.com/L1ghtmann/llvm-project/releases/download/test-210562a/iOSToolchain-x86_64.tar.xz)，86163280 字节 | `tools/marble-linux-build.sh setup` 下载并校验；安装到 `$THEOS/toolchain` |
| Theos iPhoneOS16.5 SDK | [固定源码提交](https://github.com/theos/sdks/tree/0222fd5413cf4b9af096f37b4621afa2688572f7/iPhoneOS16.5.sdk) | 由 setup 拉取；放在 `$THEOS/sdks/iPhoneOS16.5.sdk`，不随本库重分发 |
| XNU 8796.121.2 | [Apple 开源仓库](https://github.com/apple-oss-distributions/xnu/tree/xnu-8796.121.2) | `xnu/apply.sh` 固定来源并应用补丁；实际提交见 `reference/v259/inputs.json` |
| Kernel.framework 参考头文件 | [MacOSX13.3 SDK 获取入口](https://github.com/joseluisq/macosx-sdks/releases/tag/13.3) | 这是 `xnu-deps/setup.sh` 采用的第三方分发来源，不是本项目打包的 SDK；setup 固定下载/校验 |
| EFI 的 Windows LLVM 工具链 | [llvm-mingw 20261020 UCRT x86-64](https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip) | 原 host builder 期望 `../toolchains/llvm-mingw-20261020-ucrt-x86_64/bin/`；这与 Linux iOS toolchain 是两套工具 |
| 7-Zip 与 LZMA SDK | [7-Zip 下载](https://www.7-zip.org/download.html)、[LZMA SDK](https://www.7-zip.org/sdk.html) | 提取 APFS 需 7-Zip 23+；LZMA C encoder 用于 Mu 重新封装，`marble-build.py --lzma-sdk` 指向源码目录 |

Linux toolchain 固定 SHA256：

```text
a72a7a577e2fbe2838b6b5e9c72034fa7d114af96f0e1d4b016f18730ce4056e
```

原 Debian 构建包依赖（具体见 `MARBLE-BUILD.md`）：

```sh
sudo apt-get install git curl build-essential clang lld python3 python3-pip   flex bison m4 autoconf automake libtool pkg-config libxml2-dev libssl-dev   cmake ninja-build rsync libzstd-dev zlib1g-dev uuid-dev libbsd-dev gawk
```

## 外围/音频材料生成顺序

1. 保留 ROM 来源、版本和 SHA256，在主机运行 `tools/marble-rom-inspect.py ROM.zip --out out/n12turbo-rom`。
   此工具处理原输入使用的 ZIP + `images/super.img.zst` 格式；其他包格式需单独适配。
2. 将需要的 vendor/odm/vendor_dlkm、设备树、GPU 与 ADSP 固件只读提取到主机，
   由 `marble-peripheral-material.py`、`marble-pil-prepare.py`、`marble-pil-files.py`
   形成各自受校验的输入。完整历史布局尚未变成干净环境的一键生成流水线。
3. `tools/marble-adsp-package.py` 只接受两份固定的本地 PIL/firmware 输入，生成
   `MADSP001` 包；不是从网上下载一个可通用的音频驱动。当前依赖大小 30180144 字节，SHA256：

   ```text
   ce5a2ee6534ea6fe4e5d2272252989854a3e0277893f49c87d63376458f7d237
   ```

4. 自行生成内核/profile、支持盘、loader 与 packer 参考模板，再按新候选流程验收。
   `reference/v259/build-command.json` 是历史参数参考，涉及的旧 `out/` 文件未提供。

## 主机配置示例

```powershell
$env:MARBLE_ANDROID_SERIAL = '自己的设备序列号'
$env:MARBLE_ADB = 'C:/adb/adb.exe'
$env:MARBLE_FASTBOOT = 'C:/adb/fastboot.exe'
$env:MARBLE_MU_IMAGE = 'D:/自己的材料/Mu-marble.img'
```

基础 EFI 打包器接受显式参数（先查看 `--help`）：

```powershell
python tools/marble-build.py --llvm ../toolchains/llvm-mingw-20261020-ucrt-x86_64/bin   --lzma-sdk ../toolchains/lzma-sdk --source $env:MARBLE_MU_IMAGE
```

上面换行符是示意；PowerShell 实际输入需一行，或使用反引号续行。
音频/外围 builder 支持 `--mu-image`，默认读 `MARBLE_MU_IMAGE` 或当前目录的 `Mu-marble.img`。

## 可选工具

- Python 的 `zstandard`：ROM 的 zstd 流提取；`python -m pip install zstandard`。
- `paramiko`、`cryptography`、`pycdlib`：使用原 VMware 自动准备/SSH 辅助工具时需要。
- `Pillow`、`unicorn`、`capstone`：截图分析和指令/固件模型；仅在相应脚本需要时安装。
- VMware/QEMU：隔离构建或上游 QEMU 开发；它们不提供 marble 的设备固件。

## 尚无法公开提供的启动材料

匹配 Mu 的来源/源码、匹配 ROM 原下载入口，以及旧 v211 EFI 模板的干净重建方式
仍是明确缺口。没有这些输入，仅克隆源码可以做离线检查，不能声称一键复现手机启动。
请勿用其他机型 boot.img/固件凑齐文件名，也不要刷入本仓库的历史 redwood prebuilt。
