# SoloRig 一键构建

根目录 `build.ps1` 在 Windows PowerShell 中完成 Release 构建。默认不猜测远程仓库，而是使用当前源码目录：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

如需从指定仓库浅克隆后构建，必须同时显式给出地址和分支/标签：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 `
  -RepositoryUrl https://example.invalid/SoloRig.git -RepositoryRef main
```

每次执行只删除项目根目录下的 `build`，随后重新配置和编译；显式仓库会被克隆到系统临时目录，并在结束时清理。`-SkipTests` 仅用于诊断或脚本自测，正常交付构建应省略该开关，以运行 CTest、VST3 Validator 和 C# 契约测试。

产物位于 `build`：native/Win32/VST3 在 `build\bin` 与 `build\VST3\Release`，Avalonia 发布输出在 `build\managed`。`build\build.log` 同步记录每个外部命令的标准输出和错误输出，`build\SoloRigBuildManifest.json` 记录源码模式、生成器、产物位置及 `testsExecuted`。

离线构建使用本机 `%USERPROFILE%\.nuget\packages` 缓存；缺少 CMake、Visual Studio x64 工具链、dotnet 或 NuGet 包时，脚本会以非零退出并标明失败层级。脚本不访问 MIDI/ASIO，也不会在默认启动时读写用户预设。

构建脚本自测（会执行两次同源重建，并验证缺少远程地址、无效仓库引用及临时目录清理；不会访问未知远程仓库）：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tests\build_script_self_test.ps1
```
