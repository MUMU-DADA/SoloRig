# C# + Avalonia M1 骨架

项目分为两个层次：

- `SoloRig.Ui`：不依赖 Avalonia 的共享 ViewModel 和 `BackendSession`。它只通过 `solorig_backend.h` 的 C ABI 获取不可变快照、写入参数/主题，并暴露显式 Save/Open 方法。
- `SoloRig.Standalone`：Avalonia 桌面壳，显示快照中的 Drive/Delay 节点、参数、音频状态、MIDI closed 状态和文件状态。启动只创建默认内存 backend，不枚举设备、不访问用户文件。
- `SoloRig.Ui.ContractTests`：无测试框架依赖的端到端契约冒烟，验证 C# → P/Invoke → C++ 快照和参数写入。

当前机器已有 Avalonia 11.0.10 本地包缓存。离线构建示例：

```powershell
$env:DOTNET_CLI_HOME = Join-Path $env:TEMP 'solorig-dotnet-home'
$env:NUGET_PACKAGES = Join-Path $env:USERPROFILE '.nuget\packages'
dotnet restore src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --ignore-failed-sources
dotnet build src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj --no-restore -c Release
dotnet build src\managed\SoloRig.Ui.ContractTests\SoloRig.Ui.ContractTests.csproj --no-restore -c Release
```

构建前需先生成 `build\bin\SoloRigBackend.dll`；项目文件会把它复制到 C# 输出目录。真实 ASIO、VST3 UI 嵌入、实时线程通信和电路求解不属于本骨架。
