@echo off
rem ============================================================
rem  CYSC TaskBar - build, then run the CTest suites
rem  Usage:  scripts\test.bat [Debug/Release]
rem
rem  NOTE: keep this file ASCII-only. cmd.exe reads .bat files using the
rem  console codepage, so UTF-8 comments without a BOM get mis-parsed.
rem ============================================================
setlocal

set "MODE=%~1"
if "%MODE%"=="" set "MODE=Debug"

set "PROJ=%~dp0.."
set "BUILDDIR=%PROJ%\build\ninja-%MODE%"
set "QTDIR=D:\Qt\6.11.0\msvc2022_64"
set "CTESTEXE=D:\Qt\Tools\CMake_64\bin\ctest.exe"

if not exist "%CTESTEXE%" (
    echo [ERROR] ctest not found: "%CTESTEXE%"
    exit /b 1
)

rem Always build first: keeps the binaries current and regenerates the ctest
rem manifest, so newly added test cases are actually picked up.
echo [INFO] building before testing
call "%~dp0build.bat" %MODE%
if errorlevel 1 exit /b 1

rem Drop leftover processes: a locked test binary would run stale code.
taskkill /f /im cysc-taskbar.exe >nul 2>&1
taskkill /f /im cysc_tests.exe >nul 2>&1

set "PATH=%QTDIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QTDIR%\plugins"

echo [INFO] running ctest in "%BUILDDIR%"
"%CTESTEXE%" --test-dir "%BUILDDIR%" --output-on-failure
if errorlevel 1 (
    echo [ERROR] tests failed
    exit /b 1
)

echo [INFO] all tests passed
endlocal
