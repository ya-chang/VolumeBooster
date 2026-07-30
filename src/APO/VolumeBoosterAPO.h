#pragma once

#include <windows.h>
#include <audioenginebaseapo.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <strsafe.h>
#include <unordered_map>
#include <atomic>
#include <cmath>
#include <cstdint>

// {BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}
static const GUID CLSID_VolumeBoosterAPO = 
    { 0xbfa2a5e1, 0x4f1d, 0x4c8b, { 0x9e, 0x7a, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f } };

// ========== 共享内存结构 ==========

struct LevelSample {
    float rmsLeft, rmsRight, peakLeft, peakRight;
    uint64_t timestamp;
};

struct LevelRingBuffer {
    std::atomic<uint32_t> writeIndex;
    std::atomic<uint32_t> readIndex;
    LevelSample samples[4096];
    void Reset() { writeIndex.store(0); readIndex.store(0); }
    void Write(const LevelSample& s) {
        uint32_t idx = writeIndex.load(std::memory_order_relaxed);
        samples[idx % 4096] = s;
        writeIndex.store(idx + 1, std::memory_order_release);
    }
};

// Named Pipe 协议
#pragma pack(push, 1)
struct AppGainEntry { DWORD processId; float gainDb; wchar_t appName[64]; };
struct PipeMessage {
    uint32_t magic, version;
    float globalGainDb, thresholdDb;
    bool limiterEnabled;
    uint32_t appCount;
    AppGainEntry appGains[32];
};
#pragma pack(pop)
static const uint32_t PIPE_MAGIC = 0x564F4C42;
static const uint32_t PIPE_VERSION = 1;

// ========== 软限幅器 ==========

class SoftLimiter {
public:
    void ProcessBuffer(float* buf, size_t n, float threshold) {
        float inv = 1.0f / threshold;
        for (size_t i = 0; i < n; i++) buf[i] = threshold * tanhf(buf[i] * inv);
    }
};

// ========== 电平计算 ==========

class LevelMeter {
public:
    static void Calc(const float* buf, size_t n, float& rms, float& peak) {
        float ss = 0, mx = 0;
        for (size_t i = 0; i < n; i++) { ss += buf[i]*buf[i]; float a = fabsf(buf[i]); if (a > mx) mx = a; }
        rms = n > 0 ? sqrtf(ss/n) : 0; peak = mx;
    }
    static float ToDb(float v) { return v < 1e-10f ? -100.0f : 20.0f * log10f(v); }
};

// ========== APO 主类 ==========

class VolumeBoosterAPO : public IAudioProcessingObject,
                          public IAudioProcessingObjectRT {
public:
    VolumeBoosterAPO() : m_refCount(1), m_globalGainDb(0), m_thresholdDb(-3),
        m_limiterEnabled(true), m_appGainCount(0), m_hLevelShm(NULL),
        m_pLevelBuffer(nullptr), m_hPipeThread(NULL), m_pipeThreadRunning(false),
        m_tempBuffer(nullptr), m_tempBufferSize(0)
    {
        ZeroMemory(m_appGains, sizeof(m_appGains));
        wchar_t shmName[128];
        StringCbPrintfW(shmName, sizeof(shmName), L"VolumeBooster_LevelBuffer_%lu", WTSGetActiveConsoleSessionId());
        m_hLevelShm = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LevelRingBuffer), shmName);
        if (m_hLevelShm) {
            m_pLevelBuffer = (LevelRingBuffer*)MapViewOfFile(m_hLevelShm, FILE_MAP_WRITE, 0, 0, sizeof(LevelRingBuffer));
            if (m_pLevelBuffer) m_pLevelBuffer->Reset();
        }
        m_pipeThreadRunning = true;
        m_hPipeThread = CreateThread(NULL, 0, PipeListenerThread, this, 0, NULL);
    }

    ~VolumeBoosterAPO() {
        m_pipeThreadRunning = false;
        if (m_hPipeThread) { WaitForSingleObject(m_hPipeThread, 2000); CloseHandle(m_hPipeThread); }
        if (m_pLevelBuffer) UnmapViewOfFile(m_pLevelBuffer);
        if (m_hLevelShm) CloseHandle(m_hLevelShm);
        if (m_tempBuffer) _aligned_free(m_tempBuffer);
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioProcessingObject)) {
            *ppv = static_cast<IAudioProcessingObject*>(this); AddRef(); return S_OK;
        }
        if (riid == __uuidof(IAudioProcessingObjectRT)) {
            *ppv = static_cast<IAudioProcessingObjectRT*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return m_refCount.fetch_add(1) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = m_refCount.fetch_sub(1) - 1;
        if (r == 0) delete this;
        return r;
    }

    // IAudioProcessingObject
    HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* p) override { if (p) *p = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** pp) override {
        if (!pp) return E_POINTER;
        APO_REG_PROPERTIES* p = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
        if (!p) return E_OUTOFMEMORY;
        ZeroMemory(p, sizeof(APO_REG_PROPERTIES));
        p->clsid = CLSID_VolumeBoosterAPO;
        p->Flags = APO_FLAG_DEFAULT;
        StringCbCopyW(p->szFriendlyName, sizeof(p->szFriendlyName), L"VolumeBooster APO");
        StringCbCopyW(p->szCopyrightInfo, sizeof(p->szCopyrightInfo), L"Copyright 2026");
        *pp = p; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(UINT32 cb, BYTE* pb) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* a, IAudioMediaType* b, IAudioMediaType** c) override {
        if (c) *c = nullptr; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* a, IAudioMediaType* b, IAudioMediaType** c) override {
        if (c) *c = nullptr; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* p) override {
        if (!p) return E_POINTER; *p = 2; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { return S_OK; }

    // IAudioProcessingObjectRT
    UINT32 CalcInputFrames(UINT32 u32OutputFrameCount) override { return u32OutputFrameCount; }
    UINT32 CalcOutputFrames(UINT32 u32InputFrameCount) override { return u32InputFrameCount; }

    // IAudioProcessingObjectRT - 核心实时处理
    // 方法名必须是 APOProcess，返回 void，参数用 APO_CONNECTION_PROPERTY**
    void APOProcess(
        UINT32 u32NumInputConnections,
        APO_CONNECTION_PROPERTY** ppInputConnections,
        UINT32 u32NumOutputConnections,
        APO_CONNECTION_PROPERTY** ppOutputConnections) override
    {
        if (!ppInputConnections || !ppOutputConnections ||
            u32NumInputConnections == 0 || u32NumOutputConnections == 0) return;

        APO_CONNECTION_PROPERTY* pIn = ppInputConnections[0];
        APO_CONNECTION_PROPERTY* pOut = ppOutputConnections[0];
        if (!pIn || !pOut) return;

        UINT32 frames = pIn->u32ValidFrameCount;
        if (frames == 0) return;

        float* pInput = (float*)pIn->pBuffer;
        float* pOutput = (float*)pOut->pBuffer;
        if (!pInput || !pOutput) return;

        UINT32 channels = 2;  // 默认 stereo
        size_t sampleCount = frames * channels;

        // 确保缓冲区足够
        if (sampleCount > m_tempBufferSize) {
            if (m_tempBuffer) _aligned_free(m_tempBuffer);
            m_tempBuffer = (float*)_aligned_malloc(sampleCount * sizeof(float), 16);
            m_tempBufferSize = sampleCount;
        }
        if (!m_tempBuffer) return;

        // 读取参数
        float gainDb = m_globalGainDb.load(std::memory_order_relaxed);
        float thresholdDb = m_thresholdDb.load(std::memory_order_relaxed);
        bool limiterOn = m_limiterEnabled.load(std::memory_order_relaxed);
        float gainLinear = powf(10.0f, gainDb / 20.0f);
        float thresholdLinear = powf(10.0f, thresholdDb / 20.0f);

        // 复制输入到临时缓冲区
        CopyMemory(m_tempBuffer, pInput, sampleCount * sizeof(float));

        // 增益
        if (gainDb > 0.01f) {
            for (size_t i = 0; i < sampleCount; i++) m_tempBuffer[i] *= gainLinear;
        }

        // 软限幅
        if (limiterOn) {
            m_limiter.ProcessBuffer(m_tempBuffer, sampleCount, thresholdLinear);
        }

        // 硬限制
        for (size_t i = 0; i < sampleCount; i++) {
            if (m_tempBuffer[i] > 1.0f) m_tempBuffer[i] = 1.0f;
            else if (m_tempBuffer[i] < -1.0f) m_tempBuffer[i] = -1.0f;
        }

        // 复制到输出
        CopyMemory(pOutput, m_tempBuffer, sampleCount * sizeof(float));

        // 电平数据
        WriteLevel(pOutput, frames, channels);

        // 设置输出属性
        pOut->u32ValidFrameCount = frames;
        pOut->u32BufferFlags = pIn->u32BufferFlags;
    }

private:
    void WriteLevel(const float* buf, UINT32 frames, UINT32 ch) {
        if (!m_pLevelBuffer) return;
        LevelSample s;
        if (ch >= 2) {
            float lBuf[4096], rBuf[4096];
            size_t n = frames < 4096 ? frames : 4096;
            for (size_t i = 0; i < n; i++) { lBuf[i] = buf[i*ch]; rBuf[i] = buf[i*ch+1]; }
            float rl, pl, rr, pr;
            LevelMeter::Calc(lBuf, n, rl, pl); LevelMeter::Calc(rBuf, n, rr, pr);
            s.rmsLeft = LevelMeter::ToDb(rl); s.rmsRight = LevelMeter::ToDb(rr);
            s.peakLeft = LevelMeter::ToDb(pl); s.peakRight = LevelMeter::ToDb(pr);
        } else {
            float r, p;
            LevelMeter::Calc(buf, frames, r, p);
            s.rmsLeft = s.rmsRight = LevelMeter::ToDb(r);
            s.peakLeft = s.peakRight = LevelMeter::ToDb(p);
        }
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
        s.timestamp = (uint64_t)(c.QuadPart * 10000000 / f.QuadPart);
        m_pLevelBuffer->Write(s);
    }

    static DWORD WINAPI PipeListenerThread(LPVOID lp) {
        VolumeBoosterAPO* self = (VolumeBoosterAPO*)lp;
        wchar_t name[128];
        StringCbPrintfW(name, sizeof(name), L"\\\\.\\pipe\\VolumeBooster_%lu", WTSGetActiveConsoleSessionId());
        while (self->m_pipeThreadRunning) {
            HANDLE h = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND,
                PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT, 1, 0, sizeof(PipeMessage), 1000, NULL);
            if (h == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }
            if (ConnectNamedPipe(h, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
                PipeMessage msg; DWORD br;
                if (ReadFile(h, &msg, sizeof(msg), &br, NULL) && br == sizeof(msg) && msg.magic == PIPE_MAGIC) {
                    float g = msg.globalGainDb;
                    if (g > 14.0f) g = 14.0f; if (g < 0) g = 0;
                    float t = msg.thresholdDb;
                    if (t > 0) t = 0; if (t < -20) t = -20;
                    self->m_globalGainDb.store(g, std::memory_order_relaxed);
                    self->m_thresholdDb.store(t, std::memory_order_relaxed);
                    self->m_limiterEnabled.store(msg.limiterEnabled, std::memory_order_relaxed);
                    uint32_t cnt = msg.appCount < 32 ? msg.appCount : 32;
                    for (uint32_t i = 0; i < cnt; i++) self->m_appGains[i] = msg.appGains[i];
                    self->m_appGainCount.store(cnt, std::memory_order_relaxed);
                }
            }
            DisconnectNamedPipe(h); CloseHandle(h);
        }
        return 0;
    }

    std::atomic<ULONG> m_refCount;
    std::atomic<float> m_globalGainDb, m_thresholdDb;
    std::atomic<bool> m_limiterEnabled;
    AppGainEntry m_appGains[32];
    std::atomic<uint32_t> m_appGainCount;
    SoftLimiter m_limiter;
    HANDLE m_hLevelShm;
    LevelRingBuffer* m_pLevelBuffer;
    HANDLE m_hPipeThread;
    volatile bool m_pipeThreadRunning;
    float* m_tempBuffer;
    size_t m_tempBufferSize;
};
