# marble 显示同步进度 — 2026-10-26

用户已确认带 GPU 的预览发热可以接受，但交互卡顿。工作重心转为显示同步、帧耗时，
然后是日常功能。芯片瞬时温度不作为电池过热结论，不再反复运行空闲降温实验。

## 已确认的数据

`out/gpu-preview-v1-live-connected` 完成约90秒 GPU 桌面预览：FD725 CoreAnimation
硬件合成日志存在，桌面截图中 Dock 有模糊。电池31.7→33.6°C；芯片运行样本最高
50.3°C，连续≥50°C观察跨度最大1秒。GPU active=0、GX/context、CPU请求及EFI
等待寄存器均正常释放并恢复。返回Android熄屏。最初因通知时机太晚，用户查看时
已经回Android，因此又进行了明确供用户查看的一轮。

`out/gpu-preview-v1-user`：用户反馈“能接受，但是卡”。运行约51秒后触发实验的
连续≥50°C跨度10秒退出规则，芯片最高51.4°C，返回电池33.7°C，五项释放/恢复
均通过，Android熄屏。这是实验退出，不能把用户已接受的51°C芯片读数说成电池
过热。没有连续iOS电池温度，前后读数也不是中途峰值证明。

本预览保留既有约1.2GHz CPU请求限制；交互时显示驱动统计约14–23帧/秒，
CPU显示处理约8–10毫秒/帧。FD725典型合成日志约2–3毫秒，但这些打印不是完整
逐帧追踪，不能直接当成GPU全帧时间或证明唯一瓶颈。限频和CPU缩放均待性能比较。

## 硬件时序来源

从实际Android只读导出的DT和DRM节点确认：

- 控制器：`/sys/devices/platform/soc/ae00000.qcom,mdss_mdp`。
- MDP寄存器窗：0xae00000，大小0x84000。
- INTF偏移：0x35000/0x36000/0x37000/0x38000，类型DP/DSI/DSI/DP。
- DSI候选：0xae36000和0xae37000；寄存器区长度0x2c4。
- MDSS父中断：GIC SPI83，架构INTID115。尚未接管该中断。
- 面板列出的video模式：1080x2400、30/60/90/120Hz；没有验证UEFI当前刷新率。
- DISP_CC：0xaf00000，大小0x20000，实际compatible为`qcom,cape-dispcc`。
- `clock-names`包括gcc_iface、gcc_bus、gcc_nrt_bus、iface_clk、branch_clk、core_clk、vsync_clk等。

证据在`out/vsync-v1-plan/android-display-dt.json`、`android-clock-dt.json`和对应
原始二进制。固定MiCode提交48952ed36228217531482b39d5bef13e7fd808ec的
`dispcc-waipio.c`包含cape适配；保存了原始来源URL及SHA256。AHB分支在该源文件
中为0x80a4，AHB源0x8324，另有non-GDSC AHB与GCC总线前提。这些地址是参考，
本轮没有写入这些时钟或任何显示/PMIC电源寄存器。

## 未通过的候选及当前保护

1. `out/vsync-probe-v1-live`：EFI只读探测期间USB丢失，随后Android重新出现，
   无返回日志。未启动XNU/GPU；不能建立真实复位原因。参考Mu内存表没有单独覆盖
   0xae00000的显示窗，但这不是完整实机MMU审计，也不能据此认定唯一原因。
2. `out/vsync-v1-idle`：新内核通过ml_io_map映射后尝试读取显示计数，仍丢失
   loader并回Android；没有正常returned记录。GPU未启动，不能宣称时序/恢复通过。
   对应内核SHA256为875ab26334b38c788f101c50f9bf0e0bf3b1a1e7e8b966ea2b7142a0a5dad7cf，
   manifest标记FAILED，禁止重复。Android screen/actual OFF已重新确认。

EFI探测命令已停用。源码新增`register-access-qualified=1`前提，当前loader不发布
该属性，所以硬件同步路径在任何MMIO读取前退出。必须先完成可恢复的显示总线时钟/
访问权限适配，再提供资格；不能仅手工添加DT属性绕过。原始失败产物独立保留。

## 同步候选的实现范围

`MarbleDisplayTiming.inc`包含按硬件帧计数变化安排显示、按计数变化发送刷新通知、
停更退出的候选代码。编译完成，尚无硬件通过结果。其候选为轮询计数（500us重试），
不是显示中断、原子scanout翻页或无撕裂保证。现有`ca_vbl`仍是软件通知；不能用
60Hz定时器或通知次数作为真实VSync成功的证据。

下一步先核对cape显示总线的继承时钟和权限，建立可恢复的访问所有权；然后只读验证
帧计数、实测周期和扫描位置，再验证滑页/设置滚动。硬件中断与缓冲区翻页需要单独
实现和验收。此后处理按键、亮度、休眠唤醒、电量、音频、网络和传感器，每项单独
验证；本轮没有把这些功能标为完成。
