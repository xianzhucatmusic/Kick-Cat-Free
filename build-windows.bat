@echo off
chcp 65001 >nul
setlocal

echo ========================================
echo  Kick Cat Free - Windows Build
echo ========================================
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo [错误] 找不到 cmake，请安装并加入 PATH
    echo        https://cmake.org/download/
    pause
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [错误] 未检测到 MSVC 编译环境
    echo.
    echo 请用「x64 Native Tools Command Prompt for VS 2022」运行本脚本
    echo 或先执行 vcvars64.bat
    pause
    exit /b 1
)

echo [1/3] 配置 CMake（首次会下载 JUCE + libsodium）...
cmake -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [错误] 配置失败
    pause
    exit /b 1
)

echo.
echo [2/3] 编译 VST3...
cmake --build build-win --config Release --target KickForge_VST3 --parallel
if errorlevel 1 (
    echo [错误] 编译失败
    pause
    exit /b 1
)

echo.
echo [3/3] 检查产物...
set VST3=build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3
if exist "%VST3%" (
    echo.
    echo ========================================
    echo  编译成功
    echo ========================================
    echo.
    echo  产物: %VST3%
    echo.
    echo  未压缩体积:
    dir /s /-c "%VST3%" ^| findstr "个文件"
    echo.
    echo  安装（管理员权限）:
    echo    xcopy /E /I "%VST3%" "C:\Program Files\Common Files\VST3\Kick Cat Free.vst3"
    echo.
    echo  然后重启 DAW。
) else (
    echo [错误] 找不到 %VST3%
)
echo.
pause
