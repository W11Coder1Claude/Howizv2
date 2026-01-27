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

extern "C" {
#include <esp_ns.h>
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

    // Create NS handles if NS is enabled
    if (_params.nsEnabled) {
        createNsHandles(_nsHandleL, _nsHandleR, _params.nsMode);
    }

    xTaskCreatePinnedToCore(audioTask, "audio_eng", 16384, this, 10, &_taskHandle, 1);
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

void AudioEngine::setOutputGain(float gain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _params.outputGain = std::clamp(gain, 0.0f, 2.0f);
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
    codec->set_volume(80);
    codec->set_mute(true);  // Start muted

    // Allocate I/O buffers
    const size_t inBufBytes = BLOCK_SIZE * NUM_CHANNELS_IN * sizeof(int16_t);
    const size_t outBufBytes = BLOCK_SIZE * NUM_CHANNELS_OUT * sizeof(int16_t);

    int16_t* inBuf = new int16_t[BLOCK_SIZE * NUM_CHANNELS_IN];
    int16_t* outBuf = new int16_t[BLOCK_SIZE * NUM_CHANNELS_OUT];
    float* floatL = new float[BLOCK_SIZE];
    float* floatR = new float[BLOCK_SIZE];

    // NS processing buffers (16kHz domain)
    float* down16kL = new float[NS_FRAME_16K];
    float* down16kR = new float[NS_FRAME_16K];
    int16_t* ns16kIn = new int16_t[NS_FRAME_16K];
    int16_t* ns16kOut = new int16_t[NS_FRAME_16K];
    float* up48kL = new float[BLOCK_SIZE];
    float* up48kR = new float[BLOCK_SIZE];

    // Resamplers
    Resampler resamplerDownL, resamplerDownR;
    Resampler resamplerUpL, resamplerUpR;
    resamplerDownL.init(true);
    resamplerDownR.init(true);
    resamplerUpL.init(false);
    resamplerUpR.init(false);

    mclog::tagInfo(TAG, "buffers allocated: in={}B out={}B", inBufBytes, outBufBytes);

    // Local copy of params to minimize lock time
    AudioEngineParams localParams;
    bool localParamsChanged = true;
    bool prevNsEnabled = false;
    int prevNsMode = -1;

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

            // Recalculate all biquad coefficients
            recalcAllCoeffs();

            mclog::tagInfo(TAG, "params updated: micGain={:.0f} vol={} mute={} hpf={}/{:.0f}Hz lpf={}/{:.0f}Hz eq={:.1f}/{:.1f}/{:.1f}dB ns={}/mode={} gain={:.2f}",
                localParams.micGain, localParams.outputVolume, localParams.outputMute,
                localParams.hpfEnabled, localParams.hpfFrequency,
                localParams.lpfEnabled, localParams.lpfFrequency,
                localParams.eqLowGain, localParams.eqMidGain, localParams.eqHighGain,
                localParams.nsEnabled, localParams.nsMode,
                localParams.outputGain);
        }

        // ── 1. Read from I2S (4-channel input) ──
        size_t bytesRead = 0;
        codec->i2s_read(inBuf, inBufBytes, &bytesRead, portMAX_DELAY);

        int samplesRead = bytesRead / (NUM_CHANNELS_IN * sizeof(int16_t));
        if (samplesRead <= 0) continue;

        // ── 2. Extract MIC-L (ch0) and MIC-R (ch2), convert to float [-1.0, 1.0] ──
        constexpr float scale = 1.0f / 32768.0f;
        for (int i = 0; i < samplesRead; i++) {
            floatL[i] = (float)inBuf[i * NUM_CHANNELS_IN + 0] * scale;  // MIC-L
            floatR[i] = (float)inBuf[i * NUM_CHANNELS_IN + 2] * scale;  // MIC-R
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

        // ── 6. Noise Suppression (downsample → NS → upsample) ──
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

        // ── 7. Apply output gain ──
        float gain = localParams.outputGain;
        for (int i = 0; i < samplesRead; i++) {
            floatL[i] *= gain;
            floatR[i] *= gain;
        }

        // ── 8. Compute RMS + peak for level metering ──
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

        // ── 9. Clamp and convert to int16 stereo output ──
        for (int i = 0; i < samplesRead; i++) {
            float l = std::clamp(floatL[i], -1.0f, 1.0f);
            float r = std::clamp(floatR[i], -1.0f, 1.0f);
            outBuf[i * 2 + 0] = (int16_t)(l * 32767.0f);
            outBuf[i * 2 + 1] = (int16_t)(r * 32767.0f);
        }

        // ── 10. Apply mute (zero buffer) ──
        if (localParams.outputMute) {
            memset(outBuf, 0, samplesRead * NUM_CHANNELS_OUT * sizeof(int16_t));
        }

        // ── 11. Write to I2S (stereo output) ──
        size_t bytesWritten = 0;
        codec->i2s_write(outBuf, samplesRead * NUM_CHANNELS_OUT * sizeof(int16_t),
                         &bytesWritten, portMAX_DELAY);
    }

    // Cleanup
    delete[] inBuf;
    delete[] outBuf;
    delete[] floatL;
    delete[] floatR;
    delete[] down16kL;
    delete[] down16kR;
    delete[] ns16kIn;
    delete[] ns16kOut;
    delete[] up48kL;
    delete[] up48kR;

    mclog::tagInfo(TAG, "audio task exiting");
}
