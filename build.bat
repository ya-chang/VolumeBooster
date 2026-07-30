@echo off
setlocal

echo ========================================
echo   VolumeBooster Build Script
echo ========================================
echo.

REM 检查 Visual Studio
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] 未找到 Visual Studio 编译器。
    echo 请在 Visual Studio Developer Command Prompt 中运行此脚本。
    echo 或运行: "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    pause
    exit /b 1
)

REM 检查 .NET SDK
where dotnet >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] 未找到 .NET SDK。请安装 .NET 8 SDK。
    pause
    exit /b 1
)

REM 创建构建目录
if not exist build mkdir build
cd build

echo.
echo [1/4] 构建 APO + DeviceListener (C++)...
cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo [ERROR] CMake 配置失败
    pause
    exit /b 1
)

cmake --build . --config Release
if %errorlevel% neq 0 (
    echo [ERROR] C++ 构建失败
    pause
    exit /b 1
)
echo [OK] APO + DeviceListener 构建完成

echo.
echo [2/4] 构建 GUI (C#/WPF)...
cd ..\src\GUI
dotnet publish -c Release -r win-x64 --self-contained false -o ..\..\build\bin
if %errorlevel% neq 0 (
    echo [ERROR] GUI 构建失败
    pause
    exit /b 1
)
echo [OK] GUI 构建完成

cd ..\..

echo.
echo [3/4] 检查构建产物...
if not exist build\bin\VolumeBoosterAPO.dll (
    echo [ERROR] VolumeBoosterAPO.dll 未找到
    pause
    exit /b 1
)
if not exist build\bin\VolumeBooster.exe (
    echo [ERROR] VolumeBooster.exe 未找到
    pause
    exit /b 1
)
if not exist build\bin\DeviceListener.exe (
    echo [ERROR] DeviceListener.exe 未找到
    pause
    exit /b 1
)
echo [OK] 所有构建产物就绪

echo.
echo [4/4] 构建安装程序...
where makensis >nul 2>&1
if %errorlevel% equ 0 (
    makensis /V2 src\Installer\installer.nsi
    if %errorlevel% equ 0 (
        echo [OK] 安装程序构建完成: VolumeBooster-Setup.exe
    ) else (
        echo [WARN] 安装程序构建失败（NSIS 未安装或脚本有误）
    )
) else (
    echo [WARN] 未找到 NSIS (makensis)，跳过安装程序构建。
    echo         请安装 NSIS: https://nsis.sourceforge.io/Download
)

echo.
echo ========================================
echo   构建完成!
echo ========================================
echo.
echo 产物位置:
echo   APO DLL:        build\bin\VolumeBoosterAPO.dll
echo   GUI:            build\bin\VolumeBooster.exe
echo   Device Listener: build\bin\DeviceListener.exe
echo   安装程序:       VolumeBooster-Setup.exe (如已构建)
echo.
echo 安装方法:
echo   1. 以管理员身份运行 VolumeBooster-Setup.exe
echo   2. 或手动: 以管理员身份运行 DeviceListener.exe --console
echo      然后运行 VolumeBooster.exe
echo.

pause
