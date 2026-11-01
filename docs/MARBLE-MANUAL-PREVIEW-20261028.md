# 2×十分钟手动预览与首次冻结

用户明确要求把手动测试窗口从90秒延长到十分钟。新增显式`--manual-ten-minute`，
默认90秒实验不变。仅允许完整GPU preview条件下精确600秒：四核、cap、EFI idle、
触摸RAM mailbox、硬件显示计数节拍、probe17、GPU runtime/composition。
loader拒绝重复manual标志、599/601秒、缺少保护或任意时长放大。
温度保护保持启动die<50°C、运行die<55°C、连续≥50°C不足60秒；
Android真实电池新鲜预检<38°C。未改GPU/CPU频率或电压。

支持盘`out/marble-root-gpu-scale-v4-manual-600.apfs`仅在25秒时发送一次解锁通知，
随后不发送自动手势。主机不再在65/78秒主动黑屏／结束；600秒后仍执行正常
退出核对、回Android、熄屏与电池读数。温度或运行异常仍可提前结束。

## 首轮真实手动触摸后冻结

`out/gpu-scale-v4-2x-manual-600-live`为失败证据：
用户报告流畅但几次触摸后不动。日志确认多次真实HID DOWN/UP均成功送达，
释放延迟0–3ms；最后周期至少720次GPU缩放，GL_error=0。
这只说明冻结前输入桥与缩放曾工作，不证明冻结时GPU仍正常。
最后完整die44.7°C、此前采样峰47.9°C，没有记录到温度STOP。

loader日志精确写满32767字节。原`set_result()`通过向该日志临时print，再
把新增字节拷贝到状态串；日志满时新增长度0，状态串变空。
主机多次读到空`result`，温度日志也不再更新；退出流程未取得正常return。
仅有loader日志的`frozen.log`已经保存，原完整kernel快照保存在`logs/052.log`。
**没有把本轮记为GPU资源正常恢复。** 保留现场后用固件冷重启恢复Android，
确认熄屏，电池恢复时32.1°C。

日志饱和解释空状态和诊断丢失，尚不能单独解释整个UI冻结。
另一待核对问题：原观察循环直接读取非volatile的跨核心heartbeat，
没有像`other_cpu_done`那样显式刷新缓存。误判hung会退出观察／触摸轮询。
首轮未记录heartbeat变化，不能直接断言这就是冻结根因。

## 修正与验证

`out/gpu-manual-600-watch-v3-loader-plan`：

- loader日志容量512KiB，仍有严格上限；状态串改为独立有界formatter，日志满
  也能返回running/returned/hung；不再使用print的副作用。
- 每250ms刷新并volatile读取heartbeat，保留原50秒无心跳判断；
  每次变化记录`marble-watch: elapsed_s=... heartbeat=...`，用于核对实际活性。
- 新formatter边界测试覆盖uint64最大值、截断、零容量；原policy、DSI、显示
  phase、thermal-window测试通过。Python`test_marble*.py`45项通过。
- 没有改变GPU渲染内核、Mesa、CPU cap或温度停止阈值。
- 首版600秒loader的manifest标为FAILED，保留修改前manifest，防止误重复启动。

修正版实机目录`out/gpu-scale-v4-2x-manual-600-watch-v3-live`。
是否解决冻结、十分钟是否完整跑完，必须依据该轮实时状态、用户反馈及最终
return/恢复日志判断；不能从构建通过推断。
