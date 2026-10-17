@echo off
REM ===========================================================================
REM  package.bat  --  build the Windows installer
REM
REM  Output: build\package\  (installer .exe + portable .zip)
REM
REM  NOTE: this file is deliberately ASCII-only. cmd.exe reads .bat in the
REM  console code page, so non-ASCII characters get mis-parsed and produce
REM  errors like "'se' is not recognized".
REM
REM  Requires Inno Setup 6 (ISCC.exe). Install with:
REM    winget install JRSoftware.InnoSetup
REM  It needs github.com reachable (the installer is published there).
REM ===========================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build\ninja-Release"
set "PKG=%ROOT%\build\package"
set "STAGE=%PKG%\CYSC"
set "QT=D:\Qt\6.11.0\msvc2022_64"
set "WINDEPLOYQT=%QT%\bin\windeployqt.exe"
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
set "VCREDIST=%ROOT%\resources\redist\vc_redist.x64.exe"

echo [1/7] Check prerequisites
if not exist "%ISCC%" (
    echo   Not found: "%ISCC%"
    echo   Install with: winget install JRSoftware.InnoSetup
    exit /b 1
)
if not exist "%WINDEPLOYQT%" (
    echo   Not found: %WINDEPLOYQT%
    exit /b 1
)

echo [2/7] Locate the release executable
REM The CMake target is cysc-taskbar, so the artifact is cysc-taskbar.exe.
REM It is deliberately NOT renamed to Chinese in CMake (jom cannot handle a
REM non-ASCII output name on a Chinese code page). The rename happens below.
set "APPEXE=%BUILD%\cysc-taskbar.exe"
if not exist "%APPEXE%" (
    REM fall back to any non-test exe, in case the target gets renamed later
    set "APPEXE="
    for %%F in ("%BUILD%\*.exe") do (
        if /i not "%%~nxF"=="cysc_tests.exe" set "APPEXE=%%F"
    )
)
if not defined APPEXE (
    echo   No executable in "%BUILD%"
    echo   Run first: scripts\build.bat Release
    exit /b 1
)
if not exist "%APPEXE%" (
    echo   Not found: "%APPEXE%"
    echo   Run first: scripts\build.bat Release
    exit /b 1
)
echo   found: %APPEXE%

echo [3/7] Clean staging dir
if exist "%PKG%" rmdir /s /q "%PKG%"
mkdir "%STAGE%" || exit /b 1

echo [4/7] Copy executable and collect Qt dependencies
REM CMake keeps the build artifact ASCII-named, because jom (used by Qt Creator)
REM cannot handle a non-ASCII output name on a Chinese code page. Rename it to
REM the user-facing name here, in the staging dir only.
set "APPNAME=CYSC任务管理.exe"
copy /y "%APPEXE%" "%STAGE%\%APPNAME%" >nul || exit /b 1
REM This Qt build has no imageformats plugins, so windeployqt prints warnings.
"%WINDEPLOYQT%" --release --no-translations --no-opengl-sw --dir "%STAGE%" "%STAGE%\%APPNAME%" >nul 2>&1

echo [5/7] Stage the VC++ runtime and icon
if exist "%VCREDIST%" (
    copy /y "%VCREDIST%" "%PKG%\vc_redist.x64.exe" >nul
    echo   vc_redist.x64.exe staged
) else (
    echo   ERROR: "%VCREDIST%" missing
    echo   Download: https://aka.ms/vs/17/release/vc_redist.x64.exe
    echo   Save it to resources\redist\vc_redist.x64.exe
    exit /b 1
)
copy /y "%ROOT%\resources\app.ico" "%STAGE%\app.ico" >nul
copy /y "%ROOT%\resources\app.ico" "%ROOT%\scripts\app.ico" >nul

echo [6/7] Compile the installer (Inno Setup)
"%ISCC%" /Q "%ROOT%\scripts\installer.iss"
if errorlevel 1 (
    echo   ISCC failed
    exit /b 1
)

echo [7/7] Also emit a portable zip
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\make_zip.ps1" -SourceDir "%STAGE%" -OutputZip "%PKG%\CYSC-portable.zip"

echo.
echo Done. Artifacts in %PKG%:
dir /b "%PKG%"
endlocal
