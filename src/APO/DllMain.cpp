// DllMain.cpp - APO DLL 入口点
// VolumeBooster APO

#include <windows.h>
#include "VolumeBoosterAPO.h"

HMODULE g_hModule = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}

// ========== COM 工厂 ==========

class VolumeBoosterAPOFactory : public IClassFactory
{
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppvObject = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }  // 静态对象，永远返回 2
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    
    // IClassFactory
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override
    {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        if (!ppvObject) return E_POINTER;
        
        VolumeBoosterAPO* pAPO = new VolumeBoosterAPO();
        if (!pAPO) return E_OUTOFMEMORY;
        
        HRESULT hr = pAPO->QueryInterface(riid, ppvObject);
        pAPO->Release();
        return hr;
    }
    
    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override { return S_OK; }
};

static VolumeBoosterAPOFactory g_Factory;

// ========== COM 导出函数 ==========

extern "C" {

__declspec(dllexport) HRESULT DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    if (!ppv) return E_POINTER;
    
    if (rclsid == CLSID_VolumeBoosterAPO) {
        return g_Factory.QueryInterface(riid, ppv);
    }
    
    *ppv = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

__declspec(dllexport) HRESULT DllCanUnloadNow()
{
    return S_FALSE;  // 始终不卸载（简化实现）
}

__declspec(dllexport) HRESULT DllRegisterServer()
{
    // 注册 APO 到系统
    // 实际注册通过安装程序的注册表操作完成
    return S_OK;
}

__declspec(dllexport) HRESULT DllUnregisterServer()
{
    return S_OK;
}

}  // extern "C"
