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
        // Standard one-pole time-constant coefficient for a ~timeMs settle.
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
// AudioProcessor — the complete DSP signal chain, matching offscreen.js exactly.
//
// Signal chain (same order as buildCaptureGraph in offscreen.js):
//   source
//     → filterNode       (bass low-shelf boost)
//     → volumeNode       (output volume gain)
//     ├→ dryGainNode     → sumNode
//     └→ reverbEngine    → resonanceNode → wetGainNode → sumNode
//                        → panNode (rotator) → surroundGainNode → sumNode
//     sumNode → processedMasterGain → outputSumNode
//   source → bypassGainNode → outputSumNode   (A/B bypass path)
//     outputSumNode → acousticEngine → advancedAudio chain → speakerConfigEngine
//     → analyserNode → clampNode → output
//
// Thread-safe: processStereo() is called from the PortAudio callback thread;
// all parameter updates use atomic stores or a mutex-protected update queue.
// ──────────────────────────────────────────────────────────────────────────────
class AudioProcessor {
public:
    AudioProcessor();
    // Explicit (not = default): must stop and join the recording writer
    // thread before destruction — a joinable std::thread left dangling in a
    // destructor calls std::terminate().
    ~AudioProcessor();

    void setSampleRate(double sr);
    void applySettings(const AppSettings& s);

    // Called from audio thread — process one stereo frame in-place.
    void processStereo(float& l, float& r);

    // VU meter: RMS + peak from last meter interval. Call from UI thread.
    struct MeterData { float rms = 0.f; float peak = 0.f; bool clipping = false; };
    MeterData getMeterData();

    // Spectrum: FFT magnitude bins. Call from UI thread.
    // Returns false if spectrum is disabled or no new data.
    bool getSpectrum(std::vector<float>& bins);

    // Start/stop recording to WAV (returns file path on stop)
    void startRecording();
    bool stopRecording(const QString& outPath);

    // Enable/disable the entire chain (bypass everything when off)
    void setEnabled(bool on) { enabled.store(on); }
    bool isEnabled() const   { return enabled.load(); }

private:
    double sampleRate = 44100.0;
    std::atomic<bool> enabled{false};

    // ── DSP nodes (audio thread) ─────────────────────────────────────────────
    // Bass low-shelf (filterNode)
    BiquadFilter bassFilter;
    std::atomic<float> volumeGain{1.f};
    std::atomic<float> micGain{1.f};

    // Reverb
    ReverbEngine reverbEngine;
    BiquadFilter resonanceFilter; // narrow peaking on wet path
    std::atomic<float> dryGain{1.f};
    std::atomic<float> wetGain{0.f};

    // Surround rotation layer (panNode + surroundGainNode)
    Rotator rotator;
    std::atomic<float> surroundGain{0.f};

    // A/B bypass. processedMasterGain_ is the *target*; bypassMixSm_ is the
    // actual per-sample crossfade, smoothed so flipping the A/B switch (or
    // any other module on/off flag feeding gain below) fades in ~8ms instead
    // of stepping instantly — the latter is a textbook click/pop source.
    std::atomic<bool>  bypass{false};
    std::atomic<float> processedMasterGain_{1.f};

    // Smoothed control-rate gains (see ParamSmoother above). Targets are set
    // from applySettingsInternal() (UI-triggered); next() is called once per
    // sample from processStereo() on the audio thread.
    ParamSmoother volumeSm_, drySm_, wetSm_, procGainSm_, bypassGainSm_, micGainSm_;
    bool smoothersInited_ = false;

    // Post-processing chain
    AcousticEngine acousticEngine;
    Equalizer      equalizer;
    DynamicBass    dynamicBass;
    Compressor     compressor;    // Advanced Audio compressor
    Compressor     limiter;       // Advanced Audio limiter
    StereoWidth    stereoWidth;
    PitchShifter   pitchShifter;
    SpeakerConfig  speakerConfig;
    ClampProcessor clamp;
    EchoEngine     echoEngine;

    // Module on/off flags (std::atomic<bool> for thread safety)
    std::atomic<bool> acousticEngineOn{false};
    std::atomic<bool> eqOn{false};
    std::atomic<bool> dynBassOn{false};
    std::atomic<bool> compOn{false};
    std::atomic<bool> limOn{false};
    std::atomic<bool> stereoWidthOn{false};
    std::atomic<bool> pitchOn{false};
    std::atomic<bool> speakerConfigOn{false};
    std::atomic<bool> reverbOn{false};
    // Echo: two independent flags so a true-bypass switch can pass audio
    // through untouched without disabling/losing the module's dialed-in
    // parameters (see AppSettings::echoBypass doc comment).
    std::atomic<bool> echoOn{false};
    std::atomic<bool> echoBypass{false};

    // ── VU meter / Spectrum (ring-buffer) ────────────────────────────────────
    static constexpr int METER_BUF = 1024;
    std::vector<float> meterBufL, meterBufR;
    int   meterWritePos = 0;
    std::mutex meterMutex;
    MeterData  lastMeter;

    // ── Spectrum ──────────────────────────────────────────────────────────────
    // The audio thread's only job here is to copy incoming samples into a
    // small ring; once a full block has accumulated it hands the *raw*
    // samples off (lock-free double buffer, no allocation) and returns
    // immediately. The actual magnitude-spectrum computation — an O(N^2)
    // DFT — runs lazily on the UI thread inside getSpectrum(), which is
    // polled a few times a second and has no real-time deadline. Previously
    // that O(N^2) loop (128 bins x 256 samples, with cos/sin per term) ran
    // directly on the audio thread every FFT_SIZE samples (~5.8ms @44.1kHz)
    // — a periodic multi-thousand-op spike that is a classic dropout source.
    std::atomic<bool> spectrumOn{false};
    static constexpr int FFT_SIZE  = 256;
    std::array<float, FFT_SIZE> fftBuf_[2]{};     // double-buffered raw samples
    int  fftWriteSlot_ = 0;
    int  fftPos_       = 0;
    std::atomic<int>  fftReadyIndex_{-1};         // slot ready for the UI thread, or -1
    std::array<float, FFT_SIZE> hannWindow_{};    // precomputed once (was recomputed per-bin)
    void updateSpectrum(float l, float r); // called from audio thread — O(1)
    void computeSpectrumFromBlock(const std::array<float, FFT_SIZE>& block,
                                   std::vector<float>& outBins) const; // UI thread

    // ── Recording ────────────────────────────────────────────────────────────
    // Processed frames are pushed into a lock-free ring (no lock, no
    // allocation, no per-sample I/O on the audio thread) and streamed to disk
    // incrementally by a dedicated low-priority writer thread. This bounds
    // memory usage to the ring's fixed size regardless of recording length
    // (previously an unbounded std::vector held the *entire* recording in
    // RAM and grew/reallocated on the audio thread while a mutex was held
    // for every single sample).
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
    // The UI thread posts a full AppSettings snapshot into whichever of the
    // two buffers isn't currently published, then atomically publishes its
    // index. The audio thread does a single atomic exchange to grab (and
    // clear) the published index — no mutex, no blocking, no allocation on
    // the audio thread's side. This replaces a std::mutex + full AppSettings
    // deep-copy (AppSettings contains QString/std::array members that can
    // heap-allocate) that previously ran on the real-time thread and risked
    // priority inversion if the UI thread was ever preempted mid-write.
    std::mutex          settingsMutex; // UI-thread side only (serializes writers)
    AppSettings         settingsBuf_[2];
    int                 settingsWriteSlot_ = 0;
    std::atomic<int>    settingsReadyIndex_{-1};
    void consumePendingSettings();
    void applySettingsInternal(const AppSettings& s);
};
