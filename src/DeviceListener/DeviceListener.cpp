#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <strsafe.h>
#include <stdio.h>

// ========== 设备变更监听器 ==========

class DeviceChangeListener : public IMMNotificationClient {
public:
    DeviceChangeListener() : m_ref(1) {}
    ~DeviceChangeListener() {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) delete this;
        return ref;
    }

    // IMMNotificationClient - 设备状态变更
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override {
        LogEvent(L"设备状态变更: %s -> %lu", pwstrDeviceId, dwNewState);
        
        if (dwNewState == DEVICE_STATE_ACTIVE) {
            // 设备激活，检查 APO 注册
            EnsureAPORegistered();
        }
        return S_OK;
    }

    // 设备添加
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override {
        LogEvent(L"设备添加: %s", pwstrDeviceId);
        EnsureAPORegistered();
        
        // 检测是否为蓝牙设备
        if (IsBluetoothDevice(pwstrDeviceId)) {
            LogEvent(L"检测到蓝牙设备，音画同步可能受蓝牙延迟影响（100-300ms）");
            // 通知 GUI（通过注册表标记）
            WriteNotification(L"蓝牙设备已连接，音画同步可能受影响");
        }
        return S_OK;
    }

    // 设备移除
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
        LogEvent(L"设备移除: %s", pwstrDeviceId);
        return S_OK;
    }

    // 默认设备变更
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) override {
        if (flow == eRender && role == eConsole) {
            LogEvent(L"默认输出设备变更: %s", pwstrDeviceId ? pwstrDeviceId : L"(无)");
            EnsureAPORegistered();
        }
        return S_OK;
    }

    // 设备友好名变更
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override {
        return S_OK;
    }

private:
    LONG m_ref;

    // 确保 APO 已注册
    void EnsureAPORegistered() {
        HKEY hKey;
        const wchar_t* apoPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio\\AudioProcessingObjects\\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}";
        
        LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, apoPath, 0, KEY_READ, &hKey);
        if (result != ERROR_SUCCESS) {
            LogEvent(L"APO 未注册，正在重新注册...");
            RegisterAPO();
        } else {
            RegCloseKey(hKey);
        }
    }

    // 注册 APO
    void RegisterAPO() {
        HKEY hKey;
        const wchar_t* apoPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio\\AudioProcessingObjects\\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}";
        
        LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, apoPath, 0, NULL, 
                                       REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
        if (result == ERROR_SUCCESS) {
            // 获取 DLL 路径
            wchar_t dllPath[MAX_PATH];
            GetModuleFileNameW(NULL, dllPath, MAX_PATH);
            
            // 替换文件名为 VolumeBoosterAPO.dll
            wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
            if (lastSlash) {
                StringCbCopyW(lastSlash + 1, sizeof(dllPath) - (lastSlash + 1 - dllPath) * sizeof(wchar_t), 
                              L"VolumeBoosterAPO.dll");
            }

            RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)L"VolumeBooster APO", 
                           (DWORD)(wcslen(L"VolumeBooster APO") + 1) * sizeof(wchar_t));
            RegSetValueExW(hKey, L"APODll", 0, REG_SZ, (BYTE*)dllPath, 
                           (DWORD)(wcslen(dllPath) + 1) * sizeof(wchar_t));
            
            DWORD flags = 0x00000001;  // APO_FLAG_DEFAULT
            RegSetValueExW(hKey, L"APOFlags", 0, REG_DWORD, (BYTE*)&flags, sizeof(DWORD));
            
            RegCloseKey(hKey);
            LogEvent(L"APO 注册成功: %s", dllPath);
        } else {
            LogEvent(L"APO 注册失败: %lu", result);
        }
    }

    // 检测蓝牙设备
    bool IsBluetoothDevice(LPCWSTR deviceId) {
        if (!deviceId) return false;
        // 蓝牙设备 ID 通常包含 "Bluetooth" 或 "BT"
        return (wcsstr(deviceId, L"Bluetooth") != nullptr || 
                wcsstr(deviceId, L"BT") != nullptr ||
                wcsstr(deviceId, L"bth") != nullptr);
    }

    // 写入通知标记（供 GUI 读取）
    void WriteNotification(const wchar_t* message) {
        HKEY hKey;
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, 
                                       L"Software\\VolumeBooster\\Notifications", 
                                       0, NULL, REG_OPTION_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
        if (result == ERROR_SUCCESS) {
            RegSetValueExW(hKey, L"LastNotification", 0, REG_SZ, 
                           (BYTE*)message, (DWORD)(wcslen(message) + 1) * sizeof(wchar_t));
            
            DWORD timestamp = (DWORD)time(NULL);
            RegSetValueExW(hKey, L"Timestamp", 0, REG_DWORD, 
                           (BYTE*)&timestamp, sizeof(DWORD));
            RegCloseKey(hKey);
        }
    }

    // 日志记录
    void LogEvent(const wchar_t* format, ...) {
        wchar_t buffer[1024];
        va_list args;
        va_start(args, format);
        StringCbVPrintfW(buffer, sizeof(buffer), format, args);
        va_end(args);

        // 写入日志文件
        wchar_t logPath[MAX_PATH];
        GetEnvironmentVariableW(L"APPDATA", logPath, MAX_PATH);
        StringCbCatW(logPath, sizeof(logPath), L"\\VolumeBooster\\service.log");
        
        FILE* f = _wfopen(logPath, L"a, ccs=UTF-8");
        if (f) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, buffer);
            fclose(f);
        }
    }
};

// ========== 服务入口 ==========

#define SERVICE_NAME L"VolumeBoosterListener"

SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
SERVICE_STATUS g_ServiceStatus = {0};
HANDLE g_StopEvent = NULL;
DeviceChangeListener* g_Listener = NULL;
IMMDeviceEnumerator* g_Enumerator = NULL;

void WINAPI ServiceCtrlHandler(DWORD ctrlCode) {
    if (ctrlCode == SERVICE_CONTROL_STOP || ctrlCode == SERVICE_CONTROL_SHUTDOWN) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        SetEvent(g_StopEvent);
    }
}

void RegisterAPOOnStartup() {
    // 服务启动时确保 APO 已注册
    HKEY hKey;
    const wchar_t* apoPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio\\AudioProcessingObjects\\{BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}";
    
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, apoPath, 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        // APO 已注册
        return;
    }

    // APO 未注册，执行注册
    result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, apoPath, 0, NULL, 
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (result == ERROR_SUCCESS) {
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(NULL, dllPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
        if (lastSlash) {
            StringCbCopyW(lastSlash + 1, sizeof(dllPath) - (lastSlash + 1 - dllPath) * sizeof(wchar_t),
                          L"VolumeBoosterAPO.dll");
        }

        RegSetValueExW(hKey, L"", 0, REG_SZ, (BYTE*)L"VolumeBooster APO",
                       (DWORD)(wcslen(L"VolumeBooster APO") + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"APODll", 0, REG_SZ, (BYTE*)dllPath,
                       (DWORD)(wcslen(dllPath) + 1) * sizeof(wchar_t));
        DWORD flags = 0x00000001;
        RegSetValueExW(hKey, L"APOFlags", 0, REG_DWORD, (BYTE*)&flags, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    // 注册服务控制处理器
    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    // 设置服务状态
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // 创建停止事件
    g_StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_StopEvent) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // 确保 APO 已注册
    RegisterAPOOnStartup();

    // 初始化 COM
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // 创建设备枚举器
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&g_Enumerator);
    if (SUCCEEDED(hr) && g_Enumerator) {
        // 注册设备变更监听
        g_Listener = new DeviceChangeListener();
        g_Enumerator->RegisterEndpointNotificationCallback(g_Listener);
    }

    // 服务运行中
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // 等待停止信号
    WaitForSingleObject(g_StopEvent, INFINITE);

    // 清理
    if (g_Enumerator && g_Listener) {
        g_Enumerator->UnregisterEndpointNotificationCallback(g_Listener);
        g_Listener->Release();
    }
    if (g_Enumerator) {
        g_Enumerator->Release();
    }
    CoUninitialize();

    // 服务停止
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    
    CloseHandle(g_StopEvent);
}

// ========== 主函数 ==========

int wmain(int argc, wchar_t* argv[]) {
    // 检查是否以控制台模式运行（调试用）
    if (argc > 1 && wcscmp(argv[1], L"--console") == 0) {
        wprintf(L"VolumeBooster Listener (控制台模式)\n");
        wprintf(L"按 Ctrl+C 停止\n\n");
        
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        
        g_Enumerator = NULL;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), (void**)&g_Enumerator);
        if (SUCCEEDED(hr) && g_Enumerator) {
            g_Listener = new DeviceChangeListener();
            g_Enumerator->RegisterEndpointNotificationCallback(g_Listener);
            wprintf(L"设备监听已启动，等待设备变更事件...\n");
            
            // 等待 Ctrl+C
            g_StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
                if (ctrlType == CTRL_C_EVENT) {
                    SetEvent(g_StopEvent);
                    return TRUE;
                }
                return FALSE;
            }, TRUE);
            
            WaitForSingleObject(g_StopEvent, INFINITE);
            
            g_Enumerator->UnregisterEndpointNotificationCallback(g_Listener);
            g_Listener->Release();
            g_Enumerator->Release();
        }
        
        CoUninitialize();
        wprintf(L"已停止。\n");
        return 0;
    }

    // 以服务模式运行
    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { (LPWSTR)SERVICE_NAME, ServiceMain },
        { NULL, NULL }
    };
    
    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        // 如果不在服务管理器中运行，提示用 --console 参数
        wprintf(L"请以服务方式运行，或使用 --console 参数进行调试。\n");
        return 1;
    }
    
    return 0;
}
