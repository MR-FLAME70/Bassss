#pragma once
#include <atomic>
#include <vector>
#include <array>
#include <mutex>
#include <thread>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <functional>
#include "../settings.h"
#include "RingBuffer.h"
#include "../dsp/BiquadFilter.h"
#include "../dsp/ReverbEngine.h"
#include "../dsp/Compressor.h"
#include "../dsp/Equalizer.h"
#include "../dsp/StereoWidth.h"
#include "../dsp/AcousticEngine.h"
#include "../dsp/SpeakerConfig.h"
#include "../dsp/DynamicBass.h"
#include "../dsp/PitchShifter.h"
#include "../dsp/Rotator.h"
#include "../dsp/ClampProcessor.h"
#include "../dsp/EchoEngine.h"

// ── ParamSmoother ────────────────────────────────────────────────────────────
// One-pole exponential smoother for control-rate values (gains, mix levels)
// that are updated instantly from applySettingsInternal() but consumed once
// per sample. Without this, a slider move or an on/off toggle is a single-
// sample step in gain — an audible click/pop. Advancing this once per sample
// spreads the change over a few milliseconds instead.
class ParamSmoother {
public:
    void init(double sampleRate, float timeMs, float initial = 0.f) {
        value  = initial;
        target = initial;
        coeff = (float)std::exp(-1.0 / (sampleRate * (timeMs / 1000.0)));
    }
    void setTarget(float t) { target = t; }
    inline float next() {
        value += (1.f - coeff) * (target - value);
        return value;
    }
    float current() const { return value; }
private:
    float value  = 0.f;
    float target = 0.f;
    float coeff  = 0.f;
};

// ──────────────────────────────────────────────────────────────────────────────
// AudioProcessor — the complete DSP signal chain.
//
// Signal chain:
//   source
//     [→ mic gain (only when mic-only mode; "both" mode applies mic gain
//        in AudioCapture::outCallback before the frames reach here)]
//     → filterNode       (bass low-shelf boost)
//     ├→ dryGainNode     → sumNode
//     └→ reverbEngine    → resonanceNode → wetGainNode × reverbVolScale → sumNode
//                        → panNode (rotator) → surroundGainNode → sumNode
//     sumNode → processedMasterGain → outputSumNode
//   source → bypassGainNode → outputSumNode   (A/B bypass path)
//     outputSumNode → acousticEngine
//     → echoEngine (wet delta scaled by echoVolScale) → advanced chain
//     → speakerConfig
//     → speakerOutputGain  ← FINAL volume, independent of all effects
//     → analyserNode → clampNode → output
//
// Thread-safe: processStereo() is called from the PortAudio callback thread;
// all parameter updates use atomic stores or a mutex-protected update queue.
// ──────────────────────────────────────────────────────────────────────────────
class AudioProcessor {
public:
    AudioProcessor();
    ~AudioProcessor();

    void setSampleRate(double sr);
    void applySettings(const AppSettings& s);

    // Called from audio thread — process one stereo frame in-place.
    void processStereo(float& l, float& r);

    // VU meter: RMS + peak from last meter interval. Call from UI thread.
    struct MeterData { float rms = 0.f; float peak = 0.f; bool clipping = false; };
    MeterData getMeterData();

    // Spectrum: FFT magnitude bins. Call from UI thread.
    bool getSpectrum(std::vector<float>& bins);

    // Start/stop recording to WAV (returns file path on stop)
    void startRecording();
    bool stopRecording(const QString& outPath);

    // Enable/disable the entire chain
    void setEnabled(bool on) { enabled.store(on); }
    bool isEnabled() const   { return enabled.load(); }

    // Used by AudioCapture::outCallback to apply mic gain to the mic ring
    // frames BEFORE summing them with loopback in "both" mode, so the mic
    // slider only scales the microphone signal and not the loopback.
    float getMicGainAtomic() const { return micGain.load(); }

private:
    double sampleRate = 44100.0;
    std::atomic<bool> enabled{false};

    // ── DSP nodes (audio thread) ─────────────────────────────────────────────
    BiquadFilter bassFilter;
    std::atomic<float> volumeGain{1.f};
    std::atomic<float> micGain{1.f};

    // True only when the current input includes a microphone signal AND that
    // mic gain should be applied here in processStereo (i.e. mic-only mode).
    // In "both" mode the mic frames are pre-scaled in AudioCapture::outCallback
    // before being summed with loopback, so we must NOT apply the gain here
    // a second time (audioSourceBoth handles that gate).
    std::atomic<bool> micInMix{false};

    // True when audioSourceMode == "both". Prevents processStereo from
    // double-applying micGain when AudioCapture::outCallback already scaled
    // the mic ring frames before summing.
    std::atomic<bool> audioSourceBoth{false};

    // Reverb
    ReverbEngine reverbEngine;
    BiquadFilter resonanceFilter;
    std::atomic<float> dryGain{1.f};
    std::atomic<float> wetGain{0.f};
    // Independent reverb wet scale (does not affect dry or final output level)
    std::atomic<float> reverbVolScale_{1.f};

    // Surround rotation
    Rotator rotator;
    std::atomic<float> surroundGain{0.f};

    // A/B bypass
    std::atomic<bool>  bypass{false};
    std::atomic<float> processedMasterGain_{1.f};

    // Final output gain — applied AFTER all DSP so lowering it does not
    // change the reverb/echo wet-to-dry balance in the mix.
    std::atomic<float> speakerOutputGain_{1.f};

    // Echo wet scale — scales only the echo contribution (the wet delta).
    std::atomic<float> echoVolScale_{1.f};

    // Smoothed control-rate gains
    ParamSmoother volumeSm_, drySm_, wetSm_, procGainSm_, bypassGainSm_, micGainSm_;
    ParamSmoother speakerOutSm_, reverbVolSm_, echoVolSm_;
    bool smoothersInited_ = false;

    // Post-processing chain
    AcousticEngine acousticEngine;
    Equalizer      equalizer;
    DynamicBass    dynamicBass;
    Compressor     compressor;
    Compressor     limiter;
    StereoWidth    stereoWidth;
    PitchShifter   pitchShifter;
    SpeakerConfig  speakerConfig;
    ClampProcessor clamp;
    EchoEngine     echoEngine;

    // Module on/off flags
    std::atomic<bool> acousticEngineOn{false};
    std::atomic<bool> eqOn{false};
    std::atomic<bool> dynBassOn{false};
    std::atomic<bool> compOn{false};
    std::atomic<bool> limOn{false};
    std::atomic<bool> stereoWidthOn{false};
    std::atomic<bool> pitchOn{false};
    std::atomic<bool> speakerConfigOn{false};
    std::atomic<bool> reverbOn{false};
    std::atomic<bool> echoOn{false};
    std::atomic<bool> echoBypass{false};

    // ── VU meter / Spectrum ──────────────────────────────────────────────────
    static constexpr int METER_BUF = 1024;
    std::vector<float> meterBufL, meterBufR;
    int   meterWritePos = 0;
    std::mutex meterMutex;
    MeterData  lastMeter;

    // ── Spectrum ──────────────────────────────────────────────────────────────
    std::atomic<bool> spectrumOn{false};
    static constexpr int FFT_SIZE  = 256;
    std::array<float, FFT_SIZE> fftBuf_[2]{};
    int  fftWriteSlot_ = 0;
    int  fftPos_       = 0;
    std::atomic<int>  fftReadyIndex_{-1};
    std::array<float, FFT_SIZE> hannWindow_{};
    void updateSpectrum(float l, float r);
    void computeSpectrumFromBlock(const std::array<float, FFT_SIZE>& block,
                                   std::vector<float>& outBins) const;

    // ── Recording ────────────────────────────────────────────────────────────
    std::atomic<bool>     recording{false};
    StereoRingBuffer      recordRing_;
    std::thread           recordThread_;
    std::atomic<bool>     recordThreadRunning_{false};
    std::ofstream         recordFile_;
    std::atomic<uint64_t> recordedFrames_{0};
    QString               recordPath_;
    void recordThreadLoop();
    void writeWavHeaderPlaceholder(std::ofstream& f);
    void patchWavHeader(std::ofstream& f, uint64_t frames);

    // ── Pending parameter update ─────────────────────────────────────────────
    std::mutex          settingsMutex;
    AppSettings         settingsBuf_[2];
    int                 settingsWriteSlot_ = 0;
    std::atomic<int>    settingsReadyIndex_{-1};
    void consumePendingSettings();
    void applySettingsInternal(const AppSettings& s);
};
