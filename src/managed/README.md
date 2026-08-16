# C# + Avalonia Standalone 产品编辑工作流

当前共享视图提供固定 ASIO INPUT/OUTPUT 物理端口、效果器输出端点到输入端点的点击连接、ASIO 设备/物理通道选择、启动/停止/恢复、调音器开关，以及 MIDI 设备刷新/打开/关闭、目标选择、CC Learn/取消、绑定删除/清空和 MIDI Bank 导入。ASIO 打开期间路由与设备刷新入口锁定，MIDI 打开期间设备选择与刷新入口锁定，关闭后恢复编辑；模块库支持按钮添加和拖入画布按落点添加，新增节点自动接入当前链路末端；模块库筛选和检查器页签显示明确选中态，Standalone 顶栏显示当前预设路径和未保存标记；参数、主题、节点、连接、模块添加/删除/拖动和 MIDI 文档编辑支持撤销/重做，ASIO/MIDI 打开期间历史入口自动锁定；无选中节点、无连接或无 MIDI 绑定时对应操作入口会禁用，画布空白点击可清除选中；预设库支持显式目录扫描、逐项 `.srig` 校验、无效条目门禁、dirty guard 加载和 `Ctrl+PageUp/PageDown` 切换。C# Standalone 已是 self-contained 独立程序生产入口；真实硬件成功打开、双设备恢复、真实 MIDI 设备压力和真实 DAW 回灌仍是后续宿主门禁。

VST3 Controller 通过 bundle 内标准布局的 hostfxr/runtime 加载同一 SoloRig.Ui.Avalonia 程序集，在独立 STA/UI 线程中调用同一个 RigEditorView 和 RigEditorViewModel，创建 Avalonia 子 HWND 后挂载到 VST3 IPlugView 的 Windows 父窗口；VST3 bundle 同时携带同一 SoloRigBackend.dll，backend 或本地 runtime 不可加载时桥接直接拒绝挂载，避免出现无功能的空界面；Standalone 与 VST3 不再维护两套 C# 界面。

项目分为两个层次：

- `SoloRig.Ui`：不依赖 Avalonia 的共享 ViewModel 和 `BackendSession`。它只通过稳定 C ABI 获取不可变快照，并完成 DPI 坐标投影、节点选择/拖拽吸附、参数/主题编辑和显式 Save/Open 的事务状态。
- `SoloRig.Ui.Avalonia`：可被 Standalone 和 VST3 `IPlugView` 宿主复用的 `RigEditorView` 控件；界面代码不再绑定到 Standalone 窗口壳。
- `SoloRig.Standalone`：C# Avalonia 桌面程序壳，承载共享 `RigEditorView`，显示模块目录/搜索、节点按钮添加/拖入画布/删除/复制、连接反馈、连接线、节点卡片、参数编辑、主题切换、预设列表/预设库/新建/快捷默认、打开/保存/另存为、性能/延迟、显式资源识别和取消/错误状态。`--standalone-visual-probe` 检查真实窗口、跨 DPI 几何、预设库无效条目门禁和实际 PNG 渲染面并自动退出。
- `SoloRig.Ui.ContractTests`：无测试框架依赖的端到端契约冒烟，验证 C# → P/Invoke → C++ 的布局、参数、模块目录、事务编辑、主题、性能、显式资源导入、预设库扫描/校验/加载、预设快捷切换、Save/Open 往返以及失败保持状态。

当前机器已有 Avalonia 11.0.10 本地包缓存。离线构建示例：

```powershell
$env:DOTNET_CLI_HOME = Join-Path $env:TEMP 'solorig-dotnet-home'
$env:NUGET_PACKAGES = Join-Path $env:USERPROFILE '.nuget\packages'
dotnet restore src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --ignore-failed-sources
dotnet build src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --no-restore -c Release
dotnet build src\managed\SoloRig.Ui.ContractTests\SoloRig.Ui.ContractTests.csproj --no-restore -c Release
```

构建前需先生成 `build\bin\SoloRigBackend.dll`；项目文件会把它复制到 C# 输出目录。资源入口只在用户显式导入时识别 `.nam`、`.nam-a2`、`.ir`、`.wav` 和外部 `circuit.json`，不会在启动或实时路径读文件；不会把识别冒充模型加载。C# Standalone 与 VST3 managed bundle 均封装本机 .NET 8 runtime/hostfxr，并已在禁用全局 runtime 探索下分别通过 visual/lifecycle probe；实时线程继续由 C++ 原生运行时负责，真实 DAW 音频回灌、安装器签名和跨版本 runtime 升级策略仍需单独验收。
