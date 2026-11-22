# CYSC 任务栏 0.2 —— 卸载脚本
#
# 用法：
#   双击「卸载.cmd」              正常卸载（有确认弹窗）
#   卸载.cmd -DryRun              只打印将要做什么，不真删
#   卸载.cmd -KeepData            只删程序，保留任务数据
#
# 做的事：
#   1. 结束正在运行的程序
#   2. 删开机自启项（HKCU\...\Run 下的 "CYSC TaskBar"）
#   3. 删设置（HKCU\Software\CYSC\CYSC TaskBar）
#   4. 删程序文件；（默认）删任务数据

[CmdletBinding()]
param(
    [switch]$DryRun,
    [switch]$KeepData,
    [switch]$Silent
)

$ErrorActionPreference = 'Continue'
$AppTitle = '卸载 CYSC 任务栏 0.2'
$InstallDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LogPath = Join-Path $env:TEMP 'cysc-uninstall.log'
$Deleted = 0

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $Message
    Write-Host $line
    try { Add-Content -Path $LogPath -Value $line -Encoding UTF8 } catch { }
}

function Remove-RegistryRunEntry {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    try {
        $value = (Get-ItemProperty -Path $key -Name 'CYSC TaskBar' -ErrorAction Stop).'CYSC TaskBar'
        if ($DryRun) { Write-Log "会删除开机自启：$value" }
        else {
            Remove-ItemProperty -Path $key -Name 'CYSC TaskBar' -ErrorAction Stop
            Write-Log '已删除开机自启项'
        }
    } catch {
        Write-Log '没有开机自启项（跳过）'
    }
}

function Remove-SettingsKey {
    $key = 'HKCU:\Software\CYSC\CYSC TaskBar'
    if (Test-Path $key) {
        if ($DryRun) { Write-Log "会删除设置项：$key" }
        else {
            Remove-Item -Path $key -Recurse -Force -ErrorAction SilentlyContinue
            Write-Log '已删除注册表设置项'
        }
    } else {
        Write-Log '没有注册表设置项（跳过）'
    }
}

function Remove-ProcessIfRunning($Name) {
    $procs = Get-Process -Name $Name -ErrorAction SilentlyContinue
    if (-not $procs) { return }
    foreach ($p in $procs) {
        if ($DryRun) { Write-Log "会结束进程：$Name ($($p.Id))" }
        else {
            try { $p.Kill(); Write-Log "已结束进程：$Name ($($p.Id))" } catch { }
        }
    }
    Start-Sleep -Milliseconds 400
}

function Remove-DataDir([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path)) { return }
    if ($DryRun) { Write-Log "会删除数据：$Path" }
    else {
        Remove-Item -Path $Path -Recurse -Force -ErrorAction SilentlyContinue
        Write-Log "已删除数据：$Label"
    }
}

# ---------------------------------------------------------------- 开始
Write-Log '==== 卸载开始 ===='
Write-Log "安装目录：$InstallDir"
if ($DryRun) { Write-Log '** DryRun：只打印，不真删 **' }

# 安全检查：目录里得真有程序文件
$markers = @('CYSC任务管理.exe', 'cysc-taskbar.exe') | Where-Object {
    Test-Path (Join-Path $InstallDir $_)
}
if (-not $markers) {
    $msg = "这个文件夹里没有找到 CYSC 任务栏的程序文件：`n$InstallDir`n`n为避免误删，卸载已取消。"
    if ($Silent) { Write-Log $msg } else { [void][System.Windows.Forms.MessageBox]::Show($msg, $AppTitle) }
    exit 1
}

if (-not $Silent) {
    Add-Type -AssemblyName System.Windows.Forms
    $r = [System.Windows.Forms.MessageBox]::Show(
        "确定要卸载「任务栏 0.2」吗？`n`n将删除程序文件夹：`n$InstallDir",
        $AppTitle, 'OKCancel', 'Question')
    if ($r -ne 'OK') { exit 0 }

    if (-not $KeepData) {
        $r2 = [System.Windows.Forms.MessageBox]::Show(
            "是否同时删除你的任务数据？`n`n「是」= 连数据一起删（不可恢复）`n「否」= 保留数据，只删程序`n`n数据位置：%APPDATA%\CYSC 与 程序目录\data",
            $AppTitle, 'YesNo', 'Warning')
        if ($r2 -eq 'No') { $KeepData = $true }
    }
}

# 1) 结束进程
Remove-ProcessIfRunning 'CYSC任务管理'
Remove-ProcessIfRunning 'cysc-taskbar'

# 2) 自启
Remove-RegistryRunEntry

# 3) 设置
Remove-SettingsKey

# 4) 数据
if ($KeepData) {
    Write-Log '按用户选择：保留任务数据'
} else {
    Remove-DataDir (Join-Path $env:APPDATA 'CYSC') '%APPDATA%\CYSC'
    Remove-DataDir (Join-Path $env:TEMP 'CYSC') '%TEMP%\CYSC'
}
Remove-DataDir (Join-Path $InstallDir 'data') '程序目录\data'

# 5) 程序文件
$self = $MyInvocation.MyCommand.Path
$items = Get-ChildItem -Path $InstallDir -Recurse -Force -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -ne $self -and $_.Name -ne '卸载.cmd' } |
         Sort-Object { $_.FullName.Length } -Descending
foreach ($item in $items) {
    if ($DryRun) { Write-Log "会删除：$($item.FullName)" }
    else {
        try {
            if ($item.PSIsContainer) { Remove-Item $item.FullName -Force -Recurse -ErrorAction Stop }
            else { Remove-Item $item.FullName -Force -ErrorAction Stop; $Deleted++ }
        } catch { }
    }
}
if (-not $DryRun) { Write-Log "共删除文件数：$Deleted" }

# 6) 目录与自身：交给一个延迟进程，等本脚本退出后再删
$cleanup = @"
Start-Sleep -Seconds 2
Remove-Item -LiteralPath '$InstallDir' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath '$self' -Force -ErrorAction SilentlyContinue
"@
if ($DryRun) {
    Write-Log "会删除目录：$InstallDir"
} else {
    Start-Process -FilePath 'powershell.exe' `
        -ArgumentList '-NoProfile', '-WindowStyle', 'Hidden', '-Command', $cleanup `
        -WindowStyle Hidden | Out-Null
}

$done = "卸载完成。`n`n"
if ($KeepData) { $done += '任务数据已保留（%APPDATA%\CYSC）。' } else { $done += '任务数据已删除。' }
$done += "`n日志：$LogPath"
if ($Silent) { Write-Log $done } else { [void][System.Windows.Forms.MessageBox]::Show($done, $AppTitle) }
