#pragma once

#include <windows.h>
#include <audioenginebaseapo.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <unordered_map>
#include <atomic>
#include <cmath>
#include <cstdint>

// {BFA2A5E1-4F1D-4C8B-9E7A-1A2B3C4D5E6F}
static const GUID CLSID_VolumeBoosterAPO = 
    { 0xbfa2a5e1, 0x4f1d, 0x4c8b, { 0x9e, 0x7a, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f } };

// ========== 共享内存结构 ==========

struct LevelSample {
    float rmsLeft;
    float rmsRight;
    float peakLeft;
    float peakRight;
    uint64_t timestamp;
};

struct LevelRingBuffer {
    std::atomic<uint32_t> writeIndex;
    std::atomic<uint32_t> readIndex;
    LevelSample samples[4096];
    
    void Reset() {
        writeIndex.store(0);
        readIndex.store(0);
    }
    
    void Write(const LevelSample& sample) {
        uint32_t idx = writeIndex.load(std::memory_order_relaxed);
        samples[idx % 4096] = sample;
        writeIndex.store(idx + 1, std::memory_order_release);
    }
};

// Named Pipe 协议
#pragma pack(push, 1)
struct AppGainEntry {
    DWORD processId;
    float gainDb;
    wchar_t appName[64];
};

struct PipeMessage {
    uint32_t magic;
    uint32_t version;
    float globalGainDb;
    float thresholdDb;
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
    inline float Process(float sample, float threshold) {
        float normalized = sample / threshold;
        return threshold * tanhf(normalized);
    }
    
    void ProcessBuffer(float* buffer, size_t sampleCount, float threshold) {
        float inv_threshold = 1.0f / threshold;
        for (size_t i = 0; i < sampleCount; i++) {
            float normalized = buffer[i] * inv_threshold;
            buffer[i] = threshold * tanhf(normalized);
        }
    }
};

// ========== 电平计算 ==========

class LevelMeter {
public:
    static void CalculateMono(const float* buffer, size_t sampleCount, 
                               float& rms, float& peak) {
        float sumSquares = 0.0f;
        float maxAbs = 0.0f;
        for (size_t i = 0; i < sampleCount; i++) {
            float abs_val = fabsf(buffer[i]);
            sumSquares += buffer[i] * buffer[i];
            if (abs_val > maxAbs) maxAbs = abs_val;
        }
        rms = (sampleCount > 0) ? sqrtf(sumSquares / sampleCount) : 0.0f;
        peak = maxAbs;
    }
    
    static float LinearToDb(float linear) {
        if (linear < 1e-10f) return -100.0f;
        return 20.0f * log10f(linear);
    }
};

// ========== APO 主类 ==========

class VolumeBoosterAPO : public IAudioProcessingObject,
                          public IAudioProcessingObjectRT {
public:
    VolumeBoosterAPO();
    ~VolumeBoosterAPO();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IAudioProcessingObject
    HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* pTimeLatency) override;
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) override;
    HRESULT STDMETHODCALLTYPE Initialize(UINT32 cbDataSize, BYTE* pbyData) override;
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(
        IAudioMediaType* pOutputFmt,
        IAudioMediaType* pRequestedInputFmt,
        IAudioMediaType** ppSupportedInputFmt) override;
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(
        IAudioMediaType* pInputFmt,
        IAudioMediaType* pRequestedOutputFmt,
        IAudioMediaType** ppSupportedOutputFmt) override;
    HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* pu32ChannelCount) override;

    // IAudioProcessingObjectRT - 实时处理
    // 注意：Process 的签名必须严格匹配 SDK 定义
    APOProcessResult Process(
        UINT32 u32NumInputConnections,
        APO_CONNECTION_DESCRIPTOR** ppInputConnections,
        UINT32 u32NumOutputConnections,
        APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;

private:
    void ApplyGain(float* buffer, size_t sampleCount, float gainLinear);
    void HardClip(float* buffer, size_t sampleCount);
    void WriteLevelData(const float* buffer, size_t frameCount, uint32_t channelCount);

    std::atomic<ULONG> m_refCount;
    UINT32 m_sampleRate;
    UINT32 m_channelCount;
    
    std::atomic<float> m_globalGainDb;
    std::atomic<float> m_thresholdDb;
    std::atomic<bool> m_limiterEnabled;
    
    static const size_t MAX_APP_GAINS = 32;
    AppGainEntry m_appGains[MAX_APP_GAINS];
    std::atomic<uint32_t> m_appGainCount;
    
    SoftLimiter m_limiter;
    
    HANDLE m_hLevelShm;
    LevelRingBuffer* m_pLevelBuffer;
    
    HANDLE m_hPipeThread;
    volatile bool m_pipeThreadRunning;
    static DWORD WINAPI PipeListenerThread(LPVOID lpParam);
    
    float* m_tempBuffer;
    size_t m_tempBufferSize;
};
