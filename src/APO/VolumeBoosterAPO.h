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

// {A1B2C3D4-5E6F-7A8B-9C0D-1E2F3A4B5C6D}
static const GUID IID_IVolumeBoosterAPO = 
    { 0xa1b2c3d4, 0x5e6f, 0x7a8b, { 0x9c, 0x0d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d } };

// ========== 共享内存结构 ==========

// Ring Buffer 电平数据（APO → GUI，只写）
struct LevelSample {
    float rmsLeft;
    float rmsRight;
    float peakLeft;
    float peakRight;
    uint64_t timestamp;  // 100ns 单位
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

// Named Pipe 协议（GUI → APO，参数传递）
#pragma pack(push, 1)
struct AppGainEntry {
    DWORD processId;
    float gainDb;
    wchar_t appName[64];
};

struct PipeMessage {
    uint32_t magic;           // 0x564F4C42 ("VOLB")
    uint32_t version;         // 协议版本
    float globalGainDb;       // 全局增益 (dB)
    float thresholdDb;        // 限幅阈值 (dB)
    bool limiterEnabled;      // 限幅器开关
    uint32_t appCount;        // per-app 增益条目数
    AppGainEntry appGains[32];
};
#pragma pack(pop)

static const uint32_t PIPE_MAGIC = 0x564F4C42;  // "VOLB"
static const uint32_t PIPE_VERSION = 1;

// ========== 软限幅器 ==========

class SoftLimiter {
public:
    void Reset() {
        // tanh 无状态，无需重置
    }
    
    // 单采样点处理
    inline float Process(float sample, float threshold) {
        // tanh 软限幅
        // 当 sample << threshold 时：tanh(x) ≈ x，无损
        // 当 sample → threshold 时：平滑压缩
        float normalized = sample / threshold;
        return threshold * tanhf(normalized);
    }
    
    // 批量处理（SIMD 友好）
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
    // 计算单声道 RMS 和 Peak
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
    
    // 线性值转 dB
    static float LinearToDb(float linear) {
        if (linear < 1e-10f) return -100.0f;
        return 20.0f * log10f(linear);
    }
};

// ========== APO 主类 ==========

class VolumeBoosterAPO : public IAudioProcessingObject,
                          public IAudioProcessingObjectRT,
                          public IAudioSystemEffects {
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
    APOProcessResult Process(
        UINT32 u32NumInputConnections,
        APO_CONNECTION_DESCRIPTOR** ppInputConnections,
        UINT32 u32NumOutputConnections,
        APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;

    // IAudioSystemEffects
    HRESULT STDMETHODCALLTYPE GetEffectsList(
        GUID* pGUIDs, UINT32* pNumEffects, HANDLE hEvent) override;

private:
    // 参数刷新（从共享内存读取）
    void RefreshParameters();
    
    // 增益处理
    void ApplyGain(float* buffer, size_t sampleCount, float gainLinear);
    
    // 硬限制（最后一道防线）
    void HardClip(float* buffer, size_t sampleCount);
    
    // 计算电平并写入 ring buffer
    void WriteLevelData(const float* buffer, size_t sampleCount, uint32_t channelCount);

    // 引用计数
    std::atomic<ULONG> m_refCount;
    
    // 音频格式
    UINT32 m_sampleRate;
    UINT32 m_channelCount;
    UINT32 m_bitsPerSample;
    
    // 参数（从 Named Pipe 接收）
    std::atomic<float> m_globalGainDb;
    std::atomic<float> m_thresholdDb;
    std::atomic<bool> m_limiterEnabled;
    
    // Per-app 增益表
    static const size_t MAX_APP_GAINS = 32;
    AppGainEntry m_appGains[MAX_APP_GAINS];
    std::atomic<uint32_t> m_appGainCount;
    
    // 限幅器
    SoftLimiter m_limiter;
    
    // 共享内存（电平数据）
    HANDLE m_hLevelShm;
    LevelRingBuffer* m_pLevelBuffer;
    
    // Named Pipe 监听线程
    HANDLE m_hPipeThread;
    volatile bool m_pipeThreadRunning;
    static DWORD WINAPI PipeListenerThread(LPVOID lpParam);
    
    // 预分配缓冲区（避免实时线程中分配内存）
    float* m_tempBuffer;
    size_t m_tempBufferSize;
};
