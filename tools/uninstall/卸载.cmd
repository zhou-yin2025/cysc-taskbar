@echo off
rem CYSC TaskBar 0.2 - uninstaller launcher
rem Double-click to uninstall. Options:  -DryRun   -KeepData
setlocal
set "PS=powershell.exe"
if not exist "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" set "PS=pwsh.exe"
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %*
if errorlevel 1 pause
endlocal