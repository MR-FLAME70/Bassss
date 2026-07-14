#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>
#include "../settings.h"
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
    ~AudioProcessor() = default;

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

    // Reverb
    ReverbEngine reverbEngine;
    BiquadFilter resonanceFilter; // narrow peaking on wet path
    std::atomic<float> dryGain{1.f};
    std::atomic<float> wetGain{0.f};

    // Surround rotation layer (panNode + surroundGainNode)
    Rotator rotator;
    std::atomic<float> surroundGain{0.f};

    // A/B bypass
    std::atomic<bool>  bypass{false};
    std::atomic<float> processedMasterGain_{1.f};

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

    // ── VU meter / Spectrum (ring-buffer) ────────────────────────────────────
    static constexpr int METER_BUF = 1024;
    std::vector<float> meterBufL, meterBufR;
    int   meterWritePos = 0;
    std::mutex meterMutex;
    MeterData  lastMeter;

    // Spectrum
    std::atomic<bool> spectrumOn{false};
    static constexpr int FFT_SIZE  = 256;
    std::vector<float> spectrumBins; // UI-side copy
    std::vector<float> fftBuf;
    int  fftPos = 0;
    std::mutex spectrumMutex;
    bool newSpectrumData = false;
    void updateSpectrum(float l, float r); // called from audio thread
    void computeFFT();

    // ── Recording ────────────────────────────────────────────────────────────
    std::mutex recordMutex;
    bool recording = false;
    std::vector<float> recordBuf; // interleaved stereo

    // ── Pending parameter update ─────────────────────────────────────────────
    // The UI thread posts a full AppSettings snapshot; the audio thread picks
    // it up atomically between frames. Uses a simple double-buffer approach:
    // pendingDirty is set true when new settings are ready; audio thread
    // consumes them.
    std::mutex          settingsMutex;
    AppSettings         pendingSettings;
    std::atomic<bool>   pendingDirty{false};
    void consumePendingSettings();
    void applySettingsInternal(const AppSettings& s);
};
