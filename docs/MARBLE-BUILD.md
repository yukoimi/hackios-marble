# marble 源码构建与 RAM 启动

本环境于 2026-10-23 在 Windows 主机上的隔离 Debian 13 VMware 虚拟机中完成内核及适配用户态编译。
不共享主机目录、不转接手机 USB。手机测试仍由 Windows 执行 `fastboot boot`，系统盘和 `/private/var` 均在 RAM；不访问 Android `/data`。

## 固定输入

- kernel 仓库基线：`8e61187098f2c26c5daee8f9f72b6416aa64b9ab`。
- userland 仓库基线：`906425b75e02b4f99eb5d7bb1ef7c9583472d772`；其文件合并到 kernel 工作目录。Apple iOS 16.5 系统文件继续来自本地提取，不是从源码编译的整个 iOS。
- Apple XNU：`c54f35ca767986246321eb901baf8f5ff7923f6a`（8796.121.2）。
- Theos SDK：`0222fd5413cf4b9af096f37b4621afa2688572f7` 的 iPhoneOS16.5.sdk。
- Linux iOS toolchain：L1ghtmann release `test-210562a`，SHA256 `a72a7a577e2fbe2838b6b5e9c72034fa7d114af96f0e1d4b016f18730ce4056e`。
- Debian clang/lld 19.1.7、make 4.4.1、Python 3.13.5、gawk 5.2.1。实际环境与依赖提交记录在 `out/linux-build/environment.txt`。

## 现有虚拟机

`../toolchains/marble-linux/build.vmx` 使用 6 vCPU、6 GiB RAM、24 GiB sparse VMDK，源码在 `/home/builder/marble`，THEOS 在 `/home/builder/theos`。
SSH 工具自动读取串口记录中的 guest IP；使用本地专用密钥及固定 host key。不得分发 `build-key`、seed.iso 等私有文件。

```powershell
python tools/marble-build-vm.py start
python tools/marble-build-vm.py status
python tools/marble-build-vm.py exec 'cd /home/builder/marble && bash tools/marble-linux-build.sh kernel'
python tools/marble-build-vm.py exec 'cd /home/builder/marble && bash tools/marble-linux-build.sh userland'
python tools/marble-build-vm.py get /home/builder/marble/out/linux-build/mach.marble-source out/linux-build/mach.marble-source
python tools/marble-build-vm.py get /home/builder/marble/out/linux-build/mach.json out/linux-build/mach.json
python tools/marble-build-vm.py stop
```

本地修改需先用 `put` 同步到 guest；Windows 工作目录并非共享挂载。`xnu/files` 导出改动需同步到 guest 的 `xnu-src/xnu-8796.121.2` 对应文件；仅已有目录时再次运行 setup 不会自动重应用补丁。

## 从干净 Linux 环境准备

安装 `build-essential git curl clang lld python3 flex bison m4 autoconf automake libtool libxml2-dev libssl-dev cmake ninja-build zlib1g-dev libzstd-dev uuid-dev libbsd-dev gawk`。
解压保留权限和符号链接的合并源码包，执行：

```sh
python3 tools/marble-source-normalize.py
bash tools/marble-linux-build.sh setup
bash tools/marble-linux-build.sh kernel
bash tools/marble-linux-build.sh userland
```

脚本固定 toolchain/SDK 版本；上游 `xnu-deps/setup.sh` 固定开源依赖提交，并取得 macOS 13.3 Kernel.framework DriverKit 头文件。
Windows 打包的 SDK 若包含 CRLF，脚本会在 Linux 私有副本归一化，否则旧 TAPI 会拒绝 TBD。
使用 gawk 生成 sysent，避免 Debian mawk 对 `\(` 的处理差异。`xcrun` wrapper 支持从 PATH 找到版本化 LLVM 目录的 dsymutil。

每次内核构建先删除最终链接文件，避免接受旧产物。当前上游导出检查仍使 make 返回 2：
`config/Makefile:108` 的 IOKit/Libkern/Private.symbolset 和 `config/Makefile:121 check_all_exports`；
该候选已检查没有编译错误、缺失工具或未定义链接符号，但不能将其称为 make 全部目标通过。
构建后必须复核 `out/linux-build/kernel.log`，不能只凭退出码 2 接受产物。

## 实机源码候选

源码 GIC 驱动读取 DT `redistributor-stride=0x40000`；保留旧 prebuilt 的固定 hash 路径，源码候选需要显式 `--kernel` 和匹配 `mach.json`。
`MGIC-DT-STRIDE-ABI-v1` 是 ABI 声明，不是签名。

已验证内核 SHA256：`f581f4c6d7e9c346742fcf2c80048865e05921ed4989d2327d1f24a957833e1a`。
新 loader boot SHA256：`6c56782f12091672b7ca6ed7f634cdf4144d751f7b64b158d53a1742366c290d`。
源码内核 CLI 日志 `out/marble-source-kernel-cli.log`：36 checks/0 failed；四线程 60 秒压力 PASS，wall 60303 ms、CPU 241217 ms、计时漂移 0、错误 0；副核全部 PSCI OFF。
只读 EPSS 请求值分别 1555/2342/2822 MHz；这是请求频率而非实测有效频率，无温度遥测。DVFS/DDR 写入仍关闭，小核不开放。

```powershell
python tools/marble-run.py boot --image out/marble-source-loader-boot.img
python tools/marble-run.py touch-probe
python tools/marble-run.py ramdisk ../marble-baselines/20261023-touch-smp4/marble-system.apfs
python -X utf8 tools/marble-userspace.py --mode launchd --support-only --programs out/linux-build/userland --input-qos
python tools/marble-run.py run --kernel out/linux-build/mach.marble-source --ramdisk out/marble-root-launchd.apfs --input-mailbox --cpus 4 --diagnostics --fb-threads 2 --seconds 600 --detach
```

`--gesture-test` 可在 launchd 支持盘中加入延迟 45 秒、六次水平滑动的测试 job；不加该参数会移除该 job。
测试使用相同支持盘比较 `--fb-threads 1/2/4`，用 `tools/marble-perf.py LOG --output REPORT.json` 分析。
软件渲染没有 GL draw markers，上游 frames.py 不能据此分解绘制阶段；本工具报告真实显示完成间隔和拷贝时间，并明确排除的暂停。

构建下载若需代理，可使用 `marble-build-proxy.py` 只绑定 VMnet8 主机 IP 并限制 guest IP；停止文件可结束转发。
本轮结束会移除 guest 临时代理配置，不修改用户主机代理。离线重建可使用已缓存依赖。

源码恢复包保存于 `../marble-baselines/20261023-source-smp4`，独立保留旧恢复包。
包含 system RAM 镜像、loader、源码内核及支持盘、合并源码、构建日志和三轮性能报告；不含虚拟机 SSH 密钥。
用户已确认最终双拷贝线程源码桌面的触摸/显示正常、无异常发热。该反馈不是温度传感器测量。
最终触摸样本 19 次 release 延迟 p95 4 ms/max 4 ms；统计窗口桥最大延迟 6 ms，未发现 stale/overrun/panic。

```powershell
python tools/marble-checkpoint.py ../marble-baselines/20261023-source-smp4 --verify
```


## GPU context 检查点

`marble-gpu-context-loader-boot.img`配合显式`mach-marble-gpu-context`候选，支持`--gpu-probe 4/5`。
第4阶段只读GR1/CB/GMU状态；第5阶段复用ArmSmmu4K编程、实际页表审计、禁用/TLBSYNC及清零页表根。
marble的ATOSNS=1，不执行标准ATS；输出translation=-1，不代表硬件地址翻译通过。
已知失败候选的JSON标记FAILED，控制器拒绝运行。构建导出检查仍有上述四项既存失败。

```powershell
python tools/marble-run.py boot --image out/marble-gpu-context-loader-boot.img
python tools/marble-run.py touch-probe
python tools/marble-run.py ramdisk out/marble-system.apfs
python tools/marble-run.py run --kernel out/linux-build/mach-marble-gpu-context --ramdisk out/marble-root-gpu-cli.apfs --input-mailbox --cpus 4 --gpu-probe 5 --seconds 45
```

阶段结果、候选哈希及已知失败原因见`MARBLE-GPU.md`。仍不刷分区，不访问Android userdata。

## GPU ECATS 检查点

`../marble-baselines/20261024-gpu-ecats`保存第6/7阶段loader、独立源码内核、支持盘、
固定固件、合并源码和实机日志。两轮CLI硬件翻译/清理通过，第三轮桌面启动，
用户确认触摸、显示正常、无异常发热；尚未启动GMU或GPU渲染。

```powershell
python tools/marble-checkpoint.py ../marble-baselines/20261024-gpu-ecats --verify
python tools/marble-run.py boot --image ../marble-baselines/20261024-gpu-ecats/marble-gpu-tbu-loader-boot.img
python tools/marble-run.py touch-probe
python tools/marble-run.py ramdisk ../marble-baselines/20261023-source-smp4/marble-system.apfs
python tools/marble-run.py run --kernel ../marble-baselines/20261024-gpu-ecats/linux-build/mach-marble-gpu-ecats --ramdisk ../marble-baselines/20261024-gpu-ecats/marble-root-gpu-cli.apfs --input-mailbox --cpus 4 --gpu-probe 7 --seconds 45
```

两轮实机结果可重复解析：
`python tools/marble-ecats-report.py out/20261024-080912-marble.log out/20261024-080940-marble.log --output out/marble-gpu-ecats-report.json`。
`tools/tests/marble-ecats.c`为主机协议fixture，不能代替实机测试；
`marble-smmu-fence.c`同时检查旧阶段拒绝stream写入和第7阶段的精确SID5/slot0值白名单。

## GMU RAM 检查点

`../marble-baselines/20261024-gmu-memory`保存第8阶段镜像和全部证据。
六段51页RAM、6页实际页表、每轮133次ECATS权限/地址测试连续两轮通过，桌面用户确认正常。
仅RAM固件payload装载，三段TCM deferred；没有GMU执行或GPU渲染。

```powershell
python tools/marble-checkpoint.py ../marble-baselines/20261024-gmu-memory --verify
python tools/marble-run.py boot --image ../marble-baselines/20261024-gmu-memory/marble-gmu-memory-loader-boot.img
python tools/marble-run.py touch-probe
python tools/marble-run.py ramdisk ../marble-baselines/20261023-source-smp4/marble-system.apfs
python tools/marble-run.py run --kernel ../marble-baselines/20261024-gmu-memory/linux-build/mach-marble-gmu-memory --ramdisk ../marble-baselines/20261024-gmu-memory/marble-root-gpu-cli.apfs --input-mailbox --cpus 4 --gpu-probe 8 --seconds 45
python tools/marble-gmu-memory-report.py out/20261024-082401-marble.log out/20261024-082424-marble.log --output out/marble-gmu-memory-report.json
```

主机布局fixture为 `tools/tests/marble-gmu-memory.c`，使用检查点固件，链接固定的
AdrenoFirmware、ArmSmmu4K/Context/Program/Audit实现；合成物理地址仅用于主机测试，
没有把它们交给手机。内核始终以kvtophys得到的自有RAM真实PA编程和审计。
