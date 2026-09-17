@echo off
rem ---------------------------------------------------------------
rem  neverlose.ui - one click build (MSVC x64 / NMake)
rem  requires Visual Studio Build Tools 2022+ with MSVC + CMake
rem ---------------------------------------------------------------
setlocal

set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [x] vcvars64.bat not found, please edit VSROOT in build.bat
    exit /b 1
)

set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"

set "IMGUI_DIR=%~dp0..\imgui"

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"

"%CMAKE%" -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DIMGUI_DIR="%IMGUI_DIR%"
if errorlevel 1 goto fail

"%CMAKE%" --build build
if errorlevel 1 goto fail

copy /y "build\NeverloseUI.exe" "NeverloseUI.exe" >nul
copy /y "build\NeverloseUI.dll" "NeverloseUI.dll" >nul || echo [!] NeverloseUI.dll 被占用（还有进程加载着它，先关掉目标进程再编译）
copy /y "build\NLBoneProbe.exe" "NLBoneProbe.exe" >nul
echo.
echo [ok] build finished: %~dp0NeverloseUI.exe / NeverloseUI.dll
exit /b 0

:fail
echo.
echo [x] build failed
exit /b 1
