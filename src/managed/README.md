# C# + Avalonia Standalone 产品编辑工作流

当前共享视图提供固定 ASIO INPUT/OUTPUT 物理端口、效果器输出端点到输入端点的点击连接、ASIO 设备/物理通道选择、启动/停止/恢复、调音器开关，以及 MIDI 设备刷新/打开/关闭/控制线程排空。真实硬件成功打开、双设备恢复、MIDI Learn/Bank 完整产品控件和 VST3 UI 嵌入仍是后续宿主门禁。

项目分为两个层次：

- `SoloRig.Ui`：不依赖 Avalonia 的共享 ViewModel 和 `BackendSession`。它只通过稳定 C ABI 获取不可变快照，并完成 DPI 坐标投影、节点选择/拖拽吸附、参数/主题编辑和显式 Save/Open 的事务状态。
- `SoloRig.Ui.Avalonia`：可被 Standalone 和未来 VST3 `IPlugView` 宿主复用的 `RigEditorView` 控件；界面代码不再绑定到 Standalone 窗口壳。
- `SoloRig.Standalone`：C# Avalonia 桌面程序壳，承载共享 `RigEditorView`，显示模块目录/搜索、节点添加/删除/复制、连接反馈、连接线、节点卡片、参数编辑、主题切换、预设列表/新建/快捷默认、打开/保存/另存为、性能/延迟、显式资源识别和取消/错误状态。`--standalone-visual-probe` 检查真实窗口并自动退出。
- `SoloRig.Ui.ContractTests`：无测试框架依赖的端到端契约冒烟，验证 C# → P/Invoke → C++ 的布局、参数、模块目录、事务编辑、主题、性能、显式资源导入、预设快捷切换、Save/Open 往返以及失败保持状态。

当前机器已有 Avalonia 11.0.10 本地包缓存。离线构建示例：

```powershell
$env:DOTNET_CLI_HOME = Join-Path $env:TEMP 'solorig-dotnet-home'
$env:NUGET_PACKAGES = Join-Path $env:USERPROFILE '.nuget\packages'
dotnet restore src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --ignore-failed-sources
dotnet build src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --no-restore -c Release
dotnet build src\managed\SoloRig.Ui.ContractTests\SoloRig.Ui.ContractTests.csproj --no-restore -c Release
```

构建前需先生成 `build\bin\SoloRigBackend.dll`；项目文件会把它复制到 C# 输出目录。资源入口只在用户显式导入时识别 `.nam`、`.nam-a2`、`.ir`、`.wav` 和外部 `circuit.json`，不会在启动或实时路径读文件；不会把识别冒充模型加载。真实 ASIO/MIDI 控制和 VST3 UI 嵌入将在后续 C# 宿主桥里接入，实时线程继续由 C++ 原生运行时负责。
