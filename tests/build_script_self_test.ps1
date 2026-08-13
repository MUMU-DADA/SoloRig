[CmdletBinding()]
param(
    [switch]$KeepBuildOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildScript = Join-Path $projectRoot "build.ps1"
$buildRoot = Join-Path $projectRoot "build"
$requirementsPath = Join-Path $projectRoot "项目最初预期的需求.txt"
$git = Get-Command git.exe -ErrorAction Stop
$pwsh = (Get-Command pwsh.exe -ErrorAction SilentlyContinue)
if ($null -eq $pwsh) {
    $pwsh = Get-Command powershell.exe -ErrorAction Stop
}

function Invoke-BuildScript {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    Write-Host ("自测执行: build.ps1 {0}" -f ($Arguments -join " ")) -ForegroundColor Cyan
    & $pwsh.Source -NoProfile -ExecutionPolicy Bypass -File $buildScript @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("build.ps1 自测调用失败，退出码 {0}: {1}" -f $LASTEXITCODE, ($Arguments -join " "))
    }
}

function Assert-DownloadArgumentFailure {
    Write-Host "自测执行: build.ps1 -RepositoryRef <当前分支>（应在下载层失败）" -ForegroundColor Cyan
    $output = & $pwsh.Source -NoProfile -ExecutionPolicy Bypass -File $buildScript -RepositoryRef $branch 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        throw "缺少 RepositoryUrl 时脚本意外成功。"
    }
    $text = ($output | Out-String)
    if ($text.IndexOf("[Download]", [StringComparison]::Ordinal) -lt 0) {
        throw ("下载参数失败没有标明 [Download] 层: {0}" -f $text.Trim())
    }
}

function Assert-CloneFailureCleanup {
    $tempRoot = [IO.Path]::GetTempPath()
    $before = @(Get-ChildItem -LiteralPath $tempRoot -Directory -Filter "SoloRig-build-source-*" -ErrorAction SilentlyContinue | ForEach-Object FullName)
    Write-Host "自测执行: build.ps1 -RepositoryUrl <当前仓库> -RepositoryRef <不存在分支>（应在下载层失败并清理临时目录）" -ForegroundColor Cyan
    $output = & $pwsh.Source -NoProfile -ExecutionPolicy Bypass -File $buildScript -RepositoryUrl $projectRoot -RepositoryRef "__solorig_self_test_missing_ref__" -SkipTests 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0) {
        throw "不存在的 RepositoryRef 意外成功。"
    }
    $text = ($output | Out-String)
    if ($text.IndexOf("[Download]", [StringComparison]::Ordinal) -lt 0) {
        throw ("无效仓库引用失败没有标明 [Download] 层: {0}" -f $text.Trim())
    }
    $after = @(Get-ChildItem -LiteralPath $tempRoot -Directory -Filter "SoloRig-build-source-*" -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $newDirectories = @($after | Where-Object { $_ -notin $before })
    if ($newDirectories.Count -ne 0) {
        throw ("下载失败后残留临时源码目录: {0}" -f ($newDirectories -join ", "))
    }
}

function Read-Manifest {
    $manifestPath = Join-Path $buildRoot "SoloRigBuildManifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        throw ("缺少构建产物清单: {0}" -f $manifestPath)
    }
    return Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
}

if (-not (Test-Path -LiteralPath $buildScript)) {
    throw ("找不到构建脚本: {0}" -f $buildScript)
}
$requirementsExistedBefore = Test-Path -LiteralPath $requirementsPath
$branch = (& $git.Source -C $projectRoot branch --show-current).Trim()
if ([string]::IsNullOrWhiteSpace($branch)) {
    throw "自测需要当前仓库处于命名分支，以便验证显式 RepositoryRef。"
}

try {
    # 两次同源执行验证每次都清理并重新配置/编译，而不是复用旧 manifest。
    Invoke-BuildScript -Arguments @("-SkipTests")
    $first = Read-Manifest
    if ($first.sourceMode -ne "current-source" -or $first.testsExecuted) {
        throw "首次 -SkipTests manifest 的 sourceMode/testsExecuted 不符合预期。"
    }
    $firstRunId = [string]$first.buildRunId

    $logPath = Join-Path $buildRoot "build.log"
    if (-not (Test-Path -LiteralPath $logPath)) {
        throw "首次执行没有生成 build.log。"
    }
    $log = Get-Content -LiteralPath $logPath -Raw
    foreach ($marker in @("===== CMake configure =====", "===== CMake build =====", "===== .NET restore =====", "===== .NET Avalonia Release build =====")) {
        if ($log.IndexOf($marker, [StringComparison]::Ordinal) -lt 0) {
            throw ("build.log 缺少同步命令输出标记: {0}" -f $marker)
        }
    }

    Invoke-BuildScript -Arguments @("-SkipTests")
    $second = Read-Manifest
    if ($second.sourceMode -ne "current-source" -or $second.testsExecuted) {
        throw "第二次 -SkipTests manifest 的 sourceMode/testsExecuted 不符合预期。"
    }
    if ([string]$second.buildRunId -eq $firstRunId) {
        throw "重复执行没有生成新的 buildRunId，可能复用了旧构建状态。"
    }

    # 不启动第三次长编译，只验证缺少远程地址时在下载层安全拒绝，避免猜测或访问未知仓库。
    Assert-DownloadArgumentFailure
    Assert-CloneFailureCleanup
    if ($requirementsExistedBefore -and -not (Test-Path -LiteralPath $requirementsPath)) {
        throw "构建自测意外删除了用户原始需求文件。"
    }
    Write-Host "构建脚本自测通过：重复重建、同步日志、下载参数安全边界均已验证。" -ForegroundColor Green
}
finally {
    if (-not $KeepBuildOutput -and (Test-Path -LiteralPath $buildRoot)) {
        # 不删除 build；脚本本身的下一次执行会按安全规则清理它。保留最后产物便于复核。
        Write-Host ("保留最后一次构建产物供复核: {0}" -f $buildRoot) -ForegroundColor DarkGray
    }
}
