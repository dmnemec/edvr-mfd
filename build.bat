@echo off
setlocal

echo [edvr-mfd] Building Standalone EDVR MFD Plugin...

where cl.exe >nul 2>&1
if %errorlevel% equ 0 goto compiler_found

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [edvr-mfd] ERROR: vswhere.exe not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSPATH=%%i"
)

if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [edvr-mfd] ERROR: vcvars64.bat not found at "%VSPATH%".
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:compiler_found
if not exist "build\plugins\edvr_mfd" mkdir "build\plugins\edvr_mfd"
if not exist "build\plugins\edvr_mfd\displays" mkdir "build\plugins\edvr_mfd\displays"

set CXXFLAGS=/std:c++17 /O2 /W4 /WX- /EHsc /MD /Iinclude /Isrc /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DEDVR_PLUGIN_EXPORTS
set LDFLAGS=/DLL /OUT:build\plugins\edvr_mfd\plugin.dll d3d11.lib dxgi.lib d3dcompiler.lib user32.lib shell32.lib ole32.lib winmm.lib

cl %CXXFLAGS% src\mfd_plugin.cpp src\mfd_manager.cpp src\mfd_renderer.cpp src\mfd_gaze_tracker.cpp src\mfd_input_router.cpp src\mfd_provider.cpp src\mfd_font.cpp /link %LDFLAGS%

if %errorlevel% neq 0 (
    echo [edvr-mfd] ERROR: Build failed.
    exit /b 1
)

echo [edvr-mfd] SUCCESS: build\plugins\edvr_mfd\plugin.dll created.
exit /b 0
