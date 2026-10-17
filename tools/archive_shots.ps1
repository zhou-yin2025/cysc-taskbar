# 抓取 CYSC 任务管理的各屏截图，存到 docs/shots/
# 用法：powershell -File tools/archive_shots.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dest = Join-Path $root 'build\ninja-Debug'
$shot = Join-Path $root 'tools\shot.ps1'
$outDir = Join-Path $root 'docs\shots'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# 用 build 目录里的 exe（已带 data/cysc.db 示例数据）
$exe = Join-Path $dest 'cysc-taskbar.exe'
$db = Join-Path $dest 'data\cysc.db'
$seed = Join-Path $root 'tools\sample\cysc.db'
if (Test-Path $seed) {
    Copy-Item $seed $db -Force
}

function Stop-App {
  Get-Process -Name 'cysc-taskbar' -ErrorAction SilentlyContinue | ForEach-Object {
    try { $_.Kill() } catch { }
  }
  Start-Sleep -Milliseconds 900
}

function Launch([string[]]$extra) {
  $a = ($extra -join ' ')
  $bat = Join-Path $env:TEMP 'cysc_shot_launch.bat'
  $line = "@echo off`r`nset `"PATH=D:\Qt\6.11.0\msvc2022_64\bin;%PATH%`"`r`n" +
          "set `"QT_PLUGIN_PATH=D:\Qt\6.11.0\msvc2022_64\plugins`"`r`n" +
          "start `"`" `"$exe`" $a`r`n"
  [System.IO.File]::WriteAllText($bat, $line, (New-Object System.Text.UTF8Encoding($true)))
  cmd /c "`"$bat`""
  Start-Sleep -Seconds 7
}

# ---- 1. 主界面（展开第一条任务）----
Stop-App
Launch @('--expand-first')
& powershell -NoProfile -ExecutionPolicy Bypass -File $shot 'cysc-taskbar' 'CYSC 任务栏' (Join-Path $outDir '01_main.png')

# ---- 2..5. 可视化面板四个页签 ----
$tabs = @(
  @{ i = 0; name = '02_viz_hash.png' },
  @{ i = 1; name = '03_viz_trie.png' },
  @{ i = 2; name = '04_viz_segment.png' },
  @{ i = 3; name = '05_viz_unionfind.png' }
)
foreach ($t in $tabs) {
  Stop-App
  Launch @("--visualize-tab", "$($t.i)")
  # 面板放到 x=700（主窗口在 20），两个窗口错开，避免边缘混入对方像素
  & powershell -NoProfile -ExecutionPolicy Bypass -File $shot 'cysc-taskbar' '可视化' (Join-Path $outDir $t.name) 700 20
}

Stop-App
Get-ChildItem $outDir | Select-Object Name, Length | Format-Table -AutoSize
