# 真实鼠标输入冒烟测试：用 Win32 mouse_event 真的去点界面各处，任一步进程消失即报闪退。
#
# 为什么需要这一层：
#   cysc_tests / --guitest 都是直接调用槽函数或把 QMouseEvent 投递给列表视口，
#   走不到「Qt 正在派发鼠标事件给某个列表项」这个时序。而 V0.1.1 修的闪退
#   恰恰就发生在这个时序里：事件派发途中 clear() 列表，把正在派发的 item 删了。
#   所以必须用真实输入做一层验证。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/click_smoke.ps1 <exe路径>
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$sig = @'
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, System.IntPtr e);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
public struct POINT { public int X; public int Y; }
'@
Add-Type -MemberDefinition $sig -Name M -Namespace Smoke -ErrorAction SilentlyContinue | Out-Null

$exe = $args[0]
if (-not $exe -or -not (Test-Path $exe)) { Write-Output "用法: click_smoke.ps1 <exe路径>"; exit 2 }

$fail = New-Object System.Collections.ArrayList
function Alive([string]$what) {
  $p = @(Get-Process -Name 'cysc-taskbar' -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited })
  if ($p.Count -eq 0) { Write-Output "  [CRASH] $what"; [void]$fail.Add($what); return $false }
  return $true
}
function Click([int]$x, [int]$y, [int]$n = 1) {
  [Smoke.M]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 150
  for ($i = 0; $i -lt $n; $i++) {
    [Smoke.M]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 35
    [Smoke.M]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    if ($i -lt $n - 1) { Start-Sleep -Milliseconds 50 }
  }
  Start-Sleep -Milliseconds 550
}

Get-Process -Name 'cysc-taskbar' -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch { } }
Start-Sleep -Milliseconds 900

$bat = Join-Path $env:TEMP 'click_smoke_run.bat'
$lines = @(
  '@echo off',
  'set "PATH=D:\Qt\6.11.0\msvc2022_64\bin;%PATH%"',
  'set "QT_PLUGIN_PATH=D:\Qt\6.11.0\msvc2022_64\plugins"',
  "start `"`" `"$exe`""
) -join "`r`n"
[System.IO.File]::WriteAllText($bat, $lines + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
cmd /c "`"$bat`""
Start-Sleep -Seconds 7

$p = @(Get-Process -Name 'cysc-taskbar' -ErrorAction SilentlyContinue)
if ($p.Count -eq 0) { Write-Output '启动失败'; exit 1 }
$hwnd = $p[0].MainWindowHandle
Write-Output "pid=$($p[0].Id)"

[Smoke.M]::ShowWindow($hwnd, 5) | Out-Null
[Smoke.M]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 1500

$cr = New-Object Smoke.M+RECT
[Smoke.M]::GetClientRect($hwnd, [ref]$cr) | Out-Null
$org = New-Object Smoke.M+POINT
$org.X = 0; $org.Y = 0
[Smoke.M]::ClientToScreen($hwnd, [ref]$org) | Out-Null
$ox = $org.X; $oy = $org.Y
$w = $cr.Right - $cr.Left; $h = $cr.Bottom - $cr.Top
Write-Output "client ${w}x${h} at ($ox,$oy)"

Write-Output '[1] 侧栏智能视图'
foreach ($y in 100, 124, 148, 172, 196) {
  Click ($ox + 50) ($oy + $y) 1
  if (-not (Alive "sideview y=$y")) { break }
}
Write-Output '[2] 侧栏列表项'
foreach ($y in 293, 317, 341, 365, 389) {
  Click ($ox + 50) ($oy + $y) 1
  if (-not (Alive "listitem y=$y")) { break }
}
Write-Output '[3] 单击任务行（展开详情）'
Click ($ox + 50) ($oy + 172) 1 | Out-Null
foreach ($y in 150, 210, 270) {
  Click ($ox + 400) ($oy + $y) 1
  if (-not (Alive "row y=$y")) { break }
}
Write-Output '[4] 双击任务行（切换状态）'
foreach ($y in 150, 210) {
  Click ($ox + 400) ($oy + $y) 2
  if (-not (Alive "dblrow y=$y")) { break }
}
Write-Output '[5] 详情区控件'
foreach ($y in 250, 300, 330, 400, 440, 480, 520, 560) {
  Click ($ox + 120) ($oy + $y) 1
  if (-not (Alive "detail y=$y")) { break }
}
Write-Output '[6] 分类筛选'
foreach ($x in 290, 330, 370, 410, 450) {
  Click ($ox + $x) ($oy + 144) 1
  if (-not (Alive "cat x=$x")) { break }
}
Write-Output '[7] 侧栏 + 与 分组按钮'
foreach ($x in 118, 137) {
  Click ($ox + $x) ($oy + 271) 1
  Start-Sleep -Milliseconds 600
  [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
  Start-Sleep -Milliseconds 700
  if (-not (Alive "groupbtn x=$x")) { break }
}
Write-Output '[8] 拖右下角调整大小'
$sx = $ox + $w - 3; $sy = $oy + $h - 3
[Smoke.M]::SetCursorPos($sx, $sy) | Out-Null
Start-Sleep -Milliseconds 300
[Smoke.M]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
for ($i = 1; $i -le 6; $i++) {
  [Smoke.M]::SetCursorPos($sx + $i * 15, $sy + $i * 8) | Out-Null
  Start-Sleep -Milliseconds 60
}
[Smoke.M]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
Alive '拖拽调整大小' | Out-Null

Get-Process -Name 'cysc-taskbar' -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch { } }

Write-Output ''
if ($fail.Count -eq 0) { Write-Output '=== 真实输入冒烟：未出现闪退 ==='; exit 0 }
Write-Output "=== 真实输入冒烟：$($fail.Count) 处闪退 ==="
$fail | ForEach-Object { Write-Output "   - $_" }
exit 1
