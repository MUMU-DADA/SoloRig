# M17 集成与实时性能契约

本文件记录 M17 的可核验边界。它不把当前局部 MNA、M10 BJT 实验改动或 M16 证据模板扩大为通用白盒、PedalKernel 等价或 `reference-1to1` 完成。

## C ABI 契约

- `SoloRigBackend` 和 `SoloRigSnapshot` 是不透明句柄；DLL 创建、调用方只负责按顺序释放，快照必须先于 backend 释放。重复释放空句柄安全。
- 结构体只使用固定宽度整数、`double` 和 UTF-8 `const char*`，不暴露 STL、C++ 引用或内部容器。快照字符串在 `solorig_snapshot_destroy` 前有效；实时/白盒诊断字符串为 DLL 静态存储，在进程期间有效。
- backend、快照、编辑命令和诊断查询只允许独立程序 UI/控制线程调用，非线程安全；音频回调不得调用 C ABI，也不得进入 .NET。
- 失败状态通过 `SoloRigStatus` 和 `solorig_backend_last_error_utf8` 返回；编辑/文件/资源失败不替换当前有效场景。启动只创建内存默认场景，不扫描文件、不枚举 MIDI、不打开 ASIO。
- `solorig_backend_realtime_contract` 给出 Prepare/Process 分界：编译和所有权准备在控制/Prepare 阶段完成；Process 声明为 C++ `noexcept`、无动态分配、无文件/JSON、无阻塞锁，且不进入 .NET。编辑器 backend 没有真实音频设备，因此采样率、块大小、通道、实际延迟和 CPU 读数为 host-configured/未配置，而不是伪造测量值。
- `solorig_backend_whitebox_status` 只报告 M12 不可变图、M13 受限运行时和 M16 证据链的实际状态；当缺少授权模型、SPICE/golden 或 PedalKernel 输入时保持 blocked/unsupported 文本。

## Avalonia 集成

`SoloRig.Ui.BackendInterop` 只通过上述 C ABI 读取快照、实时契约和白盒状态。`RigEditorViewModel` 在一次刷新中先读取完整快照，再提交状态；打开/保存失败或取消时保留原场景、来源路径和 dirty 状态。UI 可显示编辑器、性能/延迟、实时边界、白盒证据和未配置音频状态，但不会把诊断文本当作音频设备已连接。

启动探针 `--m17-visual-probe` 只创建内存 backend，等待真实 Avalonia 窗口渲染节点、连接、模块目录、预设控件以及实时/白盒状态文本后退出；探针结束清理进程。显式 Save/Open 仍是唯一的预设文件访问入口。

## VST3 与性能证据

VST3 处理器继续直接复用 C++ `RigRuntime`、预构建 mailbox 和宿主参数/状态 seqlock；C# 不参与 `process`。既有 state v1/v2、Drive 映射、Bypass、最后自动化点、重配生命周期测试保留，并新增共享实时契约回归。

当前可审计证据包括：

- `AudioGraph::Process`、`RigRuntime::Process` 的 `noexcept` 静态断言；预分配音频缓冲、过渡和延迟补偿由 Prepare/构建路径拥有。
- `RealtimeLoadMeter` 的确定性块负载/峰值测量，以及快照中已有的图延迟和估算 CPU 字段。
- 过采样契约上限为 16；实际处理采样率由宿主/未来音频适配器在 Prepare 时提供，独立程序当前没有 ASIO 后端，不能声称已经连接设备。

这些是代码结构和自有数学/运行时测试证据，不是硬件基准、SPICE/golden 或商业设备校准结果。

## M17 明确未交付

真实 ASIO/多设备 I/O、VST3 C# 嵌入、PedalKernel 等价、完整通用白盒元件模型、授权模型数据库、SPICE/golden/实机测量、1:1 电路和最终产品视觉仍未完成；M10 BJT/MNA 工作树改动继续保持未提交，不属于本里程碑提交。
