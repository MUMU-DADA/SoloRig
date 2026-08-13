# C# + Avalonia M2 基础编辑器

项目分为两个层次：

- `SoloRig.Ui`：不依赖 Avalonia 的共享 ViewModel 和 `BackendSession`。它只通过稳定 C ABI 获取不可变快照，并完成 DPI 坐标投影、节点选择/拖拽吸附、参数/主题编辑和显式 Save/Open 的事务状态。
- `SoloRig.Standalone`：Avalonia 桌面编辑器，显示连接线、Drive/Delay 节点卡片、选中/拖动反馈、参数编辑、主题切换、打开/保存/另存为和取消/错误状态。启动只创建默认内存 backend，不枚举设备、不访问用户文件；`--m2-visual-probe` 仅用于启动后检查真实窗口的画布、节点和连接数据并自动退出。
- `SoloRig.Ui.ContractTests`：无测试框架依赖的端到端契约冒烟，验证 C# → P/Invoke → C++ 的布局、参数、主题、Save/Open 往返以及失败保持状态。

当前机器已有 Avalonia 11.0.10 本地包缓存。离线构建示例：

```powershell
$env:DOTNET_CLI_HOME = Join-Path $env:TEMP 'solorig-dotnet-home'
$env:NUGET_PACKAGES = Join-Path $env:USERPROFILE '.nuget\packages'
dotnet restore src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --ignore-failed-sources
dotnet build src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --no-restore -c Release
dotnet build src\managed\SoloRig.Ui.ContractTests\SoloRig.Ui.ContractTests.csproj --no-restore -c Release
```

构建前需先生成 `build\bin\SoloRigBackend.dll`；项目文件会把它复制到 C# 输出目录。C# MIDI Learn/Program Change、真实 ASIO、VST3 UI 嵌入、实时线程通信和电路求解不属于 M2。
