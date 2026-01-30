/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Real-time audio processing engine for Howizard
 *
 * Runs a continuous mic→DSP→headphone pipeline on a dedicated FreeRTOS task (Core 1).
 * Bypasses the HAL audio API for direct BSP codec access (streaming, not batch).
 *
 * DSP chain: HPF → LPF → EQ(3-band) → [VoiceExclusion] → [NS] → [AGC] → OutputGain → Clamp → Mute
 */

struct AudioEngineParams {
    // Input
    float micGain         = 180.0f;  // ES7210 PGA (0-240)

    // Filters
    bool  hpfEnabled      = true;
    float hpfFrequency    = 80.0f;   // Hz (20-500)
    bool  lpfEnabled      = false;
    float lpfFrequency    = 18000.0f;// Hz (2000-20000)

    // EQ (3-band parametric, peaking filters)
    float eqLowGain       = 0.0f;    // dB (-12 to +12) @ 250 Hz
    float eqMidGain       = 0.0f;    // dB (-12 to +12) @ 1000 Hz
    float eqHighGain      = 0.0f;    // dB (-12 to +12) @ 4000 Hz

    // Noise Suppression (ESP-SR standalone NS)
    bool  nsEnabled       = false;
    int   nsMode          = 2;       // 0=Mild, 1=Medium, 2=Aggressive (default: aggressive)

    // AGC (ESP-SR Automatic Gain Control @ 16kHz)
    bool  agcEnabled           = false;
    int   agcMode              = 2;      // 0=Saturation, 1=Analog, 2=Digital, 3=Fixed
    int   agcCompressionGainDb = 9;      // 0–90 dB
    bool  agcLimiterEnabled    = true;   // Built-in limiter
    int   agcTargetLevelDbfs   = -3;     // 0 to -31 dBFS

    // Voice Exclusion (NLMS adaptive filter @ 16kHz, uses headset mic as reference)
    bool  veEnabled        = false;
    float veBlend          = 0.5f;     // 0.0–1.0: mix of original vs cleaned (reduced for stability)
    float veStepSize       = 0.08f;    // 0.01–1.0: NLMS adaptation rate (slower for transient rejection)
    int   veFilterLength   = 64;       // 16–512 taps (shorter for faster adaptation)
    float veMaxAttenuation = 0.6f;     // 0.0–1.0: safety limit (reduced for stability)

    // Voice Exclusion - Reference signal conditioning (applied to HP mic before NLMS)
    float veRefGain        = 0.5f;     // 0.1–5.0: reference signal gain multiplier (reduced)
    float veRefHpf         = 150.0f;   // 20–500 Hz: reference HPF (raised to reject footsteps)
    float veRefLpf         = 3000.0f;  // 1000–8000 Hz: reference LPF (tightened for voice)

    // Voice Exclusion - AEC mode (alternative to NLMS)
    int   veMode           = 0;        // 0=NLMS, 1=AEC
    int   veAecMode        = 1;        // 0=SR_LOW_COST, 1=SR_HIGH_PERF, 3=VOIP_LOW_COST, 4=VOIP_HIGH_PERF
    int   veAecFilterLen   = 4;        // 1–6 (AEC filter length parameter)
    bool  veVadEnabled     = true;     // VAD for double-talk detection (AEC mode)
    int   veVadMode        = 3;        // 0–4: Normal to Very Very Very Aggressive

    // Voice Exclusion - VAD Gating (attenuates output during non-speech)
    bool  veVadGateEnabled = true;     // Enable VAD-based gating
    float veVadGateAtten   = 0.15f;    // 0.0–1.0: attenuation during silence (0.15 = -16dB)

    // Output
    float outputGain      = 1.5f;    // Linear (0.0-6.0, extended for boost)
    int   outputVolume    = 100;     // Codec volume (0-100)
    bool  outputMute      = true;    // MUTED by default (safety)
    bool  boostEnabled    = false;   // Enable soft clipping for high gain levels
};

struct AudioLevels {
    float rmsLeft   = 0.0f;  // 0.0 - 1.0
    float rmsRight  = 0.0f;
    float peakLeft  = 0.0f;
    float peakRight = 0.0f;
    float rmsHP     = 0.0f;  // Headphone mic level (for VE reference monitoring)
    float peakHP    = 0.0f;
    bool  vadSpeechDetected = false;  // VAD state (true = speech detected)
};

class AudioEngine {
public:
    static AudioEngine& getInstance();

    void start();
    void stop();
    bool isRunning();

    // Thread-safe parameter access
    void setParams(const AudioEngineParams& p);
    AudioEngineParams getParams();
    AudioLevels getLevels();

    // Convenience setters
    void setMicGain(float gain);
    void setHpf(bool enabled, float freq);
    void setLpf(bool enabled, float freq);
    void setEqLow(float gainDb);
    void setEqMid(float gainDb);
    void setEqHigh(float gainDb);
    void setNsEnabled(bool enabled);
    void setNsMode(int mode);
    void setAgcEnabled(bool enabled);
    void setAgcMode(int mode);
    void setAgcCompressionGain(int gainDb);
    void setAgcLimiterEnabled(bool enabled);
    void setAgcTargetLevel(int levelDbfs);
    void setVeEnabled(bool enabled);
    void setVeBlend(float blend);
    void setVeStepSize(float stepSize);
    void setVeFilterLength(int taps);
    void setVeMaxAttenuation(float atten);
    void setVeRefGain(float gain);
    void setVeRefHpf(float freq);
    void setVeRefLpf(float freq);
    void setVeMode(int mode);
    void setVeAecMode(int mode);
    void setVeAecFilterLen(int len);
    void setVeVadEnabled(bool enabled);
    void setVeVadMode(int mode);
    void setOutputGain(float gain);
    void setOutputVolume(int vol);
    void setMute(bool mute);
    void setBoostEnabled(bool enabled);
    void setVeVadGateEnabled(bool enabled);
    void setVeVadGateAtten(float atten);

private:
    AudioEngine() = default;
    ~AudioEngine() = default;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Biquad filter (Direct Form II Transposed)
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process(float in);
        void reset();
    };

    void calcHpfCoeffs(Biquad& bq, float freq, float sampleRate);
    void calcLpfCoeffs(Biquad& bq, float freq, float sampleRate);
    void calcPeakEqCoeffs(Biquad& bq, float freq, float gainDb, float Q, float sampleRate);
    void recalcAllCoeffs();

    // FreeRTOS task
    static void audioTask(void* param);
    void processLoop();

    // Per-channel filter state (L and R)
    Biquad _hpfL, _hpfR;
    Biquad _lpfL, _lpfR;
    Biquad _eqLowL, _eqLowR;
    Biquad _eqMidL, _eqMidR;
    Biquad _eqHighL, _eqHighR;

    // VE reference signal conditioning filters (mono, applied to HP mic)
    Biquad _veRefHpfBq;
    Biquad _veRefLpfBq;

    // State
    AudioEngineParams _params;
    AudioLevels _levels;
    std::mutex _mutex;
    bool _running = false;
    bool _paramsChanged = true;  // Force initial coefficient calc
    TaskHandle_t _taskHandle = nullptr;

    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int BLOCK_SIZE = 480;      // ~10.0ms latency (480/48000 = 10ms)
    static constexpr int NUM_CHANNELS_IN = 4;   // MIC-L, AEC, MIC-R, MIC-HP
    static constexpr int NUM_CHANNELS_OUT = 2;  // Stereo
    static constexpr int NS_FRAME_16K = 160;    // 10ms @ 16kHz (480/3)

    // Peak hold decay factor per block (~300ms decay)
    static constexpr float PEAK_DECAY = 0.97f;

    // NS handles (opaque pointers, typed in .cpp via esp_ns.h)
    void* _nsHandleL = nullptr;
    void* _nsHandleR = nullptr;

    // AGC handles (opaque pointers, typed in .cpp via esp_agc.h)
    void* _agcHandleL = nullptr;
    void* _agcHandleR = nullptr;

    // NLMS Voice Exclusion filter instances (opaque, typed in .cpp)
    void* _nlmsL = nullptr;
    void* _nlmsR = nullptr;

    // AEC handles (opaque pointers, typed in .cpp via esp_aec.h)
    void* _aecHandleL = nullptr;
    void* _aecHandleR = nullptr;

    // VAD handle (opaque pointer, typed in .cpp via esp_vad.h)
    void* _vadHandleRef = nullptr;

    // AEC frame bridging constants
    static constexpr int AEC_FRAME_16K = 512;  // AEC needs 512 samples @ 16kHz (32ms)
};
