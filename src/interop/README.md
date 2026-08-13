# SoloRig C ABI 后端契约（M1）

`solorig_backend.h` 是独立程序 UI 使用的稳定边界。公开头文件只使用固定宽度整数、浮点数、UTF-8 `const char*` 和不透明句柄，不暴露 C++ STL、引用或异常。

- `SoloRigBackend*` 与 `SoloRigSnapshot*` 必须由同一个 `SoloRigBackend.dll` 创建和释放；调用者先释放 snapshot，再释放 backend。
- 所有字符串均为 UTF-8、NUL 结尾。snapshot 中的字符串只在该 snapshot 存活期间有效；错误字符串只保证到同一 backend 的下一次调用前有效。
- backend API 是独立程序控制/UI 线程 API，不保证线程安全；不得从音频回调、VST3 `process` 或其他实时线程调用。调用者应在停止设备/处理后销毁 backend。
- 创建 backend 和首次快照只构造内存中的默认 RigEditor，不枚举或打开 MIDI、不访问用户文件、不连接 ASIO，也不进入 VST3 处理。
- 失败调用返回明确的 `SoloRigStatus`，并保持 backend 的当前有效状态不变；Save/Open 只有在显式调用时才访问 `.srig` 文件。
- 快照是不可变的 UI 读模型。参数写入、主题切换和文件操作完成后，UI 应丢弃旧 snapshot 并重新获取；C++ 端不会把快照指针写回运行时图。

M1 仅冻结快照、节点/参数、主题、显式 Save/Open 和 MIDI 状态/映射读取；真实 ASIO、VST3 UI 嵌入、实时 DSP 及通用电路求解均不在该 ABI 内。
