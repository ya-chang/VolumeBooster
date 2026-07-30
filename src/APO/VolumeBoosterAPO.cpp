#include "VolumeBoosterAPO.h"
#include <functiondiscoverykeys_devpkey.h>
#include <strsafe.h>
#include <cmath>
#include <algorithm>

VolumeBoosterAPO::VolumeBoosterAPO()
    : m_refCount(1)
    , m_sampleRate(48000)
    , m_channelCount(2)
    , m_globalGainDb(0.0f)
    , m_thresholdDb(-3.0f)
    , m_limiterEnabled(true)
    , m_appGainCount(0)
    , m_hLevelShm(NULL)
    , m_pLevelBuffer(nullptr)
    , m_hPipeThread(NULL)
    , m_pipeThreadRunning(false)
    , m_tempBuffer(nullptr)
    , m_tempBufferSize(0)
{
    ZeroMemory(m_appGains, sizeof(m_appGains));
    
    wchar_t shmName[128];
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    StringCbPrintfW(shmName, sizeof(shmName), 
                     L"VolumeBooster_LevelBuffer_%lu", sessionId);
    
    m_hLevelShm = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, sizeof(LevelRingBuffer), shmName);
    
    if (m_hLevelShm) {
        m_pLevelBuffer = (LevelRingBuffer*)MapViewOfFile(
            m_hLevelShm, FILE_MAP_WRITE, 0, 0, sizeof(LevelRingBuffer));
        if (m_pLevelBuffer) {
            m_pLevelBuffer->Reset();
        }
    }
    
    m_pipeThreadRunning = true;
    m_hPipeThread = CreateThread(NULL, 0, PipeListenerThread, this, 0, NULL);
}

VolumeBoosterAPO::~VolumeBoosterAPO()
{
    m_pipeThreadRunning = false;
    if (m_hPipeThread) {
        WaitForSingleObject(m_hPipeThread, 2000);
        CloseHandle(m_hPipeThread);
    }
    if (m_pLevelBuffer) UnmapViewOfFile(m_pLevelBuffer);
    if (m_hLevelShm) CloseHandle(m_hLevelShm);
    if (m_tempBuffer) _aligned_free(m_tempBuffer);
}

// ========== IUnknown ==========

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioProcessingObject)) {
        *ppvObject = static_cast<IAudioProcessingObject*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == __uuidof(IAudioProcessingObjectRT)) {
        *ppvObject = static_cast<IAudioProcessingObjectRT*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VolumeBoosterAPO::AddRef() { return m_refCount.fetch_add(1) + 1; }

ULONG STDMETHODCALLTYPE VolumeBoosterAPO::Release()
{
    ULONG ref = m_refCount.fetch_sub(1) - 1;
    if (ref == 0) delete this;
    return ref;
}

// ========== IAudioProcessingObject ==========

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetLatency(HNSTIME* pTimeLatency)
{
    if (!pTimeLatency) return E_POINTER;
    *pTimeLatency = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps)
{
    if (!ppRegProps) return E_POINTER;
    
    APO_REG_PROPERTIES* pProps = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
    if (!pProps) return E_OUTOFMEMORY;
    
    ZeroMemory(pProps, sizeof(APO_REG_PROPERTIES));
    pProps->clsid = CLSID_VolumeBoosterAPO;
    pProps->Flags = APO_FLAG_DEFAULT;
    
    // szFriendlyName 是 wchar_t[256]
    const wchar_t* name = L"VolumeBooster APO";
    StringCbCopyW(pProps->szFriendlyName, sizeof(pProps->szFriendlyName), name);
    
    // szCopyrightInfo 是 wchar_t[256]
    const wchar_t* copyright = L"Copyright 2026";
    StringCbCopyW(pProps->szCopyrightInfo, sizeof(pProps->szCopyrightInfo), copyright);
    
    *ppRegProps = pProps;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::IsInputFormatSupported(
    IAudioMediaType* pOutputFmt,
    IAudioMediaType* pRequestedInputFmt,
    IAudioMediaType** ppSupportedInputFmt)
{
    if (ppSupportedInputFmt) *ppSupportedInputFmt = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::IsOutputFormatSupported(
    IAudioMediaType* pInputFmt,
    IAudioMediaType* pRequestedOutputFmt,
    IAudioMediaType** ppSupportedOutputFmt)
{
    if (ppSupportedOutputFmt) *ppSupportedOutputFmt = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetInputChannelCount(UINT32* pu32ChannelCount)
{
    if (!pu32ChannelCount) return E_POINTER;
    *pu32ChannelCount = m_channelCount;
    return S_OK;
}

// ========== IAudioProcessingObjectRT ==========

ULONG VolumeBoosterAPO::Process(
    ULONG u32NumInputConnections,
    APO_CONNECTION_DESCRIPTOR** ppInputConnections,
    ULONG u32NumOutputConnections,
    APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
    if (!ppInputConnections || !ppOutputConnections ||
        u32NumInputConnections == 0 || u32NumOutputConnections == 0) {
        return 0;
    }
    
    APO_CONNECTION_PROPERTY* pInputProp = ppInputConnections[0]->pProperty;
    APO_CONNECTION_PROPERTY* pOutputProp = ppOutputConnections[0]->pProperty;
    
    if (!pInputProp || !pOutputProp) return 0;
    
    UINT32 frameCount = pInputProp->u32ValidFrameCount;
    if (frameCount == 0) return 0;
    
    float* pInput = (float*)pInputProp->pBuffer;
    float* pOutput = (float*)pOutputProp->pBuffer;
    
    size_t sampleCount = frameCount * m_channelCount;
    if (sampleCount > m_tempBufferSize) {
        if (m_tempBuffer) _aligned_free(m_tempBuffer);
        m_tempBuffer = (float*)_aligned_malloc(sampleCount * sizeof(float), 16);
        m_tempBufferSize = sampleCount;
    }
    
    float gainDb = m_globalGainDb.load(std::memory_order_relaxed);
    float thresholdDb = m_thresholdDb.load(std::memory_order_relaxed);
    bool limiterOn = m_limiterEnabled.load(std::memory_order_relaxed);
    float thresholdLinear = powf(10.0f, thresholdDb / 20.0f);
    float gainLinear = powf(10.0f, gainDb / 20.0f);
    
    if (gainDb >= -0.01f && !limiterOn) {
        CopyMemory(pOutput, pInput, sampleCount * sizeof(float));
    } else {
        CopyMemory(m_tempBuffer, pInput, sampleCount * sizeof(float));
        if (gainDb > 0.01f) ApplyGain(m_tempBuffer, sampleCount, gainLinear);
        if (limiterOn) m_limiter.ProcessBuffer(m_tempBuffer, sampleCount, thresholdLinear);
        HardClip(m_tempBuffer, sampleCount);
        CopyMemory(pOutput, m_tempBuffer, sampleCount * sizeof(float));
    }
    
    WriteLevelData(pOutput, frameCount, m_channelCount);
    
    pOutputProp->u32ValidFrameCount = frameCount;
    pOutputProp->u32BufferFlags = pInputProp->u32BufferFlags;
    
    return 1;  // APO_PROCESS_BUFFER_VALID
}

// ========== 内部方法 ==========

void VolumeBoosterAPO::ApplyGain(float* buffer, size_t sampleCount, float gainLinear)
{
    for (size_t i = 0; i < sampleCount; i++) {
        buffer[i] *= gainLinear;
    }
}

void VolumeBoosterAPO::HardClip(float* buffer, size_t sampleCount)
{
    for (size_t i = 0; i < sampleCount; i++) {
        if (buffer[i] > 1.0f) buffer[i] = 1.0f;
        else if (buffer[i] < -1.0f) buffer[i] = -1.0f;
    }
}

void VolumeBoosterAPO::WriteLevelData(const float* buffer, size_t frameCount, uint32_t channelCount)
{
    if (!m_pLevelBuffer) return;
    
    LevelSample sample;
    
    if (channelCount >= 2) {
        float leftBuf[4096];
        float rightBuf[4096];
        size_t count = (frameCount < 4096) ? frameCount : 4096;
        for (size_t i = 0; i < count; i++) {
            leftBuf[i] = buffer[i * channelCount];
            rightBuf[i] = buffer[i * channelCount + 1];
        }
        float rmsL, peakL, rmsR, peakR;
        LevelMeter::CalculateMono(leftBuf, count, rmsL, peakL);
        LevelMeter::CalculateMono(rightBuf, count, rmsR, peakR);
        sample.rmsLeft = LevelMeter::LinearToDb(rmsL);
        sample.rmsRight = LevelMeter::LinearToDb(rmsR);
        sample.peakLeft = LevelMeter::LinearToDb(peakL);
        sample.peakRight = LevelMeter::LinearToDb(peakR);
    } else {
        float rms, peak;
        LevelMeter::CalculateMono(buffer, frameCount, rms, peak);
        sample.rmsLeft = LevelMeter::LinearToDb(rms);
        sample.rmsRight = sample.rmsLeft;
        sample.peakLeft = LevelMeter::LinearToDb(peak);
        sample.peakRight = sample.peakLeft;
    }
    
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    sample.timestamp = (uint64_t)(counter.QuadPart * 10000000 / freq.QuadPart);
    
    m_pLevelBuffer->Write(sample);
}

// ========== Named Pipe 监听线程 ==========

DWORD WINAPI VolumeBoosterAPO::PipeListenerThread(LPVOID lpParam)
{
    VolumeBoosterAPO* pThis = (VolumeBoosterAPO*)lpParam;
    
    wchar_t pipeName[128];
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    StringCbPrintfW(pipeName, sizeof(pipeName),
                     L"\\\\.\\pipe\\VolumeBooster_%lu", sessionId);
    
    while (pThis->m_pipeThreadRunning) {
        HANDLE hPipe = CreateNamedPipeW(
            pipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 0, sizeof(PipeMessage), 1000, NULL);
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : 
                         (GetLastError() == ERROR_PIPE_CONNECTED);
        
        if (connected) {
            PipeMessage msg;
            DWORD bytesRead;
            if (ReadFile(hPipe, &msg, sizeof(msg), &bytesRead, NULL) &&
                bytesRead == sizeof(msg) &&
                msg.magic == PIPE_MAGIC &&
                msg.version == PIPE_VERSION) {
                
                float gainDb = msg.globalGainDb;
                if (gainDb > 14.0f) gainDb = 14.0f;
                if (gainDb < 0.0f) gainDb = 0.0f;
                
                float thresholdDb = msg.thresholdDb;
                if (thresholdDb > 0.0f) thresholdDb = 0.0f;
                if (thresholdDb < -20.0f) thresholdDb = -20.0f;
                
                pThis->m_globalGainDb.store(gainDb, std::memory_order_relaxed);
                pThis->m_thresholdDb.store(thresholdDb, std::memory_order_relaxed);
                pThis->m_limiterEnabled.store(msg.limiterEnabled, std::memory_order_relaxed);
                
                uint32_t count = (msg.appCount < 32) ? msg.appCount : 32;
                for (uint32_t i = 0; i < count; i++) {
                    pThis->m_appGains[i] = msg.appGains[i];
                }
                pThis->m_appGainCount.store(count, std::memory_order_relaxed);
            }
        }
        
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    
    return 0;
}
