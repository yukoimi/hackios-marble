# 2026-11-01 最新：v241 静音欠载/运行中补数两轮通过

用户要求继续直到音频完成，当前任务未完成；仍没有HAL、连续播放或实际出声验收。手机已恢复Android熄屏，无活动USB控制器。
最新完整基线out/audio-refill-native-link-v241-plan，同镜像v241-live/v241b-live两轮通过。
先装512帧→第三页故意欠载补零256帧→PID1只读ioctl确认空队列→运行中补入1280帧→五页真实DATA恢复→quiet ACK→STOP/UNMAP/PAS→XNU unmap ACK→DMA free。
8页与独立原guard oracle逐页一致，输入1792/输出2048帧；原RAM副本也通过，HMUTE/90dB全程保持。
owner新profile移除无用EFI GLink observer注册/注销；pre-AUTH只检查真实XNU READY/cap/epoch，不据此推断DSP link，实际连接仍必须由后续GPR证明。
QMP close新可选双读一致快照，仅在原750ms内等待暂忙/不一致；原transition codec和所有最终原样恢复条件保留。
两轮原始PAS608621/608397us，完整停止611088/610867us；QMP close2405/2408us，unstable均0。
电池前后33.8→34.0、34.8→35.5°C，不代表iOS期间峰值；Android新boot ID和熄屏均通过。
镜像SHA2a1b950c0adb19978d0482e11561bd936862170381080620488b08d1cd257501。
内核SHA16ca42a112fdb20f918aac005803cfa5e663c90b9550bf9b78e9b722a8e90b51。

v238b虽PCM/oracle恢复，但QMP close只1次ACK整轮FAIL；v239 USB丢状态，v240首轮PASS但复测USB断连，均永久禁用，不得重启或解禁。
v240复测最后preauth USB trace为QMP-held-on-return；v241移除旧observer后两轮通过，但尚未穷尽证明之前USB/QMP偶发失败根因；不能宣称所有间歇故障已解决。
启动前ADB offline已由主机重启adb server恢复；这一次预检未启动镜像，不算实机失败。
425项Python全量通过；之后57项audio报告/14项source定向通过，加入两份v241实机fixture后8项refill报告通过；builder72C及323编译输入哈希通过。
内核26项音频源+GIC与实际VM核对，编译/链接/strip成功；完整make仍既存symbolset/export错误exit2。

下一步：24页有限窗口（6144输出/5888输入帧，128ms，超过4096帧队列），保持原600ms诊断上限。
新增marble_audio_stream_window.h严格独立v2/24验证器，原v1/2或8验证器不放宽；真实framed endpoint主机模型通过。
这份24页header尚未绑定内核/EFI/主机，未构建或启动对应镜像。接下来实现生产者保持队列水位与跨4096绕回，显式新ABI/profile，再验证实机；不能通过延长诊断超时冒充持续播放。
全部原600ms、双层PAS各1s、QMP close750ms/总2.5s、温控、watchdog、失败资源保留、冷复位继续保留；仅fastboot boot与RAM，不刷分区、不访问/data。

