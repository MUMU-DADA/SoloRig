# SoloRig 一键构建

构建同时发布 C# Standalone、C# contract 和 VST3 UI bridge；VST3 bundle 的 Contents/Resources/managed 目录包含 hostfxr 所需的 C# UI 程序集、runtimeconfig、nethost.dll 和共享的 SoloRigBackend.dll，VST3 lifecycle 测试会真实挂载同一 Avalonia 视图并要求 backend 可加载。

根目录 `build.ps1` 在 Windows PowerShell 中完成 Release 构建。默认不猜测远程仓库，而是使用当前源码目录：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

如需从指定仓库浅克隆后构建，必须同时显式给出地址和分支/标签：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 `
  -RepositoryUrl https://example.invalid/SoloRig.git -RepositoryRef main
```

默认每次执行只删除项目根目录下的 `build`，随后用 Visual Studio 自带的 Ninja（不可用时回退 Visual Studio 生成器）并行完成重新配置和编译；显式仓库会被克隆到系统临时目录，并在结束时清理。`-SkipTests` 仅用于诊断或脚本自测，正常交付构建应省略该开关，以运行 CTest、VST3 Validator 和 C# 契约测试。

日常开发可复用 CMake 和编译缓存，只重建发生变化的目标：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Incremental
```

首次从旧生成器切换到 Ninja 时，`-Incremental` 会自动执行一次兼容性重建；后续运行才复用缓存。默认并行度由构建工具按机器配置决定，也可用 `-ParallelJobs 8` 显式限制。临时浅克隆因源码目录每次变化，不支持 `-Incremental`。

产物位于 `build`：native backend/ASIO probe 在 `build\bin`，VST3 在 `build\VST3\Release`，C# Avalonia Standalone 在 `build\managed\standalone`，共享 VST3 C# UI 在 `build\managed\vst3-ui`。旧 Direct2D 窗口只作为 Windows 回归夹具编译，不再生成或交付第二个独立程序。`build\build.log` 同步记录每个外部命令的标准输出和错误输出，`build\SoloRigBuildManifest.json` 记录源码模式、生成器、产物位置及 `testsExecuted`。

离线构建使用本机 `%USERPROFILE%\.nuget\packages` 缓存；缺少 CMake、Visual Studio x64 工具链、dotnet 或 NuGet 包时，脚本会以非零退出并标明失败层级。脚本不访问 MIDI/ASIO，也不会在默认启动时读写用户预设。

构建脚本自测（会执行一次干净构建和一次增量构建，并验证缺少远程地址、无效仓库引用及临时目录清理；不会访问未知远程仓库）：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\build_script_self_test.ps1
```
