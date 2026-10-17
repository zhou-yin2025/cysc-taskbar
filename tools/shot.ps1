Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$sig = @'
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
[DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
'@
Add-Type -MemberDefinition $sig -Name W -Namespace Z | Out-Null

$procName = $args[0]
$titleKey = $args[1]
$outFile  = $args[2]

# 找目标窗口：先取主窗口，再按标题关键字匹配
# （可视化面板是独立顶层窗口，不在 MainWindowHandle 上）
$cands = @(Get-Process -Name $procName -ErrorAction SilentlyContinue |
           Where-Object { $_.MainWindowHandle -ne 0 })
if ($cands.Count -eq 0) { Write-Output "进程 '$procName' 没有可见窗口"; exit 1 }
$proc = $cands[0]
$targetPid = [uint32]$proc.Id
$script:hwnd = [int64]$proc.MainWindowHandle

if ($titleKey) {
  $script:found = 0
  $cb = [Z.W+EnumWindowsProc]{
    param($h, $p)
    $q = [uint32]0
    [Z.W]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
    if ($q -eq $targetPid -and [Z.W]::IsWindowVisible($h)) {
      $len = [Z.W]::GetWindowTextLength($h)
      if ($len -gt 0) {
        $sb = New-Object System.Text.StringBuilder ($len + 2)
        [Z.W]::GetWindowText($h, $sb, $sb.Capacity) | Out-Null
        if ($sb.ToString() -like "*$titleKey*") { $script:found = [int64]$h }
      }
    }
    return $true
  }
  [Z.W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  if ($script:found -ne 0) { $script:hwnd = $script:found }
}
if ($script:hwnd -eq 0) { Write-Output "没找到标题含 '$titleKey' 的窗口"; exit 1 }

# 只提到前台并等它画完，**不要动窗口几何**。
# 教训：Qt 无边框窗口的尺寸由内部布局决定，外部 MoveWindow 改尺寸会和布局
# 打架，抓出来的图会缺一大块（踩过）。也不要藏控制台窗口，那会影响屏幕坐标。
$h = [IntPtr]$script:hwnd
[Z.W]::ShowWindow($h, 5) | Out-Null
[Z.W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 2600

$r = New-Object Z.W+RECT
[Z.W]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
if ($w -le 0 -or $hh -le 0) { Write-Output "窗口尺寸异常"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$g.Dispose()
$bmp.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $outFile ($w x $hh)"
