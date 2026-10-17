# 检查构建目录里的目标文件是否是「同一批」。
#
# 为什么需要这个：
#   本项目的 CMake 构建（NMake/JOM 与 Ninja）**没有把公共头文件的依赖记全**。
#   改了 mainwindow.h / Task.h 这类头文件后，增量构建可能只重编一部分 .cpp，
#   于是不同 .obj 里内联函数与类布局的定义来自不同版本的源码 —— ABI 不一致。
#   症状是随机的 0xC0000005 崩溃（读野指针/栈损坏），而且换一个构建目录就不重现，
#   极难排查。本项目已经因为这个问题被坑了三次。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/check_build_fresh.ps1 <构建目录>
# 退出码：0 = 一致；1 = 不一致（需要 clean 重建）

param([Parameter(Mandatory=$true)][string]$BuildDir)

if (-not (Test-Path $BuildDir)) {
    Write-Output "目录不存在: $BuildDir"
    exit 2
}

$objs = @(Get-ChildItem -Recurse $BuildDir -Filter '*.obj' -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -notmatch 'CompilerId|CMakeScratch' })

if ($objs.Count -eq 0) {
    Write-Output "没有找到 .obj（目录可能还没构建过）"
    exit 2
}

$groups = $objs | Group-Object { $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm') } |
          Sort-Object Name

Write-Output "目标文件 $($objs.Count) 个，按时间分 $($groups.Count) 批："
foreach ($g in $groups) {
    Write-Output ("  {0}  x{1}" -f $g.Name, $g.Count)
}

# 新老时间差得远（例如超过 30 分钟）就说明是分批编译的，很可能 ABI 不一致
$times = $objs | Sort-Object LastWriteTime
$span = ($times[-1].LastWriteTime - $times[0].LastWriteTime)

Write-Output ""
Write-Output ("最早 {0} / 最晚 {1}，跨度 {2:N1} 分钟" -f `
    $times[0].LastWriteTime.ToString('MM-dd HH:mm:ss'), `
    $times[-1].LastWriteTime.ToString('MM-dd HH:mm:ss'), $span.TotalMinutes)

if ($groups.Count -gt 1 -and $span.TotalMinutes -gt 30) {
    Write-Output ""
    Write-Output "!!! 目标文件不是同一批编译的，可能 ABI 不一致（会随机崩溃）"
    Write-Output "!!! 请做一次干净重建：删掉这个构建目录后重新配置 + 构建"
    exit 1
}

Write-Output ""
Write-Output "OK：目标文件时间集中，构建是一致的"
exit 0
