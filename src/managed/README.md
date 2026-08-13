# C# + Avalonia M5 产品编辑工作流

项目分为两个层次：

- `SoloRig.Ui`：不依赖 Avalonia 的共享 ViewModel 和 `BackendSession`。它只通过稳定 C ABI 获取不可变快照，并完成 DPI 坐标投影、节点选择/拖拽吸附、参数/主题编辑和显式 Save/Open 的事务状态。
- `SoloRig.Standalone`：Avalonia 桌面编辑器，显示模块目录/搜索、节点添加/删除/复制、串联/断开反馈、连接线、节点卡片、参数编辑、主题切换、预设列表/新建/快捷默认、打开/保存/另存为、性能/延迟、显式资源识别和取消/错误状态。启动只创建默认内存 backend，不枚举设备、不访问用户文件；`--m5-visual-probe` 检查真实窗口的画布、模块目录、性能和预设控件并自动退出（保留 `--m2-visual-probe` 兼容）。
- `SoloRig.Ui.ContractTests`：无测试框架依赖的端到端契约冒烟，验证 C# → P/Invoke → C++ 的布局、参数、模块目录、事务编辑、主题、性能、显式资源导入、预设快捷切换、Save/Open 往返以及失败保持状态。

当前机器已有 Avalonia 11.0.10 本地包缓存。离线构建示例：

```powershell
$env:DOTNET_CLI_HOME = Join-Path $env:TEMP 'solorig-dotnet-home'
$env:NUGET_PACKAGES = Join-Path $env:USERPROFILE '.nuget\packages'
dotnet restore src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --ignore-failed-sources
dotnet build src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --no-restore -c Release
dotnet build src\managed\SoloRig.Ui.ContractTests\SoloRig.Ui.ContractTests.csproj --no-restore -c Release
```

构建前需先生成 `build\bin\SoloRigBackend.dll`；项目文件会把它复制到 C# 输出目录。M5 的资源入口只在用户显式导入时识别 `.nam`、`.nam-a2`、`.ir`、`.wav` 和外部 `circuit.json`，不会在启动或实时路径读文件；不会把识别冒充模型加载。C# MIDI Learn/Program Change、真实 ASIO、VST3 UI 嵌入、实时线程通信和新的 MNA/1:1 电路仍不属于 M5。
