@echo off
rem ============================================================
rem  CYSC TaskBar - configure + build (MSVC + Ninja)
rem  Usage:  scripts\build.bat [Debug/Release] [configure/clean]
rem  Defaults: Debug, full build
rem
rem  NOTE: keep this file ASCII-only. cmd.exe reads .bat files using the
rem  console codepage, so UTF-8 comments without a BOM get mis-parsed.
rem ============================================================
setlocal

set "MODE=%~1"
if "%MODE%"=="" set "MODE=Debug"

set "ACTION=%~2"
if "%ACTION%"=="" set "ACTION=build"

set "PROJ=%~dp0.."
set "QTDIR=D:\Qt\6.11.0\msvc2022_64"
set "CMAKEEXE=D:\Qt\Tools\CMake_64\bin\cmake.exe"
set "NINJAEXE=D:\Qt\Tools\Ninja\ninja.exe"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "BUILDDIR=%PROJ%\build\ninja-%MODE%"

if not exist "%VCVARS%" (
    echo [ERROR] vcvars64.bat not found: "%VCVARS%"
    exit /b 1
)
if not exist "%CMAKEEXE%" (
    echo [ERROR] cmake not found: "%CMAKEEXE%"
    exit /b 1
)
if not exist "%NINJAEXE%" (
    echo [ERROR] ninja not found: "%NINJAEXE%"
    exit /b 1
)

rem --- enter the MSVC x64 environment ---
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] failed to initialise MSVC environment
    exit /b 1
)

rem Keep compiler diagnostics in UTF-8 instead of the console codepage.
set "CL=/utf-8"
set "VSLANG=1033"

set "PATH=%QTDIR%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QTDIR%\plugins"

if /i "%ACTION%"=="clean" (
    echo [INFO] removing "%BUILDDIR%"
    if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
    exit /b 0
)

echo [INFO] configuring %MODE% to "%BUILDDIR%"
"%CMAKEEXE%" -S "%PROJ%" -B "%BUILDDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%MODE% -DCMAKE_PREFIX_PATH="%QTDIR%" -DCMAKE_MAKE_PROGRAM="%NINJAEXE%" -DCYSC_BUILD_TESTS=ON
if errorlevel 1 (
    echo [ERROR] cmake configure failed
    exit /b 1
)

if /i "%ACTION%"=="configure" exit /b 0

rem A previously running instance locks the exe, which makes the link fail and
rem leaves the old binary in place (tests would then silently run stale code).
taskkill /f /im cysc-taskbar.exe >nul 2>&1
taskkill /f /im cysc_tests.exe >nul 2>&1

echo [INFO] building
"%CMAKEEXE%" --build "%BUILDDIR%" --parallel
if errorlevel 1 (
    echo [ERROR] build failed
    exit /b 1
)

echo [INFO] OK - "%BUILDDIR%\cysc-taskbar.exe"
endlocal
