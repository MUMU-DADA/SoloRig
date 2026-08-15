# SoloRig C ABI 后端契约（M1/M2）

`solorig_backend.h` 是独立程序 UI 使用的稳定边界。公开头文件只使用固定宽度整数、浮点数、UTF-8 `const char*` 和不透明句柄，不暴露 C++ STL、引用或异常。

- `SoloRigBackend*` 与 `SoloRigSnapshot*` 必须由同一个 `SoloRigBackend.dll` 创建和释放；调用者先释放 snapshot，再释放 backend。
- 所有字符串均为 UTF-8、NUL 结尾。snapshot 中的字符串只在该 snapshot 存活期间有效；错误字符串只保证到同一 backend 的下一次调用前有效。
- backend API 是独立程序控制/UI 线程 API，不保证线程安全；不得从音频回调、VST3 `process` 或其他实时线程调用。调用者应在停止设备/处理后销毁 backend。
- 创建 backend 和首次快照只构造内存中的默认 RigEditor，不枚举或打开 MIDI、不访问用户文件、不连接 ASIO，也不进入 VST3 处理。真实 ASIO 由 Standalone 控制线程显式使用 `standalone/asio_host.hpp` 管理，不从 C ABI 创建路径隐式启动。
- 失败调用返回明确的 `SoloRigStatus`，并保持 backend 的当前有效状态不变；Save/Open 只有在显式调用时才访问 `.srig` 文件。
- 快照是不可变的 UI 读模型。参数写入、主题切换和文件操作完成后，UI 应丢弃旧 snapshot 并重新获取；C++ 端不会把快照指针写回运行时图。
- M2 编辑命令 `solorig_backend_set_viewport`、`select_at`、`begin_drag`、`drag_to` 和 `end_drag` 只接收数值坐标/标志；坐标为当前视口像素坐标，`dpi_scale` 必须为有限正数。拖拽结果由 C++ `PedalLayout` 网格校验和吸附，失败不替换有效场景。
- M2 快照额外返回 viewport、DPI、dirty、连接线和节点选中状态。C# 端将物理坐标按同一 `dpi_scale` 投影回 Avalonia 设备无关坐标，避免保存后位置漂移。
- Save/Open 仍只能由显式 UI 命令触发；取消只更新 UI 状态，失败路径重新读取后端快照但保留原节点、布局、主题、来源和 dirty 状态。

M17 额外提供两个控制线程诊断视图：`solorig_backend_realtime_contract` 返回 Prepare/Process 分界、无分配/无文件/无 JSON/无锁标志、过采样上限和线程规则；`solorig_backend_whitebox_status` 返回不可变图、受限运行时和 M16 证据链状态。两者只返回固定宽度结构和 DLL 静态 UTF-8 字符串，不进入音频回调，也不暴露 STL。M23-D1 的 Standalone ASIO 主机已能通过显式探针连接单设备；C ABI 诊断视图仍不自动打开设备，因此尚未填充产品级 host 性能字段。

M1/M2/M17 的历史边界不交付 C# MIDI Learn/Program Change、真实 ASIO、多设备、VST3 C# UI 嵌入、通用 PedalKernel 等价电路求解或 1:1 白盒；当前 M23-D1 已交付单设备真实 ASIO 主机，但多设备、完整产品音频图和 1:1 白盒仍未完成；M10 BJT/MNA 工作树继续保持未提交。
