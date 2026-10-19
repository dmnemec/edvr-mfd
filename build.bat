@echo off
setlocal
rem All paths below are relative to the repo root, wherever the script is launched from.
cd /d "%~dp0"

echo [edvr-mfd] Running tool self-tests...
python tools\install_mfd.py --self-test
if %errorlevel% neq 0 (
    echo [edvr-mfd] ERROR: Python self-tests failed.
    exit /b 1
)

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

rem vcvars64 calls vswhere by name; put its folder on PATH so it doesn't print "not recognized".
for %%d in ("%VSWHERE%") do set "PATH=%PATH%;%%~dpd"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:compiler_found
if not exist "build\plugins\edvr_mfd" mkdir "build\plugins\edvr_mfd"
if not exist "build\plugins\edvr_mfd\displays" mkdir "build\plugins\edvr_mfd\displays"
if exist "build\plugins\edvr_mfd\mfds" rmdir /s /q "build\plugins\edvr_mfd\mfds"
mkdir "build\plugins\edvr_mfd\mfds"
xcopy /E /I /Q /Y "packages" "build\plugins\edvr_mfd\mfds" >nul 2>&1
if not exist "build\obj" mkdir "build\obj"
if not exist "build\obj_test" mkdir "build\obj_test"

set CXXFLAGS=/std:c++17 /O2 /Z7 /W4 /WX- /EHsc /MT /Fo:build\obj\ /Iinclude /Isrc /Ithird_party /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DEDVR_PLUGIN_EXPORTS
set LDFLAGS=/DLL /DEBUG /MAP:build\plugins\edvr_mfd\plugin.map /OUT:build\plugins\edvr_mfd\plugin.dll /IMPLIB:build\plugins\edvr_mfd\plugin.lib d3d11.lib dxgi.lib d3dcompiler.lib user32.lib shell32.lib ole32.lib winmm.lib

cl %CXXFLAGS% src\mfd_plugin.cpp src\mfd_manager.cpp src\mfd_renderer.cpp src\mfd_gaze_tracker.cpp src\mfd_input_router.cpp src\mfd_provider.cpp src\mfd_font.cpp src\mfd_display_schema.cpp src\mfd_package.cpp src\mfd_content_watcher.cpp src\mfd_context.cpp src\mfd_event_log.cpp src\mfd_companion.cpp /link %LDFLAGS%

if %errorlevel% neq 0 (
    echo [edvr-mfd] ERROR: Build failed.
    exit /b 1
)

echo [edvr-mfd] SUCCESS: build\plugins\edvr_mfd\plugin.dll created.

echo [edvr-mfd] Building and running unit tests...
set TEST_CXXFLAGS=/std:c++17 /O2 /EHsc /MT /Fo:build\obj_test\ /Iinclude /Isrc /Ithird_party /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DEDVR_PLUGIN_EXPORTS
set TEST_LDFLAGS=d3d11.lib dxgi.lib d3dcompiler.lib user32.lib shell32.lib ole32.lib winmm.lib
cl %TEST_CXXFLAGS% tools\mfd_test\mfd_test.cpp src\mfd_plugin.cpp src\mfd_manager.cpp src\mfd_renderer.cpp src\mfd_gaze_tracker.cpp src\mfd_input_router.cpp src\mfd_provider.cpp src\mfd_font.cpp src\mfd_display_schema.cpp src\mfd_package.cpp src\mfd_content_watcher.cpp src\mfd_context.cpp src\mfd_event_log.cpp src\mfd_companion.cpp /link %TEST_LDFLAGS% /OUT:build\mfd_test.exe
if %errorlevel% neq 0 (
    echo [edvr-mfd] ERROR: Test compilation failed.
    exit /b 1
)

build\mfd_test.exe
if %errorlevel% neq 0 (
    echo [edvr-mfd] ERROR: Unit tests failed.
    exit /b 1
)

rem Go toolchain: EDVR_GO (path to go.exe) if set, else Windows Go on PATH, else Go inside WSL.
rem WSL's login shell PATH is used, plus the default /usr/local/go/bin install location.
set "WSL_GO_PATH=export PATH=$PATH:/usr/local/go/bin"
rem Detection uses goto, not if/else: "exit /b 1" inside an if/else block returns 0 to the caller.
set "GO_MODE="
if not defined EDVR_GO goto go_detect
if not exist "%EDVR_GO%" (
    echo [edvr-mfd] ERROR: EDVR_GO is set to "%EDVR_GO%", which does not exist.
    exit /b 1
)
set "GO_MODE=native"
set "GO_EXE=%EDVR_GO%"
goto go_found
:go_detect
where go >nul 2>&1 && set "GO_MODE=native" && set "GO_EXE=go"
if defined GO_MODE goto go_found
where wsl >nul 2>&1 && wsl --exec bash -lc "%WSL_GO_PATH%; command -v go >/dev/null" && set "GO_MODE=wsl"
:go_found
if not defined GO_MODE (
    echo [edvr-mfd] ERROR: Go not found. Install Go 1.22+ for Windows ^(https://go.dev/dl/^) or inside WSL, or set EDVR_GO to go.exe.
    exit /b 1
)
echo [edvr-mfd] Using Go via %GO_MODE%.

echo [edvr-mfd] Running companion Go unit tests...
set "GO_FAIL="
if "%GO_MODE%"=="native" (
    pushd companion
    "%GO_EXE%" test ./... || set "GO_FAIL=1"
    popd
) else (
    wsl --cd "%~dp0companion" --exec bash -lc "%WSL_GO_PATH%; go test ./..." || set "GO_FAIL=1"
)
if defined GO_FAIL (
    echo [edvr-mfd] ERROR: Companion Go unit tests failed.
    exit /b 1
)

echo [edvr-mfd] Building native install_mfd.exe...
if not exist "build\tools" mkdir "build\tools"
set "GO_FAIL="
if "%GO_MODE%"=="native" (
    pushd companion
    set "GOOS=windows"
    set "GOARCH=amd64"
    "%GO_EXE%" build -trimpath -ldflags="-s -w" -o ..\build\tools\install_mfd.exe .\cmd\installer || set "GO_FAIL=1"
    popd
) else (
    wsl --cd "%~dp0companion" --exec bash -lc "%WSL_GO_PATH%; GOOS=windows GOARCH=amd64 go build -trimpath -ldflags='-s -w' -o ../build/tools/install_mfd.exe ./cmd/installer" || set "GO_FAIL=1"
)
if defined GO_FAIL (
    echo [edvr-mfd] ERROR: Failed to build native install_mfd.exe
    exit /b 1
)

if "%1"=="--install" (
    echo [edvr-mfd] Installing to game directory...
    python tools\install_mfd.py
)

exit /b 0
