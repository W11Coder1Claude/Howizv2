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
 * DSP chain: HPF → LPF → EQ(3-band) → [NS] → OutputGain → Clamp → Mute
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
    int   nsMode          = 1;       // 0=Mild, 1=Medium, 2=Aggressive

    // Output
    float outputGain      = 1.0f;    // Linear (0.0-2.0)
    int   outputVolume    = 80;      // Codec volume (0-100)
    bool  outputMute      = true;    // MUTED by default (safety)
};

struct AudioLevels {
    float rmsLeft   = 0.0f;  // 0.0 - 1.0
    float rmsRight  = 0.0f;
    float peakLeft  = 0.0f;
    float peakRight = 0.0f;
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
    void setOutputGain(float gain);
    void setOutputVolume(int vol);
    void setMute(bool mute);

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
};
