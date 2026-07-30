; VolumeBooster NSIS 安装脚本
; 编译方法: makensis installer.nsi

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; ========== 基本信息 ==========

Name "系统音量增强器"
OutFile "VolumeBooster-Setup.exe"
InstallDir "$PROGRAMFILES\VolumeBooster"
InstallDirRegKey HKLM "Software\VolumeBooster" "InstallDir"
RequestExecutionLevel admin
Unicode True

; ========== 版本信息 ==========

VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "VolumeBooster"
VIAddVersionKey "FileDescription" "系统音量增强器"
VIAddVersionKey "FileVersion" "1.0.0.0"
VIAddVersionKey "ProductVersion" "1.0.0.0"
VIAddVersionKey "LegalCopyright" "2026"

; ========== 界面设置 ==========

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; ========== 安装页面 ==========

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; ========== 卸载页面 ==========

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; ========== 语言 ==========

!insertmacro MUI_LANGUAGE "SimpChinese"

; ========== 安装区段 ==========

Section "安装" SecInstall
    SetOutPath "$INSTDIR"
    
    ; 检查是否已安装 Equalizer APO
    ReadRegStr $0 HKLM "SOFTWARE\EqualizerAPO" "InstallDir"
    ${If} $0 != ""
        MessageBox MB_YESNO|MB_ICONQUESTION "检测到已安装 Equalizer APO，可能会产生冲突。是否继续安装？" IDYES ContinueInstall
        Abort
        ContinueInstall:
    ${EndIf}
    
    ; 复制文件
    File "build\bin\VolumeBoosterAPO.dll"
    File "build\bin\VolumeBooster.exe"
    File "build\bin\DeviceListener.exe"
    File "LICENSE"
    
    ; 写入注册表（安装路径）
    WriteRegStr HKLM "Software\VolumeBooster" "InstallDir" "$INSTDIR"
    
    ; 注册 APO（写入 Windows 音频处理对象注册表）
    ; 注意：实际的 APO 注册需要更复杂的注册表操作
    ; 这里写入基础键，具体注册逻辑在 DeviceListener 中完成
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\AudioProcessingObjects\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}" "" "VolumeBooster APO"
    WriteRegStr HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\AudioProcessingObjects\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}" "APODll" "$INSTDIR\VolumeBoosterAPO.dll"
    WriteRegDWORD HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\AudioProcessingObjects\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}" "APOFlags" 0x00000001
    
    ; 安装设备监听服务
    ExecWait 'sc create VolumeBoosterListener binPath= "$INSTDIR\DeviceListener.exe"' $0
    ExecWait 'sc start VolumeBoosterListener' $0
    
    ; 重启音频服务
    DetailPrint "正在重启音频服务..."
    ExecWait 'net stop Audiosrv' $0
    ExecWait 'net start Audiosrv' $0
    
    ${If} $0 != 0
        MessageBox MB_YESNO|MB_ICONQUESTION "音频服务重启失败，是否重启电脑？" IDYES Reboot
        Goto SkipReboot
        Reboot:
        Reboot
        SkipReboot:
    ${EndIf}
    
    ; 创建卸载程序
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; 写入卸载信息
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VolumeBooster" "DisplayName" "系统音量增强器"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VolumeBooster" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VolumeBooster" "DisplayIcon" "$INSTDIR\VolumeBooster.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VolumeBooster" "Publisher" "VolumeBooster"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VolumeBooster" "DisplayVersion" "1.0.0"
    
    ; 启动 GUI
    Exec '"$INSTDIR\VolumeBooster.exe"'
    
SectionEnd

; ========== 卸载区段 ==========

Section "Uninstall"
    
    ; 停止 GUI 进程
    ExecWait 'taskkill /f /im VolumeBooster.exe' $0
    
    ; 停止并删除服务
    ExecWait 'sc stop VolumeBoosterListener' $0
    ExecWait 'sc delete VolumeBoosterListener' $0
    
    ; 注销 APO
    DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Audio\AudioProcessingObjects\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}"
    
    ; 重启音频服务
    DetailPrint "正在重启音频服务..."
    ExecWait 'net stop Audiosrv' $0
    ExecWait 'net start Audiosrv' $0
    
    ; 删除文件
    Delete "$INSTDIR\VolumeBoosterAPO.dll"
    Delete "$INSTDIR\VolumeBooster.exe"
    Delete "$INSTDIR\DeviceListener.exe"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    
    ; 删除注册表
    DeleteRegKey HKLM "Software\VolumeBooster"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VolumeBooster"
    
    ; 删除开机自启
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "VolumeBooster"
    
SectionEnd
