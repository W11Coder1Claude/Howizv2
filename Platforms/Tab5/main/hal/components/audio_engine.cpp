/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_engine.h"
#include <mooncake_log.h>
#include <bsp/m5stack_tab5.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <esp_heap_caps.h>

extern "C" {
#include <esp_ns.h>
#include <esp_agc.h>
#include <esp_aec.h>
#include <esp_vad.h>
}

static const char* TAG = "AudioEngine";

// ─────────────────────────────────────────────────────────────────────────────
// Polyphase Resampler (21-tap Kaiser-windowed sinc, ~70dB stopband)
// Ported from Boosted speech_dsp.cpp
// ─────────────────────────────────────────────────────────────────────────────

class Resampler {
public:
    static constexpr int FILTER_TAPS = 21;
    static constexpr int HALF_TAPS = FILTER_TAPS / 2;  // 10

    void init(bool downsample) {
        _downsample = downsample;
        static const float lpf_coeffs[FILTER_TAPS] = {
            -0.0029f, -0.0056f,  0.0000f,  0.0175f,  0.0303f,  0.0000f,
            -0.0657f, -0.1186f,  0.0000f,  0.3125f,  0.5002f,  0.3125f,
             0.0000f, -0.1186f, -0.0657f,  0.0000f,  0.0303f,  0.0175f,
             0.0000f, -0.0056f, -0.0029f
        };
        std::memcpy(_coeffs, lpf_coeffs, sizeof(lpf_coeffs));
        std::memset(_history, 0, sizeof(_history));
        std::memset(_upHistory, 0, sizeof(_upHistory));
    }

    void downsample3(const float* in, float* out, size_t outFrames) {
        size_t inFrames = outFrames * 3;
        for (size_t i = 0; i < outFrames; i++) {
            float sum = 0.0f;
            size_t inIdx = i * 3;
            for (int t = 0; t < FILTER_TAPS; t++) {
                int srcIdx = static_cast<int>(inIdx) - HALF_TAPS + t;
                float sample;
                if (srcIdx < 0) {
                    int histIdx = HALF_TAPS + srcIdx;
                    sample = (histIdx >= 0) ? _history[histIdx] : 0.0f;
                } else if (srcIdx < static_cast<int>(inFrames)) {
                    sample = in[srcIdx];
                } else {
                    sample = 0.0f;
                }
                sum += sample * _coeffs[t];
            }
            out[i] = sum;
        }
        if (inFrames >= static_cast<size_t>(HALF_TAPS)) {
            for (int i = 0; i < HALF_TAPS; i++) {
                _history[i] = in[inFrames - HALF_TAPS + i];
            }
        }
    }

    void upsample3(const float* in, float* out, size_t inFrames) {
        for (size_t i = 0; i < inFrames; i++) {
            for (int phase = 0; phase < 3; phase++) {
                float sum = 0.0f;
                for (int t = 0; t < 7; t++) {
                    int srcIdx = static_cast<int>(i) - 3 + t;
                    float sample;
                    if (srcIdx < 0) {
                        int histIdx = 3 + srcIdx;
                        sample = (histIdx >= 0) ? _upHistory[histIdx] : 0.0f;
                    } else if (srcIdx < static_cast<int>(inFrames)) {
                        sample = in[srcIdx];
                    } else {
                        sample = 0.0f;
                    }
                    int coeffIdx = t * 3 + phase;
                    if (coeffIdx < FILTER_TAPS) {
                        sum += sample * _coeffs[coeffIdx];
                    }
                }
                out[i * 3 + phase] = sum * 3.0f;
            }
        }
        if (inFrames >= 3) {
            for (int i = 0; i < 3; i++) {
                _upHistory[i] = in[inFrames - 3 + i];
            }
        }
    }

private:
    bool _downsample = true;
    float _coeffs[FILTER_TAPS] = {};
    float _history[HALF_TAPS] = {};
    float _upHistory[3] = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// NLMS Adaptive Filter (Voice Exclusion)
// ─────────────────────────────────────────────────────────────────────────────

class NlmsFilter {
public:
    void init(int filterLength) {
        destroy();
        _len = filterLength;
        _weights = new float[_len]();     // zero-initialized
        _refBuf = new float[_len]();      // circular reference buffer
        _pos = 0;
    }

    void destroy() {
        delete[] _weights;
        delete[] _refBuf;
        _weights = nullptr;
        _refBuf = nullptr;
        _len = 0;
        _pos = 0;
    }

    void reset() {
        if (_weights) std::memset(_weights, 0, _len * sizeof(float));
        if (_refBuf)  std::memset(_refBuf, 0, _len * sizeof(float));
        _pos = 0;
    }

    // Returns the voice estimate (what should be subtracted from primary).
    // Weight update uses the true (unclamped) error for correct convergence.
    float process(float ref, float primary, float stepSize) {
        if (!_weights || !_refBuf || _len <= 0) return 0.0f;

        // 1. Store reference sample in circular buffer
        _refBuf[_pos] = ref;

        // 2. Compute estimate = dot(weights, refBuffer)
        float estimate = 0.0f;
        float power = 0.0f;
        for (int i = 0; i < _len; i++) {
            int idx = (_pos - i + _len) % _len;
            float r = _refBuf[idx];
            estimate += _weights[i] * r;
            power += r * r;
        }

        // 3. True error for weight update (never clamped)
        float error = primary - estimate;

        // 4. Normalized step: stepSize / (power + floor)
        float normStep = stepSize / (power + 1e-6f);

        // 5. Update weights using true error
        for (int i = 0; i < _len; i++) {
            int idx = (_pos - i + _len) % _len;
            _weights[i] += normStep * error * _refBuf[idx];
            // Coefficient sanity check
            if (fabsf(_weights[i]) > 10.0f) {
                _weights[i] = 0.0f;
            }
        }

        // Advance circular buffer position
        _pos = (_pos + 1) % _len;

        // Return the estimate (caller subtracts with blend + attenuation clamp)
        return estimate;
    }

    bool isInitialized() const { return _weights != nullptr; }

private:
    float* _weights = nullptr;
    float* _refBuf = nullptr;
    int _len = 0;
    int _pos = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────

AudioEngine& AudioEngine::getInstance()
{
    static AudioEngine instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Biquad filter - Direct Form II Transposed
// ─────────────────────────────────────────────────────────────────────────────

float AudioEngine::Biquad::process(float in)
{
    float out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return out;
}

void AudioEngine::Biquad::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Coefficient calculations (Audio EQ Cookbook - Robert Bristow-Johnson)
// ─────────────────────────────────────────────────────────────────────────────

void AudioEngine::calcHpfCoeffs(Biquad& bq, float freq, float sampleRate)
{
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * 0.7071f);  // Q = 0.7071 (Butterworth)

    float a0 = 1.0f + alpha;
    bq.b0 = ((1.0f + cosw0) / 2.0f) / a0;
    bq.b1 = (-(1.0f + cosw0)) / a0;
    bq.b2 = ((1.0f + cosw0) / 2.0f) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
}

void AudioEngine::calcLpfCoeffs(Biquad& bq, float freq, float sampleRate)
{
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * 0.7071f);  // Q = 0.7071 (Butterworth)

    float a0 = 1.0f + alpha;
    bq.b0 = ((1.0f - cosw0) / 2.0f) / a0;
    bq.b1 = (1.0f - cosw0) / a0;
    bq.b2 = ((1.0f - cosw0) / 2.0f) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
}

void AudioEngine::calcPeakEqCoeffs(Biquad& bq, float freq, float gainDb, float Q, float sampleRate)
{
    if (fabsf(gainDb) < 0.1f) {
        // Unity gain - bypass
        bq.b0 = 1.0f; bq.b1 = 0.0f; bq.b2 = 0.0f;
        bq.a1 = 0.0f; bq.a2 = 0.0f;
        return;
    }

    float A = powf(10.0f, gainDb / 40.0f);  // sqrt of linear gain
    float w0 = 2.0f * M_PI * freq / sampleRate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha / A;
    bq.b0 = (1.0f + alpha * A) / a0;
    bq.b1 = (-2.0f * cosw0) / a0;
    bq.b2 = (1.0f - alpha * A) / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha / A) / a0;
}

void AudioEngine::recalcAllCoeffs()
{
    // HPF
    calcHpfCoeffs(_hpfL, _params.hpfFrequency, SAMPLE_RATE);
    calcHpfCoeffs(_hpfR, _params.hpfFrequency, SAMPLE_RATE);

    // LPF
    calcLpfCoeffs(_lpfL, _params.lpfFrequency, SAMPLE_RATE);
    calcLpfCoeffs(_lpfR, _params.lpfFrequency, SAMPLE_RATE);

    // EQ bands (Q = 1.4 for musical EQ)
    calcPeakEqCoeffs(_eqLowL, 250.0f, _params.eqLowGain, 1.4f, SAMPLE_RATE);
    calcPeakEqCoeffs(_eqLowR, 250.0f, _params.eqLowGain, 1.4f, SAMPLE_RATE);

    calcPeakEqCoeffs(_eqMidL, 1000.0f, _params.eqMidGain, 1.4f, SAMPLE_RATE);
    calcPeakEqCoeffs(_eqMidR, 1000.0f, _params.eqMidGain, 1.4f, SAMPLE_RATE);

    calcPeakEqCoeffs(_eqHighL, 4000.0f, _params.eqHighGain, 1.4f, SAMPLE_RATE);
    calcPeakEqCoeffs(_eqHighR, 4000.0f, _params.eqHighGain, 1.4f, SAMPLE_RATE);

    // VE reference signal conditioning filters (mono, applied to HP mic at 48kHz)
    calcHpfCoeffs(_veRefHpfBq, _params.veRefHpf, SAMPLE_RATE);
    calcLpfCoeffs(_veRefLpfBq, _params.veRefLpf, SAMPLE_RATE);
}

// ─────────────────────────────────────────────────────────────────────────────
// NS handle management
// ─────────────────────────────────────────────────────────────────────────────

static void destroyNsHandles(void*& handleL, void*& handleR)
{
    if (handleL) {
        ns_destroy(static_cast<ns_handle_t>(handleL));
        handleL = nullptr;
    }
    if (handleR) {
        ns_destroy(static_cast<ns_handle_t>(handleR));
        handleR = nullptr;
    }
}

static void createNsHandles(void*& handleL, void*& handleR, int mode)
{
    handleL = ns_pro_create(10, mode, 16000);
    handleR = ns_pro_create(10, mode, 16000);
    if (!handleL || !handleR) {
        mclog::tagError("AudioEngine", "failed to create NS handles (mode={})", mode);
        destroyNsHandles(handleL, handleR);
    } else {
        mclog::tagInfo("AudioEngine", "NS handles created (mode={})", mode);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AGC handle management
// ─────────────────────────────────────────────────────────────────────────────

static void destroyAgcHandles(void*& handleL, void*& handleR)
{
    if (handleL) {
        esp_agc_close(handleL);
        handleL = nullptr;
    }
    if (handleR) {
        esp_agc_close(handleR);
        handleR = nullptr;
    }
}

static void createAgcHandles(void*& handleL, void*& handleR, int mode)
{
    handleL = esp_agc_open(static_cast<agc_mode_t>(mode), 16000);
    handleR = esp_agc_open(static_cast<agc_mode_t>(mode), 16000);
    if (!handleL || !handleR) {
        mclog::tagError("AudioEngine", "failed to create AGC handles (mode={})", mode);
        destroyAgcHandles(handleL, handleR);
    } else {
        mclog::tagInfo("AudioEngine", "AGC handles created (mode={})", mode);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AEC handle management
// ─────────────────────────────────────────────────────────────────────────────

static void destroyAecHandles(void*& handleL, void*& handleR)
{
    if (handleL) {
        aec_destroy(static_cast<aec_handle_t*>(handleL));
        handleL = nullptr;
    }
    if (handleR) {
        aec_destroy(static_cast<aec_handle_t*>(handleR));
        handleR = nullptr;
    }
}

static void createAecHandles(void*& handleL, void*& handleR, int aecMode, int filterLen)
{
    // ESP-SR AEC: aec_create(sample_rate, filter_length, channel_num, mode)
    // Each handle processes one mono channel
    handleL = aec_create(16000, filterLen, 1, static_cast<aec_mode_t>(aecMode));
    handleR = aec_create(16000, filterLen, 1, static_cast<aec_mode_t>(aecMode));
    if (!handleL || !handleR) {
        mclog::tagError("AudioEngine", "failed to create AEC handles (mode={}, flen={})", aecMode, filterLen);
        destroyAecHandles(handleL, handleR);
    } else {
        mclog::tagInfo("AudioEngine", "AEC handles created (mode={}, flen={})", aecMode, filterLen);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VAD handle management
// ─────────────────────────────────────────────────────────────────────────────

static void destroyVadHandle(void*& handle)
{
    if (handle) {
        vad_destroy(static_cast<vad_handle_t>(handle));
        handle = nullptr;
    }
}

static void createVadHandle(void*& handle, int vadMode)
{
    vad_handle_t h = vad_create(static_cast<vad_mode_t>(vadMode));
    handle = h;
    if (!handle) {
        mclog::tagError("AudioEngine", "failed to create VAD handle (mode={})", vadMode);
    } else {
        mclog::tagInfo("AudioEngine", "VAD handle created (mode={})", vadMode);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AEC Ring Buffer (accumulate 160-sample blocks → 512-sample AEC frames)
// ─────────────────────────────────────────────────────────────────────────────

struct AecRingBuf {
    float buf[512];
    int writePos = 0;

    void reset() { writePos = 0; memset(buf, 0, sizeof(buf)); }

    void push(const float* data, int count) {
        for (int i = 0; i < count && writePos < 512; i++) {
            buf[writePos++] = data[i];
        }
    }

    bool ready() const { return writePos >= 512; }

    void consume() { writePos = 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Start / Stop
// ─────────────────────────────────────────────────────────────────────────────

void AudioEngine::start()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_running) {
        mclog::tagWarn(TAG, "already running");
        return;
    }

    mclog::tagInfo(TAG, "starting audio engine");
    _running = true;
    _paramsChanged = true;

    // Disable speaker amplifier to prevent feedback (headphone-only output)
    bsp_speaker_enable(false);

    // Reset filter state
    _hpfL.reset(); _hpfR.reset();
    _lpfL.reset(); _lpfR.reset();
    _eqLowL.reset(); _eqLowR.reset();
    _eqMidL.reset(); _eqMidR.reset();
    _eqHighL.reset(); _eqHighR.reset();
    _veRefHpfBq.reset(); _veRefLpfBq.reset();

    // Create NS handles if NS is enabled
    if (_params.nsEnabled) {
        createNsHandles(_nsHandleL, _nsHandleR, _params.nsMode);
    }

    // Create AGC handles if AGC is enabled
    if (_params.agcEnabled) {
        createAgcHandles(_agcHandleL, _agcHandleR, _params.agcMode);
    }

    xTaskCreatePinnedToCore(audioTask, "audio_eng", 32768, this, 10, &_taskHandle, 1);
}

void AudioEngine::stop()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_running) return;
        _running = false;
    }

    // Wait for task to exit
    if (_taskHandle) {
        // Give the task time to notice _running=false and exit
        vTaskDelay(pdMS_TO_TICKS(50));
        _taskHandle = nullptr;
    }

    // Destroy NS handles
    destroyNsHandles(_nsHandleL, _nsHandleR);

    // Destroy AGC handles
    destroyAgcHandles(_agcHandleL, _agcHandleR);

    // Destroy NLMS filters
    if (_nlmsL) {
        static_cast<NlmsFilter*>(_nlmsL)->destroy();
        delete static_cast<NlmsFilter*>(_nlmsL);
        _nlmsL = nullptr;
    }
    if (_nlmsR) {
        static_cast<NlmsFilter*>(_nlmsR)->destroy();
        delete static_cast<NlmsFilter*>(_nlmsR);
        _nlmsR = nullptr;
    }

    // Destroy AEC handles
    destroyAecHandles(_aecHandleL, _aecHandleR);

    // Destroy VAD handle
    destroyVadHandle(_vadHandleRef);

    // Mute codec output
    bsp_codec_config_t* codec = bsp_get_codec_handle();
    if (codec) {
        codec->set_mute(true);
    }

    // Re-enable speaker amplifier for normal use
    bsp_speaker_enable(true);

    mclog::tagInfo(TAG, "audio engine stopped");
}

bool AudioEngine::isRunning()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _running;
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread-safe parameter access
// ─────────────────────────────────────────────────────────────────────────────

void AudioEngine::setParams(const AudioEngineParams& p)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params = p;
    _paramsChanged = true;
}

AudioEngineParams AudioEngine::getParams()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _params;
}

AudioLevels AudioEngine::getLevels()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _levels;
}

void AudioEngine::setMicGain(float gain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.micGain = std::clamp(gain, 0.0f, 240.0f);
    _paramsChanged = true;
}

void AudioEngine::setHpf(bool enabled, float freq)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.hpfEnabled = enabled;
    _params.hpfFrequency = std::clamp(freq, 20.0f, 2000.0f);
    _paramsChanged = true;
}

void AudioEngine::setLpf(bool enabled, float freq)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.lpfEnabled = enabled;
    _params.lpfFrequency = std::clamp(freq, 500.0f, 20000.0f);
    _paramsChanged = true;
}

void AudioEngine::setEqLow(float gainDb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.eqLowGain = std::clamp(gainDb, -12.0f, 12.0f);
    _paramsChanged = true;
}

void AudioEngine::setEqMid(float gainDb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.eqMidGain = std::clamp(gainDb, -12.0f, 12.0f);
    _paramsChanged = true;
}

void AudioEngine::setEqHigh(float gainDb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.eqHighGain = std::clamp(gainDb, -12.0f, 12.0f);
    _paramsChanged = true;
}

void AudioEngine::setNsEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.nsEnabled = enabled;
    _paramsChanged = true;
}

void AudioEngine::setNsMode(int mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.nsMode = std::clamp(mode, 0, 2);
    _paramsChanged = true;
}

void AudioEngine::setAgcEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.agcEnabled = enabled;
    _paramsChanged = true;
}

void AudioEngine::setAgcMode(int mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.agcMode = std::clamp(mode, 0, 3);
    _paramsChanged = true;
}

void AudioEngine::setAgcCompressionGain(int gainDb)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.agcCompressionGainDb = std::clamp(gainDb, 0, 90);
    _paramsChanged = true;
}

void AudioEngine::setAgcLimiterEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.agcLimiterEnabled = enabled;
    _paramsChanged = true;
}

void AudioEngine::setAgcTargetLevel(int levelDbfs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.agcTargetLevelDbfs = std::clamp(levelDbfs, -31, 0);
    _paramsChanged = true;
}

void AudioEngine::setVeEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veEnabled = enabled;
    _paramsChanged = true;
}

void AudioEngine::setVeBlend(float blend)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veBlend = std::clamp(blend, 0.0f, 1.0f);
    _paramsChanged = true;
}

void AudioEngine::setVeStepSize(float stepSize)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veStepSize = std::clamp(stepSize, 0.01f, 1.0f);
    _paramsChanged = true;
}

void AudioEngine::setVeFilterLength(int taps)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veFilterLength = std::clamp(taps, 16, 512);
    _paramsChanged = true;
}

void AudioEngine::setVeMaxAttenuation(float atten)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veMaxAttenuation = std::clamp(atten, 0.0f, 1.0f);
    _paramsChanged = true;
}

void AudioEngine::setVeRefGain(float gain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veRefGain = std::clamp(gain, 0.1f, 5.0f);
    _paramsChanged = true;
}

void AudioEngine::setVeRefHpf(float freq)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veRefHpf = std::clamp(freq, 20.0f, 500.0f);
    _paramsChanged = true;
}

void AudioEngine::setVeRefLpf(float freq)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veRefLpf = std::clamp(freq, 1000.0f, 8000.0f);
    _paramsChanged = true;
}

void AudioEngine::setVeMode(int mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veMode = std::clamp(mode, 0, 1);
    _paramsChanged = true;
}

void AudioEngine::setVeAecMode(int mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veAecMode = mode;
    _paramsChanged = true;
}

void AudioEngine::setVeAecFilterLen(int len)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veAecFilterLen = std::clamp(len, 1, 6);
    _paramsChanged = true;
}

void AudioEngine::setVeVadEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veVadEnabled = enabled;
    _paramsChanged = true;
}

void AudioEngine::setVeVadMode(int mode)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.veVadMode = std::clamp(mode, 0, 4);
    _paramsChanged = true;
}

void AudioEngine::setOutputGain(float gain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.outputGain = std::clamp(gain, 0.0f, 4.0f);
    _paramsChanged = true;
}

void AudioEngine::setOutputVolume(int vol)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.outputVolume = std::clamp(vol, 0, 100);
    _paramsChanged = true;
}

void AudioEngine::setMute(bool mute)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.outputMute = mute;
    _paramsChanged = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio processing task (runs on Core 1)
// ─────────────────────────────────────────────────────────────────────────────

void AudioEngine::audioTask(void* param)
{
    auto* self = static_cast<AudioEngine*>(param);
    self->processLoop();
    vTaskDelete(nullptr);
}

void AudioEngine::processLoop()
{
    mclog::tagInfo(TAG, "audio task started on core {}", xPortGetCoreID());

    bsp_codec_config_t* codec = bsp_get_codec_handle();
    if (!codec) {
        mclog::tagError(TAG, "failed to get codec handle");
        std::lock_guard<std::mutex> lock(_mutex);
        _running = false;
        return;
    }

    // Configure codec for 48kHz stereo output
    codec->i2s_reconfig_clk_fn(SAMPLE_RATE, 16, I2S_SLOT_MODE_STEREO);
    codec->set_volume(100);
    codec->set_mute(true);  // Start muted

    // Allocate I/O buffers
    const size_t inBufBytes = BLOCK_SIZE * NUM_CHANNELS_IN * sizeof(int16_t);
    const size_t outBufBytes = BLOCK_SIZE * NUM_CHANNELS_OUT * sizeof(int16_t);

    int16_t* inBuf = new int16_t[BLOCK_SIZE * NUM_CHANNELS_IN];
    int16_t* outBuf = new int16_t[BLOCK_SIZE * NUM_CHANNELS_OUT];
    float* floatL = new float[BLOCK_SIZE];
    float* floatR = new float[BLOCK_SIZE];
    float* floatHP = new float[BLOCK_SIZE];  // Headphone mic (CH3) for voice exclusion

    // NS processing buffers (16kHz domain)
    float* down16kL = new float[NS_FRAME_16K];
    float* down16kR = new float[NS_FRAME_16K];
    int16_t* ns16kIn = new int16_t[NS_FRAME_16K];
    int16_t* ns16kOut = new int16_t[NS_FRAME_16K];
    float* up48kL = new float[BLOCK_SIZE];
    float* up48kR = new float[BLOCK_SIZE];

    // NS Resamplers
    Resampler resamplerDownL, resamplerDownR;
    Resampler resamplerUpL, resamplerUpR;
    resamplerDownL.init(true);
    resamplerDownR.init(true);
    resamplerUpL.init(false);
    resamplerUpR.init(false);

    // VE Resamplers (separate state from NS resamplers)
    Resampler veResDownL, veResDownR, veResDownHP;
    Resampler veResUpL, veResUpR;
    veResDownL.init(true);
    veResDownR.init(true);
    veResDownHP.init(true);
    veResUpL.init(false);
    veResUpR.init(false);

    // VE 16kHz processing buffers
    float* veDown16kL  = new float[NS_FRAME_16K];  // 160 samples
    float* veDown16kR  = new float[NS_FRAME_16K];
    float* veDown16kHP = new float[NS_FRAME_16K];
    float* veEst16kL   = new float[NS_FRAME_16K];  // NLMS estimates at 16kHz
    float* veEst16kR   = new float[NS_FRAME_16K];
    float* veEstUp48kL = new float[BLOCK_SIZE];     // Upsampled estimates
    float* veEstUp48kR = new float[BLOCK_SIZE];

    // AEC ring buffers (accumulate 160→512 sample frames for AEC processing)
    AecRingBuf aecRingL, aecRingR, aecRingHP;
    AecRingBuf aecOutRingL, aecOutRingR;  // Output ring buffers
    aecRingL.reset(); aecRingR.reset(); aecRingHP.reset();
    aecOutRingL.reset(); aecOutRingR.reset();

    // AEC 16kHz I/O buffers (512 samples, aligned for ESP-SR)
    int16_t* aec16kInL  = static_cast<int16_t*>(heap_caps_aligned_alloc(16, AEC_FRAME_16K * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t* aec16kInR  = static_cast<int16_t*>(heap_caps_aligned_alloc(16, AEC_FRAME_16K * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t* aec16kRef  = static_cast<int16_t*>(heap_caps_aligned_alloc(16, AEC_FRAME_16K * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t* aec16kOutL = static_cast<int16_t*>(heap_caps_aligned_alloc(16, AEC_FRAME_16K * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t* aec16kOutR = static_cast<int16_t*>(heap_caps_aligned_alloc(16, AEC_FRAME_16K * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    // VAD buffer (uses the AEC frame size for analysis)
    int16_t* vad16kBuf = static_cast<int16_t*>(heap_caps_aligned_alloc(16, AEC_FRAME_16K * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    // AEC output float buffers for upsampling
    float* aecFloat16kL = new float[AEC_FRAME_16K];
    float* aecFloat16kR = new float[AEC_FRAME_16K];

    // AEC Resamplers (separate from VE NLMS resamplers)
    Resampler aecResDownL, aecResDownR, aecResDownHP;
    Resampler aecResUpL, aecResUpR;
    aecResDownL.init(true);
    aecResDownR.init(true);
    aecResDownHP.init(true);
    aecResUpL.init(false);
    aecResUpR.init(false);

    // AEC 16kHz downsampled buffers per block (160 samples from 480 @ 48kHz)
    float* aecDown16kBlockL  = new float[NS_FRAME_16K];
    float* aecDown16kBlockR  = new float[NS_FRAME_16K];
    float* aecDown16kBlockHP = new float[NS_FRAME_16K];

    // AGC Resamplers (separate state from NS/VE resamplers)
    Resampler agcResDownL, agcResDownR;
    Resampler agcResUpL, agcResUpR;
    agcResDownL.init(true);
    agcResDownR.init(true);
    agcResUpL.init(false);
    agcResUpR.init(false);

    // AGC 16kHz processing buffers
    float* agcDown16kL = new float[NS_FRAME_16K];   // 160 samples
    float* agcDown16kR = new float[NS_FRAME_16K];
    int16_t* agc16kIn  = new int16_t[NS_FRAME_16K];
    int16_t* agc16kOut = new int16_t[NS_FRAME_16K];

    mclog::tagInfo(TAG, "buffers allocated: in={}B out={}B", inBufBytes, outBufBytes);

    // Local copy of params to minimize lock time
    AudioEngineParams localParams;
    bool localParamsChanged = true;
    bool prevNsEnabled = false;
    int prevNsMode = -1;
    bool prevVeEnabled = false;
    int prevVeFilterLength = -1;
    bool prevAgcEnabled = false;
    int prevAgcMode = -1;
    int prevAgcCompressionGainDb = -1;
    bool prevAgcLimiterEnabled = true;
    int prevAgcTargetLevelDbfs = -99;
    bool prevVeAecActive = false;  // Track AEC mode activation
    int prevVeAecMode = -1;
    int prevVeAecFilterLen = -1;
    bool prevVeVadEnabled = false;
    int prevVeVadMode = -1;
    bool hpDetected = false;
    int hpDetectCounter = 0;
    static constexpr int HP_DETECT_INTERVAL = 48;  // check every ~48 blocks (~480ms)
    bool aecOutputReady = false;  // True once AEC has produced its first output frame

    while (true) {
        // Check if we should stop
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_running) break;

            // Snapshot params
            if (_paramsChanged) {
                localParams = _params;
                localParamsChanged = true;
                _paramsChanged = false;
            }
        }

        // Recalculate filter coefficients if params changed
        if (localParamsChanged) {
            localParamsChanged = false;

            // Apply mic gain
            codec->set_in_gain(localParams.micGain);

            // Apply codec volume
            codec->set_volume(localParams.outputVolume);

            // Apply mute
            codec->set_mute(localParams.outputMute);

            // Handle NS enable/mode changes
            if (localParams.nsEnabled != prevNsEnabled || localParams.nsMode != prevNsMode) {
                if (localParams.nsEnabled) {
                    // Recreate NS handles with new mode
                    destroyNsHandles(_nsHandleL, _nsHandleR);
                    createNsHandles(_nsHandleL, _nsHandleR, localParams.nsMode);
                } else {
                    destroyNsHandles(_nsHandleL, _nsHandleR);
                }
                prevNsEnabled = localParams.nsEnabled;
                prevNsMode = localParams.nsMode;
            }

            // Handle VE enable/filter-length changes
            if (localParams.veEnabled != prevVeEnabled ||
                localParams.veFilterLength != prevVeFilterLength) {
                if (localParams.veEnabled) {
                    // Destroy old and create new NLMS filters
                    if (_nlmsL) {
                        static_cast<NlmsFilter*>(_nlmsL)->destroy();
                        delete static_cast<NlmsFilter*>(_nlmsL);
                    }
                    if (_nlmsR) {
                        static_cast<NlmsFilter*>(_nlmsR)->destroy();
                        delete static_cast<NlmsFilter*>(_nlmsR);
                    }
                    auto* nlL = new NlmsFilter();
                    auto* nlR = new NlmsFilter();
                    nlL->init(localParams.veFilterLength);
                    nlR->init(localParams.veFilterLength);
                    _nlmsL = nlL;
                    _nlmsR = nlR;
                    mclog::tagInfo(TAG, "NLMS filters created (taps={})", localParams.veFilterLength);
                } else {
                    if (_nlmsL) {
                        static_cast<NlmsFilter*>(_nlmsL)->destroy();
                        delete static_cast<NlmsFilter*>(_nlmsL);
                        _nlmsL = nullptr;
                    }
                    if (_nlmsR) {
                        static_cast<NlmsFilter*>(_nlmsR)->destroy();
                        delete static_cast<NlmsFilter*>(_nlmsR);
                        _nlmsR = nullptr;
                    }
                }
                prevVeEnabled = localParams.veEnabled;
                prevVeFilterLength = localParams.veFilterLength;
            }

            // Handle AEC enable/mode changes (when VE enabled in AEC mode)
            {
                bool aecWanted = localParams.veEnabled && localParams.veMode == 1;
                if (aecWanted != prevVeAecActive ||
                    localParams.veAecMode != prevVeAecMode ||
                    localParams.veAecFilterLen != prevVeAecFilterLen) {
                    if (aecWanted) {
                        destroyAecHandles(_aecHandleL, _aecHandleR);
                        createAecHandles(_aecHandleL, _aecHandleR, localParams.veAecMode, localParams.veAecFilterLen);
                        // Reset ring buffers when recreating AEC
                        aecRingL.reset(); aecRingR.reset(); aecRingHP.reset();
                        aecOutRingL.reset(); aecOutRingR.reset();
                        aecOutputReady = false;
                    } else {
                        destroyAecHandles(_aecHandleL, _aecHandleR);
                    }
                    prevVeAecActive = aecWanted;
                    prevVeAecMode = localParams.veAecMode;
                    prevVeAecFilterLen = localParams.veAecFilterLen;
                }
            }

            // Handle VAD enable/mode changes
            {
                bool vadWanted = localParams.veEnabled && localParams.veMode == 1 && localParams.veVadEnabled;
                if (vadWanted != prevVeVadEnabled || localParams.veVadMode != prevVeVadMode) {
                    if (vadWanted) {
                        destroyVadHandle(_vadHandleRef);
                        createVadHandle(_vadHandleRef, localParams.veVadMode);
                    } else {
                        destroyVadHandle(_vadHandleRef);
                    }
                    prevVeVadEnabled = vadWanted;
                    prevVeVadMode = localParams.veVadMode;
                }
            }

            // Handle AGC enable/config changes
            if (localParams.agcEnabled != prevAgcEnabled ||
                localParams.agcMode != prevAgcMode ||
                localParams.agcCompressionGainDb != prevAgcCompressionGainDb ||
                localParams.agcLimiterEnabled != prevAgcLimiterEnabled ||
                localParams.agcTargetLevelDbfs != prevAgcTargetLevelDbfs) {
                if (localParams.agcEnabled) {
                    // Recreate AGC handles with new config
                    destroyAgcHandles(_agcHandleL, _agcHandleR);
                    createAgcHandles(_agcHandleL, _agcHandleR, localParams.agcMode);
                    // Configure AGC parameters on each handle
                    if (_agcHandleL) {
                        set_agc_config(_agcHandleL,
                            localParams.agcCompressionGainDb,
                            localParams.agcLimiterEnabled ? 1 : 0,
                            localParams.agcTargetLevelDbfs);
                    }
                    if (_agcHandleR) {
                        set_agc_config(_agcHandleR,
                            localParams.agcCompressionGainDb,
                            localParams.agcLimiterEnabled ? 1 : 0,
                            localParams.agcTargetLevelDbfs);
                    }
                } else {
                    destroyAgcHandles(_agcHandleL, _agcHandleR);
                }
                prevAgcEnabled = localParams.agcEnabled;
                prevAgcMode = localParams.agcMode;
                prevAgcCompressionGainDb = localParams.agcCompressionGainDb;
                prevAgcLimiterEnabled = localParams.agcLimiterEnabled;
                prevAgcTargetLevelDbfs = localParams.agcTargetLevelDbfs;
            }

            // Recalculate all biquad coefficients
            recalcAllCoeffs();

            mclog::tagInfo(TAG, "params updated: micGain={:.0f} vol={} mute={} hpf={}/{:.0f}Hz lpf={}/{:.0f}Hz eq={:.1f}/{:.1f}/{:.1f}dB ns={}/mode={} ve={}/blend={:.2f} gain={:.2f}",
                localParams.micGain, localParams.outputVolume, localParams.outputMute,
                localParams.hpfEnabled, localParams.hpfFrequency,
                localParams.lpfEnabled, localParams.lpfFrequency,
                localParams.eqLowGain, localParams.eqMidGain, localParams.eqHighGain,
                localParams.nsEnabled, localParams.nsMode,
                localParams.veEnabled, localParams.veBlend,
                localParams.outputGain);
        }

        // ── 1. Read from I2S (4-channel input) ──
        size_t bytesRead = 0;
        codec->i2s_read(inBuf, inBufBytes, &bytesRead, portMAX_DELAY);

        int samplesRead = bytesRead / (NUM_CHANNELS_IN * sizeof(int16_t));
        if (samplesRead <= 0) continue;

        // ── 2. Extract MIC-L (ch0), MIC-R (ch2), MIC-HP (ch3), convert to float [-1.0, 1.0] ──
        constexpr float scale = 1.0f / 32768.0f;
        for (int i = 0; i < samplesRead; i++) {
            floatL[i]  = (float)inBuf[i * NUM_CHANNELS_IN + 0] * scale;  // MIC-L
            floatR[i]  = (float)inBuf[i * NUM_CHANNELS_IN + 2] * scale;  // MIC-R
            floatHP[i] = (float)inBuf[i * NUM_CHANNELS_IN + 3] * scale;  // MIC-HP
        }

        // ── 3. Apply HPF ──
        if (localParams.hpfEnabled) {
            for (int i = 0; i < samplesRead; i++) {
                floatL[i] = _hpfL.process(floatL[i]);
                floatR[i] = _hpfR.process(floatR[i]);
            }
        }

        // ── 4. Apply LPF ──
        if (localParams.lpfEnabled) {
            for (int i = 0; i < samplesRead; i++) {
                floatL[i] = _lpfL.process(floatL[i]);
                floatR[i] = _lpfR.process(floatR[i]);
            }
        }

        // ── 5. Apply 3-band EQ ──
        for (int i = 0; i < samplesRead; i++) {
            floatL[i] = _eqLowL.process(floatL[i]);
            floatR[i] = _eqLowR.process(floatR[i]);
        }
        for (int i = 0; i < samplesRead; i++) {
            floatL[i] = _eqMidL.process(floatL[i]);
            floatR[i] = _eqMidR.process(floatR[i]);
        }
        for (int i = 0; i < samplesRead; i++) {
            floatL[i] = _eqHighL.process(floatL[i]);
            floatR[i] = _eqHighR.process(floatR[i]);
        }

        // ── 6. Reference signal conditioning (applied to HP mic before VE) ──
        // Apply gain, HPF, LPF to the reference signal so NLMS sees clean voice
        {
            float refGain = localParams.veRefGain;
            for (int i = 0; i < samplesRead; i++) {
                floatHP[i] *= refGain;
            }
            // Bandpass: HPF removes rumble/handling, LPF focuses on voice band
            for (int i = 0; i < samplesRead; i++) {
                floatHP[i] = _veRefHpfBq.process(floatHP[i]);
            }
            for (int i = 0; i < samplesRead; i++) {
                floatHP[i] = _veRefLpfBq.process(floatHP[i]);
            }
        }

        // ── 6b. HP mic level metering ──
        {
            float sumHP = 0.0f, pkHP = 0.0f;
            for (int i = 0; i < samplesRead; i++) {
                float absHP = fabsf(floatHP[i]);
                sumHP += floatHP[i] * floatHP[i];
                if (absHP > pkHP) pkHP = absHP;
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _levels.rmsHP = sqrtf(sumHP / (float)samplesRead);
            _levels.peakHP = std::max(pkHP, _levels.peakHP * PEAK_DECAY);
        }

        // ── 7. Voice Exclusion (NLMS or AEC @ 16kHz, uses conditioned HP mic as ref) ──
        // Poll headphone detect periodically (not every sample)
        if (++hpDetectCounter >= HP_DETECT_INTERVAL) {
            hpDetectCounter = 0;
            hpDetected = bsp_headphone_detect();
        }

        if (localParams.veEnabled && hpDetected && samplesRead == BLOCK_SIZE) {
            float blend = localParams.veBlend;
            float maxAtt = localParams.veMaxAttenuation;

            if (localParams.veMode == 0 && _nlmsL && _nlmsR) {
                // ── NLMS mode (existing, unchanged) ──
                float step = localParams.veStepSize;

                veResDownL.downsample3(floatL, veDown16kL, NS_FRAME_16K);
                veResDownR.downsample3(floatR, veDown16kR, NS_FRAME_16K);
                veResDownHP.downsample3(floatHP, veDown16kHP, NS_FRAME_16K);

                auto* nlL = static_cast<NlmsFilter*>(_nlmsL);
                auto* nlR = static_cast<NlmsFilter*>(_nlmsR);

                for (int i = 0; i < NS_FRAME_16K; i++) {
                    veEst16kL[i] = nlL->process(veDown16kHP[i], veDown16kL[i], step);
                    veEst16kR[i] = nlR->process(veDown16kHP[i], veDown16kR[i], step);
                }

                veResUpL.upsample3(veEst16kL, veEstUp48kL, NS_FRAME_16K);
                veResUpR.upsample3(veEst16kR, veEstUp48kR, NS_FRAME_16K);

                for (int i = 0; i < samplesRead; i++) {
                    float maxRemL = fabsf(floatL[i]) * maxAtt;
                    float maxRemR = fabsf(floatR[i]) * maxAtt;
                    float remL = std::clamp(veEstUp48kL[i], -maxRemL, maxRemL);
                    float remR = std::clamp(veEstUp48kR[i], -maxRemR, maxRemR);
                    floatL[i] -= blend * remL;
                    floatR[i] -= blend * remR;
                    if (std::isnan(floatL[i])) floatL[i] = 0.0f;
                    if (std::isnan(floatR[i])) floatR[i] = 0.0f;
                }

            } else if (localParams.veMode == 1 && _aecHandleL && _aecHandleR) {
                // ── AEC mode (ring buffer → 512-sample frames) ──
                constexpr float i16scale = 1.0f / 32768.0f;

                // Downsample this block to 16kHz (480→160)
                aecResDownL.downsample3(floatL, aecDown16kBlockL, NS_FRAME_16K);
                aecResDownR.downsample3(floatR, aecDown16kBlockR, NS_FRAME_16K);
                aecResDownHP.downsample3(floatHP, aecDown16kBlockHP, NS_FRAME_16K);

                // Push 160 samples into ring buffers
                aecRingL.push(aecDown16kBlockL, NS_FRAME_16K);
                aecRingR.push(aecDown16kBlockR, NS_FRAME_16K);
                aecRingHP.push(aecDown16kBlockHP, NS_FRAME_16K);

                // When we have 512 samples, process one AEC frame
                if (aecRingL.ready() && aecRingR.ready() && aecRingHP.ready()) {
                    // Convert float→int16 for AEC
                    for (int i = 0; i < AEC_FRAME_16K; i++) {
                        float sL = std::clamp(aecRingL.buf[i], -1.0f, 1.0f);
                        float sR = std::clamp(aecRingR.buf[i], -1.0f, 1.0f);
                        float sHP = std::clamp(aecRingHP.buf[i], -1.0f, 1.0f);
                        aec16kInL[i] = static_cast<int16_t>(sL * 32767.0f);
                        aec16kInR[i] = static_cast<int16_t>(sR * 32767.0f);
                        aec16kRef[i] = static_cast<int16_t>(sHP * 32767.0f);
                    }

                    // Run VAD on reference signal if enabled
                    if (_vadHandleRef) {
                        // VAD processes 30ms frames (480 samples @ 16kHz)
                        // We'll check the first 480 samples of our 512-sample buffer
                        vad_state_t vadState = vad_process(
                            static_cast<vad_handle_t>(_vadHandleRef),
                            aec16kRef, 16000, 30);
                        std::lock_guard<std::mutex> lock(_mutex);
                        _levels.vadSpeechDetected = (vadState == VAD_SPEECH);
                    }

                    // Process AEC: input signal + reference → cleaned output
                    aec_process(static_cast<aec_handle_t*>(_aecHandleL),
                                aec16kInL, aec16kRef, aec16kOutL);
                    aec_process(static_cast<aec_handle_t*>(_aecHandleR),
                                aec16kInR, aec16kRef, aec16kOutR);

                    // Convert AEC output to float and store in output ring buffers
                    for (int i = 0; i < AEC_FRAME_16K; i++) {
                        aecOutRingL.buf[i] = static_cast<float>(aec16kOutL[i]) * i16scale;
                        aecOutRingR.buf[i] = static_cast<float>(aec16kOutR[i]) * i16scale;
                    }
                    aecOutRingL.writePos = AEC_FRAME_16K;
                    aecOutRingR.writePos = AEC_FRAME_16K;

                    // Consume input ring buffers
                    aecRingL.consume();
                    aecRingR.consume();
                    aecRingHP.consume();
                    aecOutputReady = true;
                }

                // Apply AEC output: blend with original signal
                // During startup transient (no output yet), pass through unchanged
                if (aecOutputReady && aecOutRingL.writePos > 0) {
                    // Upsample the AEC output back to 48kHz
                    // We process NS_FRAME_16K (160) samples from AEC output per block
                    int consumeCount = std::min(NS_FRAME_16K, aecOutRingL.writePos);

                    // Use temporary buffers for upsample input
                    float aecChunkL[NS_FRAME_16K];
                    float aecChunkR[NS_FRAME_16K];
                    memcpy(aecChunkL, aecOutRingL.buf, consumeCount * sizeof(float));
                    memcpy(aecChunkR, aecOutRingR.buf, consumeCount * sizeof(float));

                    // Shift remaining data in output ring
                    int remaining = aecOutRingL.writePos - consumeCount;
                    if (remaining > 0) {
                        memmove(aecOutRingL.buf, aecOutRingL.buf + consumeCount, remaining * sizeof(float));
                        memmove(aecOutRingR.buf, aecOutRingR.buf + consumeCount, remaining * sizeof(float));
                    }
                    aecOutRingL.writePos = remaining;
                    aecOutRingR.writePos = remaining;

                    // Upsample to 48kHz
                    aecResUpL.upsample3(aecChunkL, veEstUp48kL, consumeCount);
                    aecResUpR.upsample3(aecChunkR, veEstUp48kR, consumeCount);

                    // Replace signal with blended AEC output
                    int outCount = consumeCount * 3;
                    for (int i = 0; i < samplesRead && i < outCount; i++) {
                        float aecL = std::clamp(veEstUp48kL[i], -1.0f, 1.0f);
                        float aecR = std::clamp(veEstUp48kR[i], -1.0f, 1.0f);
                        // Blend: 0 = original, 1 = full AEC output
                        floatL[i] = (1.0f - blend) * floatL[i] + blend * aecL;
                        floatR[i] = (1.0f - blend) * floatR[i] + blend * aecR;
                        if (std::isnan(floatL[i])) floatL[i] = 0.0f;
                        if (std::isnan(floatR[i])) floatR[i] = 0.0f;
                    }
                }
            }
        }

        // ── 8. Noise Suppression (downsample → NS → upsample) ──
        if (localParams.nsEnabled && _nsHandleL && _nsHandleR && samplesRead == BLOCK_SIZE) {
            // Downsample 48kHz → 16kHz (480 → 160 samples)
            resamplerDownL.downsample3(floatL, down16kL, NS_FRAME_16K);
            resamplerDownR.downsample3(floatR, down16kR, NS_FRAME_16K);

            // Process Left channel: float → int16 → NS → int16 → float
            for (int i = 0; i < NS_FRAME_16K; i++) {
                float s = std::clamp(down16kL[i], -1.0f, 1.0f);
                ns16kIn[i] = static_cast<int16_t>(s * 32767.0f);
            }
            ns_process(static_cast<ns_handle_t>(_nsHandleL), ns16kIn, ns16kOut);
            for (int i = 0; i < NS_FRAME_16K; i++) {
                down16kL[i] = static_cast<float>(ns16kOut[i]) * scale;
            }

            // Process Right channel
            for (int i = 0; i < NS_FRAME_16K; i++) {
                float s = std::clamp(down16kR[i], -1.0f, 1.0f);
                ns16kIn[i] = static_cast<int16_t>(s * 32767.0f);
            }
            ns_process(static_cast<ns_handle_t>(_nsHandleR), ns16kIn, ns16kOut);
            for (int i = 0; i < NS_FRAME_16K; i++) {
                down16kR[i] = static_cast<float>(ns16kOut[i]) * scale;
            }

            // Upsample 16kHz → 48kHz (160 → 480 samples)
            resamplerUpL.upsample3(down16kL, floatL, NS_FRAME_16K);
            resamplerUpR.upsample3(down16kR, floatR, NS_FRAME_16K);
        }

        // ── 8b. AGC (downsample → AGC → upsample, after NS, before gain) ──
        if (localParams.agcEnabled && _agcHandleL && _agcHandleR && samplesRead == BLOCK_SIZE) {
            // Downsample 48kHz → 16kHz (480 → 160 samples)
            agcResDownL.downsample3(floatL, agcDown16kL, NS_FRAME_16K);
            agcResDownR.downsample3(floatR, agcDown16kR, NS_FRAME_16K);

            // Process Left channel: float → int16 → AGC → int16 → float
            for (int i = 0; i < NS_FRAME_16K; i++) {
                float s = std::clamp(agcDown16kL[i], -1.0f, 1.0f);
                agc16kIn[i] = static_cast<int16_t>(s * 32767.0f);
            }
            esp_agc_process(_agcHandleL, agc16kIn, agc16kOut, NS_FRAME_16K, 16000);
            for (int i = 0; i < NS_FRAME_16K; i++) {
                agcDown16kL[i] = static_cast<float>(agc16kOut[i]) * scale;
            }

            // Process Right channel
            for (int i = 0; i < NS_FRAME_16K; i++) {
                float s = std::clamp(agcDown16kR[i], -1.0f, 1.0f);
                agc16kIn[i] = static_cast<int16_t>(s * 32767.0f);
            }
            esp_agc_process(_agcHandleR, agc16kIn, agc16kOut, NS_FRAME_16K, 16000);
            for (int i = 0; i < NS_FRAME_16K; i++) {
                agcDown16kR[i] = static_cast<float>(agc16kOut[i]) * scale;
            }

            // Upsample 16kHz → 48kHz (160 → 480 samples)
            agcResUpL.upsample3(agcDown16kL, floatL, NS_FRAME_16K);
            agcResUpR.upsample3(agcDown16kR, floatR, NS_FRAME_16K);
        }

        // ── 9. Apply output gain ──
        float gain = localParams.outputGain;
        for (int i = 0; i < samplesRead; i++) {
            floatL[i] *= gain;
            floatR[i] *= gain;
        }

        // ── 10. Compute RMS + peak for level metering ──
        float sumL = 0.0f, sumR = 0.0f;
        float pkL = 0.0f, pkR = 0.0f;
        for (int i = 0; i < samplesRead; i++) {
            float absL = fabsf(floatL[i]);
            float absR = fabsf(floatR[i]);
            sumL += floatL[i] * floatL[i];
            sumR += floatR[i] * floatR[i];
            if (absL > pkL) pkL = absL;
            if (absR > pkR) pkR = absR;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _levels.rmsLeft = sqrtf(sumL / (float)samplesRead);
            _levels.rmsRight = sqrtf(sumR / (float)samplesRead);
            // Peak hold with decay
            _levels.peakLeft = std::max(pkL, _levels.peakLeft * PEAK_DECAY);
            _levels.peakRight = std::max(pkR, _levels.peakRight * PEAK_DECAY);
        }

        // ── 11. Clamp and convert to int16 stereo output ──
        for (int i = 0; i < samplesRead; i++) {
            float l = std::clamp(floatL[i], -1.0f, 1.0f);
            float r = std::clamp(floatR[i], -1.0f, 1.0f);
            outBuf[i * 2 + 0] = (int16_t)(l * 32767.0f);
            outBuf[i * 2 + 1] = (int16_t)(r * 32767.0f);
        }

        // ── 12. Apply mute (zero buffer) ──
        if (localParams.outputMute) {
            memset(outBuf, 0, samplesRead * NUM_CHANNELS_OUT * sizeof(int16_t));
        }

        // ── 13. Write to I2S (stereo output) ──
        size_t bytesWritten = 0;
        codec->i2s_write(outBuf, samplesRead * NUM_CHANNELS_OUT * sizeof(int16_t),
                         &bytesWritten, portMAX_DELAY);
    }

    // Cleanup
    delete[] inBuf;
    delete[] outBuf;
    delete[] floatL;
    delete[] floatR;
    delete[] floatHP;
    delete[] down16kL;
    delete[] down16kR;
    delete[] ns16kIn;
    delete[] ns16kOut;
    delete[] up48kL;
    delete[] up48kR;
    delete[] veDown16kL;
    delete[] veDown16kR;
    delete[] veDown16kHP;
    delete[] veEst16kL;
    delete[] veEst16kR;
    delete[] veEstUp48kL;
    delete[] veEstUp48kR;
    delete[] agcDown16kL;
    delete[] agcDown16kR;
    delete[] agc16kIn;
    delete[] agc16kOut;

    // AEC buffers (heap_caps allocated)
    heap_caps_free(aec16kInL);
    heap_caps_free(aec16kInR);
    heap_caps_free(aec16kRef);
    heap_caps_free(aec16kOutL);
    heap_caps_free(aec16kOutR);
    heap_caps_free(vad16kBuf);
    delete[] aecFloat16kL;
    delete[] aecFloat16kR;
    delete[] aecDown16kBlockL;
    delete[] aecDown16kBlockR;
    delete[] aecDown16kBlockHP;

    mclog::tagInfo(TAG, "audio task exiting");
}
