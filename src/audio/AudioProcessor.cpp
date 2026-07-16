#include "AudioProcessor.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <fstream>
#include <chrono>
#include <QFile>
#include <QDir>
#include <QDateTime>

// ──────────────────────────────────────────────────────────────────────────────
// AudioProcessor implementation — matches offscreen.js signal chain
// ──────────────────────────────────────────────────────────────────────────────

AudioProcessor::AudioProcessor() {
    meterBufL.resize(METER_BUF, 0.f);
    meterBufR.resize(METER_BUF, 0.f);

    // Precompute the Hann window once. The old code recomputed
    // 0.5-0.5*cos(2*pi*n/N) inside the innermost (k,n) loop of the spectrum
    // DFT — i.e. FFT_SIZE/2 times more often than necessary, all for a
    // value that never changes.
    for (int n = 0; n < FFT_SIZE; ++n)
        hannWindow_[n] = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * n / FFT_SIZE);

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

AudioProcessor::~AudioProcessor() {
    if (recordThreadRunning_.load()) {
        recordThreadRunning_.store(false);
        if (recordThread_.joinable()) recordThread_.join();
    }
    if (recordFile_.is_open()) recordFile_.close();
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
    echoEngine.setSampleRate(sr);

    // (Re)initialize gain smoothers for the new rate. ~8ms is short enough
    // to feel instant on a slider drag but long enough (several hundred
    // samples) to eliminate the audible step on toggle/mix changes.
    volumeSm_.init(sr, 8.f, volumeGain.load());
    micGainSm_.init(sr, 8.f, micGain.load());
    drySm_.init(sr, 8.f, dryGain.load());
    wetSm_.init(sr, 8.f, wetGain.load());
    procGainSm_.init(sr, 8.f, processedMasterGain_.load());
    bypassGainSm_.init(sr, 8.f, bypass.load() ? 1.f : 0.f);
    smoothersInited_ = true;
}

void AudioProcessor::applySettings(const AppSettings& s) {
    // Lock-free hand-off to the audio thread: write into whichever of the
    // two buffers isn't the currently-published one, then publish its index
    // with a single atomic store. std::mutex here only serializes concurrent
    // UI-thread callers against each other (settings can be posted from
    // more than one Qt signal handler) — the audio thread never takes it.
    std::lock_guard<std::mutex> lk(settingsMutex);
    settingsWriteSlot_ = 1 - settingsWriteSlot_;
    settingsBuf_[settingsWriteSlot_] = s;
    settingsReadyIndex_.store(settingsWriteSlot_, std::memory_order_release);
}

void AudioProcessor::applySettingsInternal(const AppSettings& s) {
    // Bass filter
    bassFilter.setType(BiquadFilter::LowShelf,
                       sampleRate, s.frequency, 0.707,
                       s.bassOn ? s.gain : 0.0);

    volumeGain.store(s.volume / 100.f);
    micGain.store(s.micVolume / 100.f);

    // ── Reverb Engine ─────────────────────────────────────────────────────────
    // Matches offscreen.js exactly: there is a single hybrid reverb engine,
    // always active with one unified parameter set — there is no separate
    // "basic" vs "advanced" engine or parameter source.
    //
    // reverbEngineOn ("Advanced Reverb Engine" toggle) does NOT switch which
    // sliders feed the engine. Per pushSettings()'s reverbEngineBypass, when
    // the toggle is off, the subset of fields present in REVERB_ENGINE_DEFAULTS
    // (roomSize, earlyReflection*, lateReverbLevel, hfDamping, lfDamping,
    // stereoWidth, modulationDepth/Rate, lowCut, wetLevel, dryLevel) is reset
    // to neutral defaults so those sliders only affect audio when enabled.
    // preDelay, decayTime, diffusion, highCut (Tone Hz) and density are NOT
    // part of that bypass set and are always live, regardless of the toggle.
    //
    // The outer wet/dry bus (Effects Amount, Mix, Song Volume) is a separate,
    // independent stage — see setReverbMix() in the original — gated solely
    // by the basic Reverb on/off switch, never by reverbEngineOn.

    ReverbEngine::Params rp;

    // ── Always-live fields (not part of the REVERB_ENGINE_DEFAULTS bypass) ───
    rp.preDelay  = s.reverbPredelay / 1000.f;
    rp.decayTime = s.reverbDecay;
    rp.diffusion = s.reverbDiffuse / 100.f;
    rp.highCut   = s.reverbToneHz;   // single Tone/High-Cut control, always live
    rp.density   = s.reverbDensity / 100.f;

    if (s.reverbEngineOn) {
        // ── Advanced Reverb Engine ON: sliders are live ─────────────────────
        rp.roomSize             = s.reverbRoomSize;
        rp.earlyReflectionDelay = s.reverbEarlyReflectionDelay / 1000.f;
        rp.earlyReflectionLevel = s.reverbEarlyReflectionLevel / 100.f;
        rp.lateReverbLevel      = s.reverbLateReverbLevel / 100.f;
        rp.hfDamping            = s.reverbHfDamping / 100.f;
        rp.lfDamping            = s.reverbLfDamping / 100.f;
        rp.stereoWidth          = s.reverbStereoWidth / 100.f;
        rp.modulationDepth      = s.reverbModulationDepth / 100.f;
        rp.modulationRate       = s.reverbModulationRate / 100.f;
        rp.lowCut               = s.reverbLowCut;
        rp.wetLevel             = s.reverbWetLevel / 100.f;
        rp.dryLevel             = s.reverbDryLevel / 100.f;
    } else {
        // ── Advanced Reverb Engine OFF: bypass to REVERB_ENGINE_DEFAULTS ────
        rp.roomSize             = 1.0f;   // reverbRoomSize
        rp.earlyReflectionDelay = 0.f;    // reverbEarlyReflectionDelay: 0 ms
        rp.earlyReflectionLevel = 0.35f;  // reverbEarlyReflectionLevel: 35 %
        rp.lateReverbLevel      = 1.0f;   // reverbLateReverbLevel: 100 %
        rp.hfDamping            = 0.f;    // reverbHfDamping: 0 %
        rp.lfDamping            = 0.f;    // reverbLfDamping: 0 %
        rp.stereoWidth          = 1.0f;   // reverbStereoWidth: 100 %
        rp.modulationDepth      = 0.40f;  // reverbModulationDepth: 40 %
        rp.modulationRate       = 0.35f;  // reverbModulationRate: 35 %
        rp.lowCut               = 80.f;   // reverbLowCut: 80 Hz
        rp.wetLevel             = 1.0f;   // reverbWetLevel: 100 %
        rp.dryLevel             = 0.f;    // reverbDryLevel: 0 %
    }

    reverbEngine.setParams(rp);
    // Outer bus is gated solely by the basic Reverb toggle (setReverbMix's
    // `on` argument is reverbOn — reverbEngineOn never enables/disables it).
    reverbOn.store(s.reverbOn);

    // ── Outer wet/dry gains (offscreen.js setReverbMix) ───────────────────────
    // wet = reverbOn ? (mix/100) * (amount/100) : 0
    // dry = max(0, songVolume/100)   — independent of reverbOn/mix
    {
        float amount = std::clamp(s.reverbAmount, 0.f, 100.f) / 100.f;
        float mix    = std::clamp(s.reverbMix,    0.f, 100.f) / 100.f;
        wetGain.store(s.reverbOn ? mix * amount : 0.f);
        dryGain.store(std::max(0.f, s.songVolume / 100.f));
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

    // ── Echo Engine ────────────────────────────────────────────────────────────
    echoOn.store(s.echoOn);
    echoBypass.store(s.echoBypass);
    if (s.echoOn) {
        EchoEngine::Params ep;

        // ── Basic / shared controls ─────────────────────────────────────────
        ep.delayMs    = s.echoDelayMs;
        ep.feedback   = s.echoFeedback   / 100.f;
        ep.mix        = s.echoMix        / 100.f;
        ep.tone       = s.echoTone       / 100.f;
        ep.pingPong   = s.echoPingPong   / 100.f;
        ep.numEchoes  = s.echoNumEchoes;
        ep.echoAmount = s.echoAmount     / 100.f;
        ep.wetLevel   = s.echoWetLevel   / 100.f;
        ep.dryLevel   = s.echoDryLevel   / 100.f;
        ep.outputGain = s.echoOutputGain / 100.f;

        // ── Advanced Echo Engine controls ─────────────────────────────────────
        ep.aeOn = s.aeOn;
        // Per-channel delays: always set so the delay line tracks echoDelayMs
        // even when the advanced engine is off (aeOn gate in EchoEngine selects
        // which value to actually use, but we keep them in sync here).
        ep.aeLeftDelayMs  = s.aeOn ? s.aeLeftDelayMs  : s.echoDelayMs;
        ep.aeRightDelayMs = s.aeOn ? s.aeRightDelayMs : s.echoDelayMs;

        if (s.aeOn) {
            // [Delay] (aeLeft/RightDelayMs already set above)
            ep.aeStereoOffset  = s.aeStereoOffset;
            ep.aeStereoWidthD  = s.aeStereoWidthD  / 100.f;
            ep.aeTempoSync     = s.aeTempoSync;
            ep.aeMillisecondMode = s.aeMillisecondMode;
            // [Feedback]
            ep.aeCrossFeedback = s.aeCrossFeedback / 100.f;
            ep.aeFbSaturation  = s.aeFbSaturation  / 100.f;
            ep.aeFbDamping     = s.aeFbDamping     / 100.f;
            ep.aeFbLowCut      = s.aeFbLowCut;
            ep.aeFbHighCut     = s.aeFbHighCut;
            ep.aeFbDiffusion   = s.aeFbDiffusion   / 100.f;
            // [Stereo]
            ep.aeBalance       = s.aeBalance       / 100.f;   // ±1
            ep.aeLeftLevel     = s.aeLeftLevel      / 100.f;
            ep.aeRightLevel    = s.aeRightLevel     / 100.f;
            ep.aeMidSideMix    = s.aeMidSideMix    / 100.f;
            ep.aePingPongMode  = s.aePingPongMode;
            ep.aeSwapChannels  = s.aeSwapChannels;
            // [Tone]
            ep.aeToneLowCut    = s.aeToneLowCut;
            ep.aeToneHighCut   = s.aeToneHighCut;
            ep.aeToneBass      = s.aeToneBass;        // already in dB
            ep.aeToneMid       = s.aeToneMid;
            ep.aeToneTreble    = s.aeToneTreble;
            ep.aeTonePresence  = s.aeTonePresence;
            ep.aeToneBrightness= s.aeToneBrightness;
            // [Saturation]
            ep.aeTapeSat       = s.aeTapeSat        / 100.f;
            ep.aeAnalogSat     = s.aeAnalogSat      / 100.f;
            ep.aeDrive         = s.aeDrive          / 100.f;
            ep.aeWarmth        = s.aeWarmth         / 100.f;
            ep.aeSoftClip      = s.aeSoftClip;
            // [Dynamics]
            ep.aeInputGainDb   = s.aeInputGainDb;    // already in dB
            ep.aeOutputGainDb  = s.aeOutputGainDb;
            ep.aeWetGainDb     = s.aeWetGainDb;
            ep.aeDryGainDb     = s.aeDryGainDb;
            ep.aeIntLimiter    = s.aeIntLimiter;
            ep.aeSoftLimiter   = s.aeSoftLimiter;
            // [Mix]
            ep.aeWetLevel2     = s.aeWetLevel2      / 100.f;
            ep.aeDryLevel2     = s.aeDryLevel2      / 100.f;
            ep.aeBlend         = s.aeBlend          / 100.f;
            ep.aeMix           = (s.aeMixOverride >= 0.f)
                                   ? s.aeMixOverride / 100.f
                                   : -1.f; // -1 → use basic mix
            // [Modulation]
            ep.aeWow           = s.aeWow            / 100.f;
            ep.aeFlutter       = s.aeFlutter        / 100.f;
            ep.aeModDepth      = s.aeModDepth       / 100.f;
            ep.aeModRate       = s.aeModRate;        // Hz, no scaling
            ep.aeRandomDrift   = s.aeRandomDrift    / 100.f;
            // [Spatial]
            ep.aeHaasWidth        = s.aeHaasWidth;   // ms, no scaling
            ep.aeStereoSpread     = s.aeStereoSpread / 100.f;
            ep.aeEarlyReflections = s.aeEarlyReflections / 100.f;
            ep.aeReflLevel        = s.aeReflLevel    / 100.f;
            ep.aeReflDelay        = s.aeReflDelay;   // ms, no scaling
        }
        // ── Echo Algorithm character ──────────────────────────────────────────
        // When the Advanced Echo Engine is off (aeOn=false), apply the selected
        // algorithm's DSP fingerprint. This enables the advanced signal path
        // transparently — the user sees a simple Algorithm selector but gets
        // rich, hardware-modelled coloration without touching any sliders.
        // When s.aeOn is true, the user's own advanced settings take precedence
        // and algorithm shaping is suppressed so there's no interference.
        if (!s.aeOn) {
            const QString& alg = s.echoAlgorithm;
            // Any algorithm other than "clean" (and the implicit neutral state)
            // enables the advanced path with algorithm-specific defaults.
            if (alg == "digital") {
                // Crisp, transparent. Enable advanced path but keep it neutral
                // — no saturation, no modulation, flat tone. True to the signal.
                ep.aeOn = true;
                // all params remain at neutral defaults; just gates the path

            } else if (alg == "analog") {
                // Warm odd-harmonic saturation, gentle HF limiting on feedback,
                // whisper of slow wow to simulate VCO drift.
                ep.aeOn          = true;
                ep.aeAnalogSat   = 0.20f;
                ep.aeWarmth      = 0.15f;
                ep.aeFbHighCut   = 8000.f;
                ep.aeWow         = 0.05f;
                ep.aeModDepth    = 0.15f;
                ep.aeModRate     = 0.6f;

            } else if (alg == "tape") {
                // Even-harmonic tape saturation, gentle treble rolloff per repeat,
                // classic wow & flutter, stochastic speed drift.
                ep.aeOn          = true;
                ep.aeTapeSat     = 0.35f;
                ep.aeAnalogSat   = 0.10f;
                ep.aeWarmth      = 0.20f;
                ep.aeFbHighCut   = 6000.f;
                ep.aeToneTreble  = -2.f;
                ep.aeWow         = 0.15f;
                ep.aeFlutter     = 0.08f;
                ep.aeModDepth    = 0.30f;
                ep.aeModRate     = 0.8f;
                ep.aeRandomDrift = 0.05f;

            } else if (alg == "bucketbrigade") {
                // BBD-style: band-limited, slightly gritty, clock-noise drift.
                ep.aeOn          = true;
                ep.aeAnalogSat   = 0.25f;
                ep.aeWarmth      = 0.10f;
                ep.aeFbHighCut   = 7000.f;
                ep.aeFbLowCut    = 80.f;
                ep.aeRandomDrift = 0.15f;
                ep.aeWow         = 0.08f;
                ep.aeFlutter     = 0.05f;
                ep.aeModDepth    = 0.20f;
                ep.aeModRate     = 1.2f;

            } else if (alg == "vintage") {
                // Heavy aged-hardware character: saturated, dark, wobbly.
                ep.aeOn          = true;
                ep.aeTapeSat     = 0.50f;
                ep.aeAnalogSat   = 0.15f;
                ep.aeWarmth      = 0.25f;
                ep.aeFbHighCut   = 4000.f;
                ep.aeFbDamping   = 0.15f;
                ep.aeToneTreble  = -3.f;
                ep.aeWow         = 0.20f;
                ep.aeFlutter     = 0.10f;
                ep.aeModDepth    = 0.35f;
                ep.aeModRate     = 0.5f;
                ep.aeRandomDrift = 0.25f;

            } else if (alg == "warm") {
                // Low-end emphasis, silky top-end roll, tape warmth.
                ep.aeOn          = true;
                ep.aeTapeSat     = 0.20f;
                ep.aeWarmth      = 0.30f;
                ep.aeToneBass    = 2.f;
                ep.aeToneTreble  = -2.f;
                ep.aeFbHighCut   = 9000.f;

            } else if (alg == "dark") {
                // Heavily damped, bass-forward, shadows and depth.
                ep.aeOn          = true;
                ep.aeTapeSat     = 0.30f;
                ep.aeWarmth      = 0.20f;
                ep.aeFbHighCut   = 3000.f;
                ep.aeToneHighCut = 8000.f;
                ep.aeToneBass    = 3.f;
                ep.aeToneTreble  = -4.f;
                ep.aeFbDamping   = 0.20f;

            } else if (alg == "bright") {
                // Articulate, airy, presence-forward, slight even-order sheen.
                ep.aeOn           = true;
                ep.aeAnalogSat    = 0.10f;
                ep.aeToneTreble   = 3.f;
                ep.aeTonePresence = 2.f;
                ep.aeToneBrightness = 2.f;

            } else if (alg == "lofi") {
                // Severe bandwidth limiting, heavy drift, maximum grit.
                ep.aeOn           = true;
                ep.aeAnalogSat    = 0.60f;
                ep.aeFbHighCut    = 4000.f;
                ep.aeToneHighCut  = 6000.f;
                ep.aeFbLowCut     = 120.f;
                ep.aeToneBass     = -1.f;
                ep.aeRandomDrift  = 0.40f;
                ep.aeFlutter      = 0.20f;
                ep.aeWow          = 0.10f;
                ep.aeModDepth     = 0.40f;
                ep.aeModRate      = 1.5f;
                ep.aeSoftClip     = true;
            }
            // "clean" and any unrecognised id: leave aeOn=false, pure basic path.
        }

        echoEngine.setParams(ep);
    }

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
    // Single atomic exchange, no lock, no allocation: grab whichever slot is
    // currently published (if any) and mark "nothing pending". Reading
    // settingsBuf_[idx] here is safe because the UI thread only ever writes
    // to the *other* slot on its next update, never the one it just
    // published — see applySettings().
    int idx = settingsReadyIndex_.exchange(-1, std::memory_order_acquire);
    if (idx < 0) return;
    applySettingsInternal(settingsBuf_[idx]);
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

    if (!smoothersInited_) setSampleRate(sampleRate); // safety net if never called

    float inL = l, inR = r;

    // ── Smoothed control-rate gains ───────────────────────────────────────────
    // Targets were set (as plain atomics) in applySettingsInternal(); each
    // gets ramped one small step per sample instead of jumping instantly, so
    // a volume-slider drag, a bypass toggle, or a reverb mix change fades in
    // over ~8ms instead of producing a single-sample discontinuity (click).
    volumeSm_.setTarget(volumeGain.load());
    drySm_.setTarget(dryGain.load());
    wetSm_.setTarget(wetGain.load());
    procGainSm_.setTarget(processedMasterGain_.load());
    bypassGainSm_.setTarget(bypass.load() ? 1.f : 0.f);
    micGainSm_.setTarget(micGain.load());

    // ── Mic / input gain ─────────────────────────────────────────────────────
    {
        float mg = micGainSm_.next();
        l *= mg; r *= mg;
    }

    // ── Bass low-shelf + volume ───────────────────────────────────────────────
    bassFilter.processStereo(l, r);
    float vol = volumeSm_.next();
    l *= vol; r *= vol;

    // ── Dry / Wet split ───────────────────────────────────────────────────────
    float dry = drySm_.next();
    float dl = l * dry;
    float dr = r * dry;

    float wl = l, wr = r; // wet path
    if (reverbOn.load()) {
        float revOutL = 0.f, revOutR = 0.f;
        reverbEngine.processStereo(wl, wr, revOutL, revOutR);

        // Resonance filter on wet
        resonanceFilter.processStereo(revOutL, revOutR);

        float wet = wetSm_.next();
        wl = revOutL * wet;
        wr = revOutR * wet;
    } else {
        // Still advance the smoother toward 0 so re-enabling reverb doesn't
        // resume from a stale target and jump.
        wetSm_.next();
        wl = 0.f; wr = 0.f;
    }

    // Surround (rotator on wet)
    float sl = wl, sr2 = wr;
    rotator.processStereo(sl, sr2);
    float sGain = surroundGain.load();
    float sumL = dl + wl + sl * sGain;
    float sumR = dr + wr + sr2 * sGain;

    // ── A/B bypass ────────────────────────────────────────────────────────────
    float procGain   = procGainSm_.next();
    float bypassGain = bypassGainSm_.next();
    float outL = sumL * procGain + inL * bypassGain;
    float outR = sumR * procGain + inR * bypassGain;

    // ── Acoustic Engine ────────────────────────────────────────────────────────
    if (acousticEngineOn.load()) acousticEngine.processStereo(outL, outR);

    // ── Echo Engine ────────────────────────────────────────────────────────────
    // True bypass: when echoBypass is set, the DSP call is skipped entirely
    // (not just muted) — audio passes through this stage completely
    // untouched, regardless of echoOn's stored parameters/preset.
    if (echoOn.load() && !echoBypass.load()) echoEngine.processStereo(outL, outR);

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

    // ── Recording ──────────────────────────────────────────────────────────────
    // Lock-free push into the ring; the writer thread drains and streams to
    // disk. No lock, no allocation, no growing buffer on the audio thread.
    if (recording.load(std::memory_order_relaxed)) {
        recordRing_.push(outL, outR);
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
// Spectrum
//
// Audio thread (here): O(1) per sample — just write into the current raw
// sample block. Once a block fills, hand it to the UI thread via a
// lock-free double buffer and move on; no trig, no allocation, no lock.
//
// UI thread (computeSpectrumFromBlock, called from getSpectrum): does the
// actual O(N^2) DFT. This is still a simple squared-magnitude DFT rather
// than a real FFT (a proper FFT, e.g. pffft, would cut this from O(N^2) to
// O(N log N) and is worth wiring in if the UI thread's spectrum-tick timer
// ever becomes a bottleneck) — but critically it no longer runs on the
// real-time audio thread, so it can no longer cause a dropout.
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::updateSpectrum(float l, float r) {
    fftBuf_[fftWriteSlot_][fftPos_] = 0.5f * (l + r);
    if (++fftPos_ < FFT_SIZE) return;
    fftPos_ = 0;

    // Publish the just-filled block and switch to the other one. If the UI
    // thread hasn't consumed the previous block yet, this simply overwrites
    // the "ready" index — spectrum display only needs the latest block, so
    // dropping a stale one is correct behavior, not a bug.
    fftReadyIndex_.store(fftWriteSlot_, std::memory_order_release);
    fftWriteSlot_ = 1 - fftWriteSlot_;
}

void AudioProcessor::computeSpectrumFromBlock(const std::array<float, FFT_SIZE>& block,
                                              std::vector<float>& outBins) const {
    outBins.assign(FFT_SIZE/2, 0.f);
    const float twoPiOverN = 2.f * (float)M_PI / FFT_SIZE;
    for (int k = 0; k < FFT_SIZE/2; ++k) {
        // Goertzel-style recurrence: rotate a running unit complex number by
        // a fixed per-sample angle instead of calling cos()/sin() for every
        // (k, n) pair. This replaces 2*FFT_SIZE transcendental calls per bin
        // with 2 (to seed the rotator) plus one complex multiply per sample.
        float angle = twoPiOverN * k;
        float cosA = std::cos(angle), sinA = std::sin(angle);
        float cr = 1.f, ci = 0.f; // running e^{-i*angle*n}
        double re = 0.0, im = 0.0;
        for (int n = 0; n < FFT_SIZE; ++n) {
            float w = hannWindow_[n] * block[n];
            re += w * cr;
            im -= w * ci;
            // Rotate (cr, ci) by -angle for the next n.
            float ncr = cr * cosA + ci * sinA;
            float nci = ci * cosA - cr * sinA;
            cr = ncr; ci = nci;
        }
        outBins[k] = (float)std::sqrt(re*re + im*im);
    }
}

bool AudioProcessor::getSpectrum(std::vector<float>& bins) {
    int idx = fftReadyIndex_.exchange(-1, std::memory_order_acquire);
    if (idx < 0) return false;
    computeSpectrumFromBlock(fftBuf_[idx], bins);
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Recording (WAV export)
//
// Streams directly to a temp file via a dedicated writer thread fed by a
// lock-free ring buffer, instead of accumulating the entire recording in a
// std::vector held behind a mutex the audio thread locked on every sample.
// That old design meant: an unbounded in-memory buffer (memory usage grew
// for the whole session — hours of recording could mean gigabytes of RAM),
// a mutex lock/unlock pair on every single output sample (~44100/s), and a
// vector reallocation stall whenever capacity ran out — any one of which
// could be enough to miss the audio callback's deadline and produce a
// click/dropout. The ring buffer here has a fixed, small footprint
// (~340ms of audio) regardless of how long the recording runs.
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::startRecording() {
    // Defensive: make sure a previous session's writer thread is fully
    // stopped before reusing the ring/file. Normal usage always calls
    // stopRecording() first, so this is just a safety net.
    if (recordThreadRunning_.load()) {
        recordThreadRunning_.store(false);
        if (recordThread_.joinable()) recordThread_.join();
    }
    recordRing_.reset();
    recordedFrames_.store(0);

    recordPath_ = QDir::tempPath() + QString("/bass_nuker_rec_%1.wav")
                      .arg(QDateTime::currentMSecsSinceEpoch());
    if (recordFile_.is_open()) recordFile_.close();
    recordFile_.open(recordPath_.toStdString(), std::ios::binary);
    if (!recordFile_.is_open()) return; // caller sees a failed stopRecording()
    writeWavHeaderPlaceholder(recordFile_);

    recordThreadRunning_.store(true);
    recordThread_ = std::thread(&AudioProcessor::recordThreadLoop, this);
    recording.store(true);
}

bool AudioProcessor::stopRecording(const QString& outPath) {
    recording.store(false);           // audio thread stops enqueueing frames
    recordThreadRunning_.store(false);
    if (recordThread_.joinable()) recordThread_.join(); // drains remaining frames

    uint64_t frames = recordedFrames_.load();
    if (recordFile_.is_open()) {
        patchWavHeader(recordFile_, frames);
        recordFile_.close();
    }

    if (frames == 0 || outPath.isEmpty()) {
        QFile::remove(recordPath_);
        return false;
    }

    QFile::remove(outPath); // QFile::rename() fails if the destination exists
    bool ok = QFile::rename(recordPath_, outPath);
    if (!ok) {
        // Temp dir and destination can be on different volumes; fall back
        // to copy+remove if a plain rename isn't possible.
        ok = QFile::copy(recordPath_, outPath);
        QFile::remove(recordPath_);
    }
    return ok;
}

// Runs on a dedicated low-priority thread for the duration of the recording.
// Drains the lock-free ring and appends each frame to the (already-open)
// WAV file. Sleeps briefly when idle instead of busy-spinning.
void AudioProcessor::recordThreadLoop() {
    float l, r;
    auto drainOnce = [&]() -> bool {
        bool any = false;
        while (recordFile_.is_open() && recordRing_.pop(l, r)) {
            float frame[2] = { l, r };
            recordFile_.write(reinterpret_cast<const char*>(frame), sizeof(frame));
            recordedFrames_.fetch_add(1, std::memory_order_relaxed);
            any = true;
        }
        return any;
    };
    while (recordThreadRunning_.load(std::memory_order_relaxed)) {
        if (!drainOnce()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    drainOnce(); // final flush of anything pushed just before stop
}

void AudioProcessor::writeWavHeaderPlaceholder(std::ofstream& f) {
    int channels = 2, bitsPerSample = 32;
    int sr = (int)sampleRate;
    int blockAlign = channels * bitsPerSample / 8;
    int byteRate   = sr * blockAlign;
    auto write4 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v),4); };
    auto write2 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v),2); };
    f.write("RIFF", 4); write4(0); // chunkSize — patched in patchWavHeader()
    f.write("WAVE", 4);
    f.write("fmt ", 4); write4(18); // subchunk1Size = 18 for IEEE float
    write2(3);           // AudioFormat = IEEE_FLOAT (3)
    write2(channels);
    write4(sr);
    write4(byteRate);
    write2(blockAlign);
    write2(bitsPerSample);
    write2(0);           // cbSize = 0 (no extension)
    f.write("data", 4); write4(0); // dataSize — patched in patchWavHeader()
}

void AudioProcessor::patchWavHeader(std::ofstream& f, uint64_t frames) {
    // Header layout written by writeWavHeaderPlaceholder():
    //   "RIFF"(4) + chunkSize(4) + "WAVE"(4)                = 12
    //   "fmt "(4) + 18(4) + fmt-data(18, incl. cbSize)       = 26   -> ends at 38
    //   "data"(4) + dataSize(4)                              = 8    -> ends at 46
    // So dataSize lives at byte offset 42, not 40 — the old hardcoded 40
    // clobbered the last 2 bytes of the "data" tag itself, corrupting every
    // recorded file's header (readers would fail to find a valid "data"
    // chunk and either reject the file or mis-locate the sample data,
    // producing garbled/"electric"-sounding playback).
    // Likewise chunkSize = fileSize - 8 = 46 + dataSize - 8 = 38 + dataSize,
    // not the PCM-standard 36 + dataSize (which only holds for a 16-byte
    // fmt chunk with no cbSize field).
    uint32_t dataSize  = (uint32_t)(frames * 2 * sizeof(float));
    uint32_t chunkSize = 38 + dataSize;
    f.seekp(4, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&chunkSize), 4);
    f.seekp(42, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&dataSize), 4);
}
