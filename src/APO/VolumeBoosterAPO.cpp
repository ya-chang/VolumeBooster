#include "VolumeBoosterAPO.h"
#include <functiondiscoverykeys_devpkey.h>
#include <strsafe.h>
#include <cmath>
#include <algorithm>

// ========== 构造/析构 ==========

VolumeBoosterAPO::VolumeBoosterAPO()
    : m_refCount(1)
    , m_sampleRate(48000)
    , m_channelCount(2)
    , m_bitsPerSample(32)
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
    
    // 创建共享内存（电平数据）
    // 使用当前用户 SID 构建唯一名称
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
    
    // 启动 Named Pipe 监听线程
    m_pipeThreadRunning = true;
    m_hPipeThread = CreateThread(NULL, 0, PipeListenerThread, this, 0, NULL);
}

VolumeBoosterAPO::~VolumeBoosterAPO()
{
    // 停止 Pipe 监听线程
    m_pipeThreadRunning = false;
    if (m_hPipeThread) {
        WaitForSingleObject(m_hPipeThread, 2000);
        CloseHandle(m_hPipeThread);
    }
    
    // 清理共享内存
    if (m_pLevelBuffer) {
        UnmapViewOfFile(m_pLevelBuffer);
    }
    if (m_hLevelShm) {
        CloseHandle(m_hLevelShm);
    }
    
    // 释放预分配缓冲区
    if (m_tempBuffer) {
        _aligned_free(m_tempBuffer);
    }
}

// ========== IUnknown ==========

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject) return E_POINTER;
    
    if (riid == __uuidof(IUnknown)) {
        *ppvObject = static_cast<IAudioProcessingObject*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == __uuidof(IAudioProcessingObject)) {
        *ppvObject = static_cast<IAudioProcessingObject*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == __uuidof(IAudioProcessingObjectRT)) {
        *ppvObject = static_cast<IAudioProcessingObjectRT*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == __uuidof(IAudioSystemEffects)) {
        *ppvObject = static_cast<IAudioSystemEffects*>(this);
        AddRef();
        return S_OK;
    }
    
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VolumeBoosterAPO::AddRef()
{
    return m_refCount.fetch_add(1) + 1;
}

ULONG STDMETHODCALLTYPE VolumeBoosterAPO::Release()
{
    ULONG ref = m_refCount.fetch_sub(1) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

// ========== IAudioProcessingObject ==========

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetLatency(HNSTIME* pTimeLatency)
{
    if (!pTimeLatency) return E_POINTER;
    *pTimeLatency = 0;  // APO 处理延迟 < 1ms，可忽略
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps)
{
    if (!ppRegProps) return E_POINTER;
    
    // 分配注册属性结构
    *ppRegProps = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
    if (!*ppRegProps) return E_OUTOFMEMORY;
    
    APO_REG_PROPERTIES* pProps = *ppRegProps;
    pProps->clsid = CLSID_VolumeBoosterAPO;
    pProps->Flags = APO_FLAG_DEFAULT;
    pProps->szFriendlyName[0] = L'V';
    pProps->szFriendlyName[1] = L'o';
    pProps->szFriendlyName[2] = L'l';
    pProps->szFriendlyName[3] = L'u';
    pProps->szFriendlyName[4] = L'm';
    pProps->szFriendlyName[5] = L'e';
    pProps->szFriendlyName[6] = L' ';
    pProps->szFriendlyName[7] = L'B';
    pProps->szFriendlyName[8] = L'o';
    pProps->szFriendlyName[9] = L'o';
    pProps->szFriendlyName[10] = L's';
    pProps->szFriendlyName[11] = L't';
    pProps->szFriendlyName[12] = L'e';
    pProps->szFriendlyName[13] = L'r';
    pProps->szFriendlyName[14] = L'\0';
    
    StringCbCopyW(pProps->szCopyrightInfo, sizeof(pProps->szCopyrightInfo), L"Copyright 2026");
    pProps->u32MinInputBufferFrames = 0;
    pProps->u32MaxInputBufferFrames = 4096;
    pProps->u32MinOutputBufferFrames = 0;
    pProps->u32MaxOutputBufferFrames = 4096;
    pProps->u32MaxInstances = 1;
    pProps->u32NumAPOInterfaces = 0;
    pProps->ppAPOInterfaces = nullptr;
    
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
    // APO 初始化，可以从 pbyData 读取初始配置
    // 这里不做特殊处理，参数通过 Named Pipe 动态更新
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::IsInputFormatSupported(
    IAudioMediaType* pOutputFmt,
    IAudioMediaType* pRequestedInputFmt,
    IAudioMediaType** ppSupportedInputFmt)
{
    // 接受所有格式
    if (ppSupportedInputFmt) {
        *ppSupportedInputFmt = nullptr;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::IsOutputFormatSupported(
    IAudioMediaType* pInputFmt,
    IAudioMediaType* pRequestedOutputFmt,
    IAudioMediaType** ppSupportedOutputFmt)
{
    // 接受所有格式
    if (ppSupportedOutputFmt) {
        *ppSupportedOutputFmt = nullptr;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetInputChannelCount(UINT32* pu32ChannelCount)
{
    if (!pu32ChannelCount) return E_POINTER;
    *pu32ChannelCount = m_channelCount;
    return S_OK;
}

// ========== IAudioProcessingObjectRT - 核心实时处理 ==========

APOProcessResult VolumeBoosterAPO::Process(
    UINT32 u32NumInputConnections,
    APO_CONNECTION_DESCRIPTOR** ppInputConnections,
    UINT32 u32NumOutputConnections,
    APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
    // 参数校验
    if (!ppInputConnections || !ppOutputConnections ||
        u32NumInputConnections == 0 || u32NumOutputConnections == 0) {
        return APOPROCESS_BUFFER_SILENT;
    }
    
    // 获取输入/输出缓冲区
    APO_CONNECTION_PROPERTY* pInputProp = ppInputConnections[0]->pProperty;
    APO_CONNECTION_PROPERTY* pOutputProp = ppOutputConnections[0]->pProperty;
    
    if (!pInputProp || !pOutputProp) {
        return APOPROCESS_BUFFER_SILENT;
    }
    
    UINT32 frameCount = pInputProp->u32ValidFrameCount;
    if (frameCount == 0) {
        return APOPROCESS_BUFFER_SILENT;
    }
    
    float* pInput = (float*)pInputProp->pBuffer;
    float* pOutput = (float*)pOutputProp->pBuffer;
    
    // 确保预分配缓冲区足够大
    size_t sampleCount = frameCount * m_channelCount;
    if (sampleCount > m_tempBufferSize) {
        if (m_tempBuffer) _aligned_free(m_tempBuffer);
        m_tempBuffer = (float*)_aligned_malloc(sampleCount * sizeof(float), 16);
        m_tempBufferSize = sampleCount;
    }
    
    // 读取当前参数
    float gainDb = m_globalGainDb.load(std::memory_order_relaxed);
    float thresholdDb = m_thresholdDb.load(std::memory_order_relaxed);
    bool limiterOn = m_limiterEnabled.load(std::memory_order_relaxed);
    float thresholdLinear = powf(10.0f, thresholdDb / 20.0f);
    float gainLinear = powf(10.0f, gainDb / 20.0f);
    
    // 增益为 0dB（100%）时直接跳过处理
    if (gainDb >= -0.01f && !limiterOn) {
        // 直接拷贝输入到输出
        CopyMemory(pOutput, pInput, sampleCount * sizeof(float));
    } else {
        // 复制到临时缓冲区
        CopyMemory(m_tempBuffer, pInput, sampleCount * sizeof(float));
        
        // 应用增益
        if (gainDb > 0.01f) {
            ApplyGain(m_tempBuffer, sampleCount, gainLinear);
        }
        
        // 应用软限幅
        if (limiterOn) {
            m_limiter.ProcessBuffer(m_tempBuffer, sampleCount, thresholdLinear);
        }
        
        // 硬限制（最后一道防线）
        HardClip(m_tempBuffer, sampleCount);
        
        // 复制到输出
        CopyMemory(pOutput, m_tempBuffer, sampleCount * sizeof(float));
    }
    
    // 计算电平并写入 ring buffer（不阻塞）
    WriteLevelData(pOutput, frameCount, m_channelCount);
    
    // 设置输出属性
    pOutputProp->u32ValidFrameCount = frameCount;
    pOutputProp->u32BufferFlags = pInputProp->u32BufferFlags;
    
    return APOPROCESS_BUFFER_VALID;
}

HRESULT STDMETHODCALLTYPE VolumeBoosterAPO::GetEffectsList(
    GUID* pGUIDs, UINT32* pNumEffects, HANDLE hEvent)
{
    if (!pNumEffects) return E_POINTER;
    *pNumEffects = 0;
    return S_OK;
}

// ========== 内部方法 ==========

void VolumeBoosterAPO::ApplyGain(float* buffer, size_t sampleCount, float gainLinear)
{
    // 对每个采样点施加增益
    // 注意：不做限幅，限幅在后续步骤处理
    for (size_t i = 0; i < sampleCount; i++) {
        buffer[i] *= gainLinear;
    }
}

void VolumeBoosterAPO::HardClip(float* buffer, size_t sampleCount)
{
    // 硬限制：绝对不允许超过 ±1.0 (0 dBFS)
    // 这是最后一道防线，正常情况下软限幅器已经处理了
    for (size_t i = 0; i < sampleCount; i++) {
        if (buffer[i] > 1.0f) buffer[i] = 1.0f;
        else if (buffer[i] < -1.0f) buffer[i] = -1.0f;
    }
}

void VolumeBoosterAPO::WriteLevelData(const float* buffer, size_t frameCount, uint32_t channelCount)
{
    if (!m_pLevelBuffer) return;
    
    // 计算左右声道电平
    // 对于 stereo：奇数索引为左，偶数索引为右
    // 对于多声道：前两个声道作为 L/R
    LevelSample sample;
    
    if (channelCount >= 2) {
        // 分离左右声道
        // 注意：在实时线程中，避免使用动态分配
        // 使用栈上的小缓冲区（最多 4096 帧）
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
        // Mono
        float rms, peak;
        LevelMeter::CalculateMono(buffer, frameCount, rms, peak);
        sample.rmsLeft = LevelMeter::LinearToDb(rms);
        sample.rmsRight = sample.rmsLeft;
        sample.peakLeft = LevelMeter::LinearToDb(peak);
        sample.peakRight = sample.peakLeft;
    }
    
    // 时间戳
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    sample.timestamp = (uint64_t)(counter.QuadPart * 10000000 / freq.QuadPart);
    
    // 写入 ring buffer（无锁，单生产者）
    m_pLevelBuffer->Write(sample);
}

void VolumeBoosterAPO::RefreshParameters()
{
    // 参数通过 Named Pipe 异步更新，这里不需要额外操作
    // 参数已在 m_globalGainDb, m_thresholdDb 等原子变量中
}

// ========== Named Pipe 监听线程 ==========

DWORD WINAPI VolumeBoosterAPO::PipeListenerThread(LPVOID lpParam)
{
    VolumeBoosterAPO* pThis = (VolumeBoosterAPO*)lpParam;
    
    // 构建管道名称（包含 session ID）
    wchar_t pipeName[128];
    DWORD sessionId = WTSGetActiveConsoleSessionId();
    StringCbPrintfW(pipeName, sizeof(pipeName),
                     L"\\\\.\\pipe\\VolumeBooster_%lu", sessionId);
    
    while (pThis->m_pipeThreadRunning) {
        // 创建 Named Pipe
        HANDLE hPipe = CreateNamedPipeW(
            pipeName,
            PIPE_ACCESS_INBOUND,           // 只读
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,                              // 最多 1 个实例
            0,                              // 输出缓冲区大小
            sizeof(PipeMessage),            // 输入缓冲区大小
            1000,                           // 超时时间 (ms)
            NULL                            // 安全属性（使用默认）
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        
        // 等待客户端连接
        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : 
                         (GetLastError() == ERROR_PIPE_CONNECTED);
        
        if (connected) {
            // 读取消息
            PipeMessage msg;
            DWORD bytesRead;
            
            if (ReadFile(hPipe, &msg, sizeof(msg), &bytesRead, NULL) &&
                bytesRead == sizeof(msg) &&
                msg.magic == PIPE_MAGIC &&
                msg.version == PIPE_VERSION) {
                
                // 验证增益值范围（安全校验）
                float gainDb = msg.globalGainDb;
                if (gainDb > 14.0f) gainDb = 14.0f;   // 上限 500% (+14dB)
                if (gainDb < 0.0f) gainDb = 0.0f;      // 下限 100% (0dB)
                
                float thresholdDb = msg.thresholdDb;
                if (thresholdDb > 0.0f) thresholdDb = 0.0f;
                if (thresholdDb < -20.0f) thresholdDb = -20.0f;
                
                // 更新参数
                pThis->m_globalGainDb.store(gainDb, std::memory_order_relaxed);
                pThis->m_thresholdDb.store(thresholdDb, std::memory_order_relaxed);
                pThis->m_limiterEnabled.store(msg.limiterEnabled, std::memory_order_relaxed);
                
                // 更新 per-app 增益
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
