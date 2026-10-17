# ============================================================================
#  make_zip.ps1  --  把暂存目录打成便携版 zip
#
#  用法: make_zip.ps1 -SourceDir <暂存目录> -OutputZip <输出 zip>
#
#  为什么单独放一个 .ps1：在 .bat 里用 ^ 续行拼 PowerShell 命令，
#  %VAR% 展开后带空格就会被拆成多个参数，报出
#  "'py' is not recognized" 这种莫名其妙的错。分开写干净得多。
#
#  ⚠ 存盘必须是 UTF-8 带 BOM：Windows PowerShell 5.1 会把无 BOM 的 .ps1
#  当 ANSI(GBK) 读，中文全乱码然后语法报错。
# ============================================================================
param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$OutputZip
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
if (Test-Path -LiteralPath $OutputZip) { Remove-Item -LiteralPath $OutputZip -Force }

# includeBaseDirectory = true -> 解压出来是一个文件夹，不会散落一地
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $SourceDir,
    $OutputZip,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $true)

$mb = [math]::Round((Get-Item -LiteralPath $OutputZip).Length / 1MB, 2)
Write-Host "  便携版 zip 已生成: $OutputZip ($mb MB)"
