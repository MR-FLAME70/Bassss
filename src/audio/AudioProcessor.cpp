#include "AudioProcessor.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <fstream>

// ──────────────────────────────────────────────────────────────────────────────
// AudioProcessor implementation — matches offscreen.js signal chain
// ──────────────────────────────────────────────────────────────────────────────

AudioProcessor::AudioProcessor() {
    meterBufL.resize(METER_BUF, 0.f);
    meterBufR.resize(METER_BUF, 0.f);
    spectrumBins.resize(FFT_SIZE/2, 0.f);
    fftBuf.resize(FFT_SIZE, 0.f);

    // Set up limiter defaults (ratio=20, attack=0.001s, knee=0)
    limiter.setRatio(20.f);
    limiter.setAttack(0.001f);
    limiter.setKnee(0.f);
    limiter.setThreshold(-3.f);
    limiter.setRelease(0.05f);

    // Bass filter default: low-shelf @ 150Hz, gain=12dB, Q=0.707
    bassFilter.setType(BiquadFilter::LowShelf, 44100.0, 150.0, 0.707, 12.0);

    // Resonance (peaking on wet path)
    resonanceFilter.setType(BiquadFilter::Peaking, 44100.0, 1000.0, 1.0, 0.0);
}

void AudioProcessor::setSampleRate(double sr) {
    sampleRate = sr;
    bassFilter.setSampleRate(sr);
    reverbEngine.setSampleRate(sr);
    resonanceFilter.setSampleRate(sr);
    rotator.setSampleRate(sr);
    acousticEngine.setSampleRate(sr);
    equalizer.setSampleRate(sr);
    dynamicBass.setSampleRate(sr);
    compressor.setSampleRate(sr);
    limiter.setSampleRate(sr);
    pitchShifter.setSampleRate(sr);
    speakerConfig.setSampleRate(sr);
}

void AudioProcessor::applySettings(const AppSettings& s) {
    std::lock_guard<std::mutex> lk(settingsMutex);
    pendingSettings = s;
    pendingDirty.store(true);
}

void AudioProcessor::applySettingsInternal(const AppSettings& s) {
    // Bass filter
    bassFilter.setType(BiquadFilter::LowShelf,
                       sampleRate, s.frequency, 0.707,
                       s.bassOn ? s.gain : 0.0);

    volumeGain.store(s.volume / 100.f);

    // ── Reverb Engine ─────────────────────────────────────────────────────────
    // Two modes:
    //
    //  BASIC (reverbOn=true, reverbEngineOn=false)
    //    Parameters come from the Live Tab preset controls.
    //    Internal wet/dry: engine outputs 100% wet, outer dryGain/wetGain mix.
    //
    //  ADVANCED (reverbEngineOn=true)
    //    Parameters come from the Advanced Reverb Engine sliders.
    //    Overrides the basic reverb when active; both may be on simultaneously
    //    (advanced takes precedence for parameter routing).
    //    Wet/Dry controlled entirely by the advanced Wet/Dry Level sliders:
    //      - rp.wetLevel scales the reverb output
    //      - outer dryGain passes the dry signal (no pre-delay on dry)
    //      - outer wetGain = 1.0 (engine already at correct level)

    const bool useAdvanced = s.reverbEngineOn;
    const bool anyReverbOn = s.reverbOn || s.reverbEngineOn;

    ReverbEngine::Params rp;

    // ── Shared fields (both modes use the same DSP, same struct) ──────────────
    rp.preDelay             = s.reverbPredelay / 1000.f;
    rp.earlyReflectionDelay = s.reverbEarlyReflectionDelay / 1000.f;
    rp.earlyReflectionLevel = s.reverbEarlyReflectionLevel / 100.f;
    rp.lateReverbLevel      = s.reverbLateReverbLevel / 100.f;
    rp.hfDamping            = s.reverbHfDamping / 100.f;
    rp.lfDamping            = s.reverbLfDamping / 100.f;
    rp.stereoWidth          = s.reverbStereoWidth / 100.f;
    rp.density              = s.reverbDensity / 100.f;
    rp.modulationDepth      = s.reverbModulationDepth / 100.f;
    rp.modulationRate       = s.reverbModulationRate / 100.f;
    rp.lowCut               = s.reverbLowCut;

    if (useAdvanced) {
        // ── ADVANCED MODE ─────────────────────────────────────────────────────
        // Use the advanced reverb engine's own controls.
        rp.roomSize  = s.reverbRoomSize;
        rp.decayTime = s.reverbDecay;
        rp.diffusion = s.reverbDiffuse / 100.f;
        rp.highCut   = s.reverbHighCut;           // High Cut slider (not basic Tone)
        // Engine outputs scaled wet; dry is handled by outer dryGain below.
        rp.wetLevel  = s.reverbWetLevel / 100.f;
        rp.dryLevel  = 0.f;
    } else {
        // ── BASIC MODE ────────────────────────────────────────────────────────
        // Parameters driven by preset / Live Tab sliders.
        rp.roomSize  = s.reverbRoomSize;
        rp.decayTime = s.reverbDecay;
        rp.diffusion = s.reverbDiffuse / 100.f;
        rp.highCut   = s.reverbToneHz;            // Basic Tone Hz slider
        rp.wetLevel  = (s.reverbMix / 100.f) * (s.reverbAmount / 100.f);
        rp.dryLevel  = 0.f;                       // Dry handled by outer dryGain
    }

    reverbEngine.setParams(rp);
    reverbOn.store(anyReverbOn);

    // ── Outer wet/dry gains (applied in processStereo after engine output) ───
    if (useAdvanced) {
        // Advanced: Dry Level slider controls the unprocessed dry signal.
        // Wet Level is already baked into rp.wetLevel; outer wetGain = 1.
        dryGain.store(s.reverbDryLevel / 100.f);
        wetGain.store(1.f);
    } else {
        // Basic: derive from the Mix slider (same formula as the extension).
        float reverbWet = s.reverbMix / 100.f;
        dryGain.store(1.f - reverbWet * 0.5f);
        wetGain.store(reverbWet);
    }

    // Resonance (on wet path after reverb)
    resonanceFilter.setType(BiquadFilter::Peaking,
                            sampleRate,
                            s.reverbResonanceHz > 20.f ? s.reverbResonanceHz : 1000.f,
                            std::max(0.01f, s.reverbResonanceQ),
                            0.f);

    // Surround rotation (speed = rateHz in offscreen.js)
    rotator.setRate(s.speed * 0.15f);
    surroundGain.store(0.f); // surround gain is set by acousticEngine surround

    // A/B bypass
    bypass.store(s.bypass);
    processedMasterGain_.store(s.bypass ? 0.f : 1.f);

    // Acoustic engine
    acousticEngineOn.store(s.acousticEngineOn);
    if (s.acousticEngineOn) {
        acousticEngine.update(s.fxSurround, s.fxCrystalizer,
                              s.fxBass, s.fxSmartVolume,
                              s.fxDialogPlus, s.fxCrossover);
    }

    // EQ
    eqOn.store(s.eqOn);
    if (s.eqOn) equalizer.setBands(s.eqBands);

    // Dynamic Bass
    dynBassOn.store(s.dynBassOn);
    if (s.dynBassOn) dynamicBass.setParams(s.dynBassSensitivity, s.dynBassStrength);

    // Compressor
    compOn.store(s.compOn);
    compressor.setThreshold(s.compThreshold);
    compressor.setRatio(s.compRatio);
    compressor.setAttack(s.compAttack / 1000.f);
    compressor.setRelease(s.compRelease / 1000.f);
    compressor.setMakeupGain(s.compMakeup);

    // Limiter
    limOn.store(s.limOn);
    limiter.setThreshold(s.limThreshold);
    limiter.setRelease(s.limRelease / 1000.f);

    // Stereo width
    stereoWidthOn.store(s.stereoWidthOn);
    if (s.stereoWidthOn) stereoWidth.setWidth(s.stereoWidth);

    // Pitch
    pitchOn.store(s.pitchOn);
    if (s.pitchOn) pitchShifter.setPitchSemitones(s.pitch);

    // Speaker config
    speakerConfigOn.store(s.speakerConfigOn);
    if (s.speakerConfigOn) {
        speakerConfig.setMode(s.speakerMode.toStdString());
        SpeakerConfig::Layout lay;
        lay.frontWidth      = s.speakerFrontWidth    / 100.f;
        lay.rearWidth       = s.speakerRearWidth     / 100.f;
        lay.centerDistance  = s.speakerCenterDistance/ 100.f;
        lay.rearDistance    = s.speakerRearDistance  / 100.f;
        lay.subDistanceFt   = s.speakerSubDistance;
        lay.levelFL         = s.speakerLevelFL  / 100.f;
        lay.levelFR         = s.speakerLevelFR  / 100.f;
        lay.levelC          = s.speakerLevelC   / 100.f;
        lay.levelSub        = s.speakerLevelSub / 100.f;
        lay.levelRL         = s.speakerLevelRL  / 100.f;
        lay.levelRR         = s.speakerLevelRR  / 100.f;
        speakerConfig.setLayout(lay);
    }

    // Spectrum
    spectrumOn.store(s.spectrumOn);
}

void AudioProcessor::consumePendingSettings() {
    if (!pendingDirty.load()) return;
    AppSettings snap;
    {
        std::lock_guard<std::mutex> lk(settingsMutex);
        snap = pendingSettings;
        pendingDirty.store(false);
    }
    applySettingsInternal(snap);
}

// ──────────────────────────────────────────────────────────────────────────────
// Main audio processing entry-point (called from PortAudio callback thread)
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::processStereo(float& l, float& r) {
    // Consume any pending settings update from the UI thread
    consumePendingSettings();

    if (!enabled.load()) {
        return; // pass-through when disabled
    }

    float inL = l, inR = r;

    // ── Bass low-shelf + volume ───────────────────────────────────────────────
    bassFilter.processStereo(l, r);
    float vol = volumeGain.load();
    l *= vol; r *= vol;

    // ── Dry / Wet split ───────────────────────────────────────────────────────
    float dl = l * dryGain.load();
    float dr = r * dryGain.load();

    float wl = l, wr = r; // wet path
    if (reverbOn.load()) {
        float revOutL = 0.f, revOutR = 0.f;
        reverbEngine.processStereo(wl, wr, revOutL, revOutR);

        // Resonance filter on wet
        resonanceFilter.processStereo(revOutL, revOutR);

        wl = revOutL * wetGain.load();
        wr = revOutR * wetGain.load();
    } else {
        wl = 0.f; wr = 0.f;
    }

    // Surround (rotator on wet)
    float sl = wl, sr2 = wr;
    rotator.processStereo(sl, sr2);
    float sGain = surroundGain.load();
    float sumL = dl + wl + sl * sGain;
    float sumR = dr + wr + sr2 * sGain;

    // ── A/B bypass ────────────────────────────────────────────────────────────
    float procGain   = processedMasterGain_.load();
    float bypassGain = bypass.load() ? 1.f : 0.f;
    float outL = sumL * procGain + inL * bypassGain;
    float outR = sumR * procGain + inR * bypassGain;

    // ── Acoustic Engine ────────────────────────────────────────────────────────
    if (acousticEngineOn.load()) acousticEngine.processStereo(outL, outR);

    // ── Advanced Audio chain ──────────────────────────────────────────────────
    if (eqOn.load())          equalizer.processStereo(outL, outR);
    if (dynBassOn.load())     dynamicBass.processStereo(outL, outR);
    if (compOn.load())        compressor.processStereo(outL, outR);
    if (limOn.load())         limiter.processStereo(outL, outR);
    if (stereoWidthOn.load()) stereoWidth.processStereo(outL, outR);
    if (pitchOn.load())       pitchShifter.processStereo(outL, outR);

    // ── Speaker Config ────────────────────────────────────────────────────────
    if (speakerConfigOn.load()) speakerConfig.processStereo(outL, outR);

    // ── Spectrum / VU metering (analyser tap) ─────────────────────────────────
    if (spectrumOn.load()) updateSpectrum(outL, outR);
    {
        // VU meter ring-buffer write (no mutex — we accept occasional torn reads)
        meterBufL[meterWritePos] = outL;
        meterBufR[meterWritePos] = outR;
        meterWritePos = (meterWritePos + 1) % METER_BUF;
    }

    // ── Clamp (soft tanh at 0.8) ──────────────────────────────────────────────
    clamp.processStereo(outL, outR);

    l = outL;
    r = outR;

    // ── Recording buffer ──────────────────────────────────────────────────────
    if (recording) {
        std::lock_guard<std::mutex> lk(recordMutex);
        if (recording) {
            recordBuf.push_back(outL);
            recordBuf.push_back(outR);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// VU meter
// ──────────────────────────────────────────────────────────────────────────────
AudioProcessor::MeterData AudioProcessor::getMeterData() {
    float peakL = 0.f, peakR = 0.f;
    float rmsL  = 0.f, rmsR  = 0.f;
    for (int i = 0; i < METER_BUF; ++i) {
        float al = std::abs(meterBufL[i]);
        float ar = std::abs(meterBufR[i]);
        peakL = std::max(peakL, al);
        peakR = std::max(peakR, ar);
        rmsL += al*al; rmsR += ar*ar;
    }
    float peak = std::max(peakL, peakR);
    float rms  = std::sqrt(0.5f*(rmsL+rmsR) / METER_BUF);
    MeterData d;
    d.rms      = rms;
    d.peak     = peak;
    d.clipping = peak > 0.95f;
    return d;
}

// ──────────────────────────────────────────────────────────────────────────────
// Spectrum (very simple FFT-free power-spectrum via squared-magnitude buckets)
// A proper FFT (e.g., pffft) would be wired in here for the full visualiser.
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::updateSpectrum(float l, float r) {
    float mono = 0.5f*(l+r);
    fftBuf[fftPos] = mono;
    fftPos = (fftPos + 1) % FFT_SIZE;

    // Compute magnitude spectrum every FFT_SIZE samples
    static int countdown = FFT_SIZE;
    if (--countdown > 0) return;
    countdown = FFT_SIZE;

    std::vector<float> bins(FFT_SIZE/2, 0.f);
    for (int k = 0; k < FFT_SIZE/2; ++k) {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < FFT_SIZE; ++n) {
            int idx = (fftPos + n) % FFT_SIZE;
            double w = 0.5 - 0.5*std::cos(2.0*M_PI*n/FFT_SIZE); // Hann
            re += fftBuf[idx] * w * std::cos(2.0*M_PI*k*n/FFT_SIZE);
            im -= fftBuf[idx] * w * std::sin(2.0*M_PI*k*n/FFT_SIZE);
        }
        bins[k] = (float)std::sqrt(re*re+im*im);
    }

    std::lock_guard<std::mutex> lk(spectrumMutex);
    spectrumBins = bins;
    newSpectrumData = true;
}

bool AudioProcessor::getSpectrum(std::vector<float>& bins) {
    std::lock_guard<std::mutex> lk(spectrumMutex);
    if (!newSpectrumData) return false;
    bins = spectrumBins;
    newSpectrumData = false;
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Recording (WAV export)
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::startRecording() {
    std::lock_guard<std::mutex> lk(recordMutex);
    recordBuf.clear();
    recording = true;
}

bool AudioProcessor::stopRecording(const QString& outPath) {
    std::vector<float> data;
    {
        std::lock_guard<std::mutex> lk(recordMutex);
        recording = false;
        data = std::move(recordBuf);
        recordBuf.clear();
    }
    if (data.empty()) return false;

    // Write 32-bit float WAV
    std::ofstream f(outPath.toStdString(), std::ios::binary);
    if (!f) return false;

    int channels   = 2;
    int sr         = (int)sampleRate;
    int bitsPerSample = 32;
    int blockAlign = channels * bitsPerSample / 8;
    int byteRate   = sr * blockAlign;
    int dataSize   = (int)data.size() * sizeof(float);
    int chunkSize  = 36 + dataSize;

    auto write4 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v),4); };
    auto write2 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v),2); };

    f.write("RIFF", 4); write4(chunkSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); write4(18); // subchunk1Size = 18 for IEEE float
    write2(3);           // AudioFormat = IEEE_FLOAT (3)
    write2(channels);
    write4(sr);
    write4(byteRate);
    write2(blockAlign);
    write2(bitsPerSample);
    write2(0);           // cbSize = 0 (no extension)
    f.write("data", 4); write4(dataSize);
    f.write(reinterpret_cast<const char*>(data.data()), dataSize);

    return f.good();
}
