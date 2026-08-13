[CmdletBinding()]
param(
    [Parameter()]
    [string]$RepositoryUrl,

    [Parameter()]
    [string]$RepositoryRef,

    [Parameter()]
    [string]$SourceDirectory,

    [Parameter()]
    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$script:BuildLogPath = $null

function Fail-Layer {
    param(
        [Parameter(Mandatory = $true)][string]$Layer,
        [Parameter(Mandatory = $true)][string]$Message
    )
    throw "[$Layer] $Message"
}

function Invoke-Tool {
    param(
        [Parameter(Mandatory = $true)][string]$Layer,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter()][string[]]$Arguments = @()
    )
    Write-Host ("> {0} {1}" -f $Executable, ($Arguments -join " ")) -ForegroundColor DarkGray
    if ($null -ne $script:BuildLogPath) {
        Add-Content -LiteralPath $script:BuildLogPath -Value ("`r`n===== {0} =====" -f $Layer) -Encoding UTF8
        Add-Content -LiteralPath $script:BuildLogPath -Value ("> {0} {1}" -f $Executable, ($Arguments -join " ")) -Encoding UTF8
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $script:BuildLogPath -Append
    } else {
        & $Executable @Arguments 2>&1
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Fail-Layer -Layer $Layer -Message ("命令退出码 {0}: {1}；详细输出见 {2}" -f $exitCode, $Executable, $script:BuildLogPath)
    }
}

function Find-CommandPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        return $null
    }
    return $command.Source
}

function Find-VsInstallPath {
    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )
    $vswhere = $null
    foreach ($candidate in $vswhereCandidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            $vswhere = $candidate
            break
        }
    }
    if ($null -eq $vswhere) {
        $vswhereCommand = Find-CommandPath -Name "vswhere.exe"
        if ($null -ne $vswhereCommand) {
            $vswhere = $vswhereCommand
        }
    }
    if ($null -eq $vswhere) {
        return $null
    }
    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($installation)) {
        return $null
    }
    return $installation.Trim()
}

function Find-CMakeTool {
    param([Parameter(Mandatory = $true)][string]$Name)
    $fromPath = Find-CommandPath -Name $Name
    if ($null -ne $fromPath) {
        return $fromPath
    }
    $vsInstall = Find-VsInstallPath
    if ($null -ne $vsInstall) {
        $candidate = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$Name"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

function Assert-SafeBuildDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot
    )
    $root = (Resolve-FullPath -Path $ProjectRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $build = (Resolve-FullPath -Path $BuildRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $parent = (Split-Path -Parent $build).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $leaf = Split-Path -Leaf $build
    if ($leaf -ne "build" -or $parent -ne $root -or $build -eq $root) {
        Fail-Layer -Layer "Safety" -Message ("清理目标不是项目根目录下的 build: {0}" -f $build)
    }
}

function Find-Artifact {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter()][switch]$Directory
    )
    if ($Directory) {
        return Get-ChildItem -LiteralPath $Root -Recurse -Directory -Filter $Name -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    return Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction SilentlyContinue |
        Select-Object -First 1
}

function Select-CMakeGenerator {
    param([Parameter(Mandatory = $true)][string]$CMake)
    if ($null -ne (Get-Command nmake.exe -ErrorAction SilentlyContinue)) {
        return "NMake Makefiles"
    }
    $help = (& $CMake --help 2>&1 | Out-String)
    foreach ($candidate in @("Visual Studio 18 2026", "Visual Studio 17 2022")) {
        if ($help -match [regex]::Escape($candidate)) {
            return $candidate
        }
    }
    Fail-Layer -Layer "CMake" -Message "未找到可用的 Visual Studio CMake 生成器；请安装 VS C++ 工具和 CMake。"
}

function Import-VsDeveloperEnvironment {
    param([Parameter(Mandatory = $true)][string]$VsInstall)
    $vsDevCmd = Join-Path $VsInstall "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        Fail-Layer -Layer "Tools" -Message ("找不到 Visual Studio 开发环境脚本: {0}" -f $vsDevCmd)
    }
    $command = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 >nul 2>&1 && set'
    $environmentLines = & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        Fail-Layer -Layer "Tools" -Message "Visual Studio C++ 开发环境初始化失败。"
    }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
            switch ($name) {
                "PATH" { $env:PATH = $value }
                "VSINSTALLDIR" { $env:VSINSTALLDIR = $value }
                "VCINSTALLDIR" { $env:VCINSTALLDIR = $value }
                "VCToolsInstallDir" { $env:VCToolsInstallDir = $value }
                "VCToolsVersion" { $env:VCToolsVersion = $value }
                "WindowsSdkDir" { $env:WindowsSdkDir = $value }
                "WindowsSDKVersion" { $env:WindowsSDKVersion = $value }
                "UniversalCRTSdkDir" { $env:UniversalCRTSdkDir = $value }
                "UCRTVersion" { $env:UCRTVersion = $value }
            }
        }
    }
    $toolRoot = Join-Path $VsInstall "VC\Tools\MSVC"
    $nativeTool = Get-ChildItem -LiteralPath $toolRoot -Recurse -File -Filter "nmake.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "HostX64\\x64" } |
        Select-Object -First 1
    if ($null -ne $nativeTool) {
        $env:PATH = "{0};{1}" -f $nativeTool.DirectoryName, $env:PATH
    }
    $sdkRoot = $env:WindowsSdkDir
    $sdkVersion = $env:WindowsSDKVersion
    if (-not [string]::IsNullOrWhiteSpace($sdkRoot) -and -not [string]::IsNullOrWhiteSpace($sdkVersion)) {
        $sdkVersion = $sdkVersion.TrimEnd('\')
        $sdkCandidates = @(
            (Join-Path $sdkRoot ("bin\{0}\x64" -f $sdkVersion)),
            (Join-Path $sdkRoot "bin\x64")
        )
        foreach ($sdkCandidate in $sdkCandidates) {
            if (Test-Path -LiteralPath $sdkCandidate) {
                $env:PATH = "{0};{1}" -f $sdkCandidate, $env:PATH
            }
        }
    }
    if ($null -eq (Get-Command nmake.exe -ErrorAction SilentlyContinue)) {
        Fail-Layer -Layer "Tools" -Message "Visual Studio 开发环境已加载，但找不到 x64 nmake.exe。"
    }
    Write-Host ("已加载 Visual Studio x64 开发环境: {0}" -f $VsInstall)
}

$scriptRoot = Resolve-FullPath -Path $PSScriptRoot
$outputBuildRoot = Join-Path $scriptRoot "build"
Assert-SafeBuildDirectory -ProjectRoot $scriptRoot -BuildRoot $outputBuildRoot

$downloadRoot = $null
$sourceRoot = $null
$sourceMode = "current-source"

try {
    if (-not [string]::IsNullOrWhiteSpace($RepositoryUrl)) {
        if ([string]::IsNullOrWhiteSpace($RepositoryRef)) {
            Fail-Layer -Layer "Download" -Message "显式 RepositoryUrl 必须同时提供 RepositoryRef。"
        }
        $downloadRoot = Join-Path ([IO.Path]::GetTempPath()) ("SoloRig-build-source-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
        $cloneTarget = Join-Path $downloadRoot "source"
        $git = Find-CommandPath -Name "git.exe"
        if ($null -eq $git) {
            Fail-Layer -Layer "Download" -Message "找不到 git.exe，无法执行显式源码下载。"
        }
        Write-Host ("使用显式源码仓库: {0}#{1}" -f $RepositoryUrl, $RepositoryRef)
        Invoke-Tool -Layer "Download" -Executable $git -Arguments @(
            "clone", "--depth", "1", "--single-branch", "--branch", $RepositoryRef, $RepositoryUrl, $cloneTarget
        )
        $sourceRoot = Resolve-FullPath -Path $cloneTarget
        $sourceMode = "repository-clone"
    }
    else {
        if (-not [string]::IsNullOrWhiteSpace($RepositoryRef)) {
            Fail-Layer -Layer "Download" -Message "未提供 RepositoryUrl，不能单独使用 RepositoryRef；默认使用当前源码。"
        }
        $sourceRoot = if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
            $scriptRoot
        } else {
            Resolve-FullPath -Path $SourceDirectory
        }
        if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot "CMakeLists.txt"))) {
            Fail-Layer -Layer "Source" -Message ("源码目录缺少 CMakeLists.txt: {0}" -f $sourceRoot)
        }
        Write-Host ("未提供远程地址，使用当前源码: {0}" -f $sourceRoot)
    }

    $cmake = Find-CMakeTool -Name "cmake.exe"
    $ctest = Find-CMakeTool -Name "ctest.exe"
    if ($null -eq $cmake -or $null -eq $ctest) {
        Fail-Layer -Layer "Tools" -Message "找不到 cmake.exe/ctest.exe；请安装 Visual Studio CMake 工具或将 CMake 加入 PATH。"
    }
    $dotnet = Find-CommandPath -Name "dotnet.exe"
    if ($null -eq $dotnet) {
        Fail-Layer -Layer "Tools" -Message "找不到 dotnet.exe，无法构建 Avalonia 项目。"
    }
    $vsInstall = Find-VsInstallPath
    if ($null -eq $vsInstall) {
        Fail-Layer -Layer "Tools" -Message "CMake 选择了 Visual Studio 生成器，但找不到对应的 VS 安装路径。"
    }
    Import-VsDeveloperEnvironment -VsInstall $vsInstall
    $generator = Select-CMakeGenerator -CMake $cmake

    if (Test-Path -LiteralPath $outputBuildRoot) {
        Write-Host ("清理项目 build 目录: {0}" -f $outputBuildRoot)
        Remove-Item -LiteralPath $outputBuildRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $outputBuildRoot -Force | Out-Null
    $script:BuildLogPath = Join-Path $outputBuildRoot "build.log"
    Set-Content -LiteralPath $script:BuildLogPath -Value ("SoloRig build started {0}" -f [DateTime]::UtcNow.ToString("o")) -Encoding UTF8

    $cmakeConfigureArguments = @(
        "-S", $sourceRoot,
        "-B", $outputBuildRoot,
        "-G", $generator,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTING=ON",
        "-DSOLO_RIG_BUILD_VST3=ON",
        "-DSOLO_RIG_BUILD_STANDALONE=ON"
    )
    if ($generator -like "Visual Studio*") {
        $cmakeConfigureArguments += @("-A", "x64")
    }
    Invoke-Tool -Layer "CMake configure" -Executable $cmake -Arguments $cmakeConfigureArguments
    $cmakeBuildArguments = @("--build", $outputBuildRoot, "--config", "Release")
    if ($generator -notlike "NMake*") {
        $cmakeBuildArguments += @("--parallel", "4")
    }
    Invoke-Tool -Layer "CMake build" -Executable $cmake -Arguments $cmakeBuildArguments

    $backendDll = Find-Artifact -Root $outputBuildRoot -Name "SoloRigBackend.dll"
    $standaloneExe = Find-Artifact -Root $outputBuildRoot -Name "SoloRigStandalone.exe"
    $validatorExe = Find-Artifact -Root $outputBuildRoot -Name "validator.exe"
    $vst3Bundle = Find-Artifact -Root $outputBuildRoot -Name "SoloRig.vst3" -Directory
    foreach ($artifact in @(
        @{ Name = "SoloRigBackend.dll"; Value = $backendDll },
        @{ Name = "SoloRigStandalone.exe"; Value = $standaloneExe },
        @{ Name = "validator.exe"; Value = $validatorExe },
        @{ Name = "SoloRig.vst3"; Value = $vst3Bundle }
    )) {
        if ($null -eq $artifact.Value) {
            Fail-Layer -Layer "Artifacts" -Message ("Release 构建缺少产物: {0}" -f $artifact.Name)
        }
    }

    $managedRoot = Join-Path $outputBuildRoot "managed"
    $managedStandaloneOutput = Join-Path $managedRoot "standalone"
    $managedContractOutput = Join-Path $managedRoot "contract-tests"
    New-Item -ItemType Directory -Path $managedStandaloneOutput -Force | Out-Null
    New-Item -ItemType Directory -Path $managedContractOutput -Force | Out-Null

    $env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
    $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = "1"
    $env:DOTNET_CLI_HOME = Join-Path ([IO.Path]::GetTempPath()) "solorig-dotnet-home"
    $localNuGetPackages = Join-Path $env:USERPROFILE ".nuget\packages"
    if (Test-Path -LiteralPath $localNuGetPackages) {
        $env:NUGET_PACKAGES = $localNuGetPackages
        Write-Host ("使用本机 NuGet 包缓存: {0}" -f $localNuGetPackages)
    } else {
        Fail-Layer -Layer ".NET configuration" -Message ("本机 NuGet 包缓存不存在: {0}" -f $localNuGetPackages)
    }

    if ($sourceMode -eq "repository-clone") {
        $sourceBuildBin = Join-Path $sourceRoot "build\bin"
        New-Item -ItemType Directory -Path $sourceBuildBin -Force | Out-Null
        Copy-Item -LiteralPath $backendDll.FullName -Destination (Join-Path $sourceBuildBin "SoloRigBackend.dll") -Force
    }
    $standaloneProject = Join-Path $sourceRoot "src\managed\SoloRig.Standalone\SoloRig.Standalone.csproj"
    $contractProject = Join-Path $sourceRoot "src\managed\SoloRig.Ui.ContractTests\SoloRig.Ui.ContractTests.csproj"
    foreach ($project in @($standaloneProject, $contractProject)) {
        if (-not (Test-Path -LiteralPath $project)) {
            Fail-Layer -Layer ".NET configuration" -Message ("缺少托管项目: {0}" -f $project)
        }
        Invoke-Tool -Layer ".NET restore" -Executable $dotnet -Arguments @(
            "restore", $project, "--ignore-failed-sources", "-p:NuGetAudit=false"
        )
    }
    Invoke-Tool -Layer ".NET Avalonia Release build" -Executable $dotnet -Arguments @(
        "publish", $standaloneProject, "-c", "Release", "--no-restore", "-o", $managedStandaloneOutput
    )
    Invoke-Tool -Layer ".NET contract Release build" -Executable $dotnet -Arguments @(
        "publish", $contractProject, "-c", "Release", "--no-restore", "-o", $managedContractOutput
    )

    $contractDll = Join-Path $managedContractOutput "SoloRig.Ui.ContractTests.dll"
    if (-not (Test-Path -LiteralPath $contractDll)) {
        Fail-Layer -Layer ".NET artifacts" -Message ("契约测试产物不存在: {0}" -f $contractDll)
    }

    if (-not $SkipTests) {
        Invoke-Tool -Layer "CTest" -Executable $ctest -Arguments @(
            "--test-dir", $outputBuildRoot, "-C", "Release", "--output-on-failure"
        )
        $vst3Config = if (Test-Path -LiteralPath (Join-Path $outputBuildRoot "VST3\Release\SoloRig.vst3")) {
            "Release"
        } else {
            "Debug"
        }
        $vst3Path = Join-Path $outputBuildRoot ("VST3\{0}\SoloRig.vst3" -f $vst3Config)
        if (-not (Test-Path -LiteralPath $vst3Path)) {
            Fail-Layer -Layer "VST3 Validator" -Message ("找不到 VST3 bundle: {0}" -f $vst3Path)
        }
        Invoke-Tool -Layer "VST3 Validator" -Executable $validatorExe.FullName -Arguments @($vst3Path)
        Invoke-Tool -Layer "C# contract tests" -Executable $dotnet -Arguments @($contractDll)
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        buildRunId = [guid]::NewGuid().ToString("N")
        startedUtc = [DateTime]::UtcNow.ToString("o")
        sourceMode = $sourceMode
        sourceDirectory = $sourceRoot
        buildDirectory = $outputBuildRoot
        generator = $generator
        testsExecuted = (-not $SkipTests)
        artifacts = [ordered]@{
            backend = $backendDll.FullName
            standalone = $standaloneExe.FullName
            validator = $validatorExe.FullName
            vst3 = $vst3Bundle.FullName
            managedStandalone = $managedStandaloneOutput
            managedContractTests = $managedContractOutput
        }
    }
    $manifestPath = Join-Path $outputBuildRoot "SoloRigBuildManifest.json"
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host ("构建完成，产物清单: {0}" -f $manifestPath) -ForegroundColor Green
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
finally {
    if ($null -ne $downloadRoot -and (Test-Path -LiteralPath $downloadRoot)) {
        Remove-Item -LiteralPath $downloadRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
