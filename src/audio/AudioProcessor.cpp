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

AudioProcessor::AudioProcessor() {
    meterBufL.resize(METER_BUF, 0.f);
    meterBufR.resize(METER_BUF, 0.f);

    for (int n = 0; n < FFT_SIZE; ++n)
        hannWindow_[n] = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * n / FFT_SIZE);

    limiter.setRatio(20.f);
    limiter.setAttack(0.001f);
    limiter.setKnee(0.f);
    limiter.setThreshold(-3.f);
    limiter.setRelease(0.05f);

    bassFilter.setType(BiquadFilter::LowShelf, 44100.0, 150.0, 0.707, 12.0);
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

    volumeSm_.init(sr, 8.f, volumeGain.load());
    micGainSm_.init(sr, 8.f, micGain.load());
    drySm_.init(sr, 8.f, dryGain.load());
    wetSm_.init(sr, 8.f, wetGain.load());
    procGainSm_.init(sr, 8.f, processedMasterGain_.load());
    bypassGainSm_.init(sr, 8.f, bypass.load() ? 1.f : 0.f);
    speakerOutSm_.init(sr, 8.f, speakerOutputGain_.load());
    reverbVolSm_.init(sr, 8.f, reverbVolScale_.load());
    echoVolSm_.init(sr, 8.f, echoVolScale_.load());
    smoothersInited_ = true;
}

void AudioProcessor::applySettings(const AppSettings& s) {
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

    // volumeGain is now used only as a pre-DSP input level (kept for
    // backward compat). The actual speaker output is speakerOutputGain_.
    volumeGain.store(s.volume / 100.f);
    micGain.store(s.micVolume / 100.f);

    // micInMix: true when a mic is in the signal (mic-only OR both).
    // audioSourceBoth: true only when mode=="both". In "both" mode the
    // mic frames are pre-scaled in AudioCapture::outCallback BEFORE being
    // summed with loopback, so processStereo must not re-apply micGain.
    bool isMicBoth = (s.audioSourceMode == "both");
    bool isMicMode = (s.audioSourceMode == "microphone");
    micInMix.store(isMicMode);         // processStereo applies gain in mic-only mode
    audioSourceBoth.store(isMicBoth);  // outCallback applies gain in both mode

    // Speaker output gain — applied as the final stage AFTER all DSP.
    speakerOutputGain_.store(s.speakerOutputGain / 100.f);

    // Reverb wet volume scale — multiplies only the reverb wet signal.
    reverbVolScale_.store(s.reverbVolumeScale / 100.f);

    // Echo volume scale — multiplies only the echo wet contribution.
    echoVolScale_.store(s.echoVolumeScale / 100.f);

    // ── Reverb Engine ─────────────────────────────────────────────────────────
    ReverbEngine::Params rp;

    rp.preDelay  = s.reverbPredelay / 1000.f;
    rp.decayTime = s.reverbDecay;
    rp.diffusion = s.reverbDiffuse / 100.f;
    rp.highCut   = s.reverbToneHz;
    rp.density   = s.reverbDensity / 100.f;

    if (s.reverbEngineOn) {
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
        rp.roomSize             = 1.0f;
        rp.earlyReflectionDelay = 0.f;
        rp.earlyReflectionLevel = 0.35f;
        rp.lateReverbLevel      = 1.0f;
        rp.hfDamping            = 0.f;
        rp.lfDamping            = 0.f;
        rp.stereoWidth          = 1.0f;
        rp.modulationDepth      = 0.40f;
        rp.modulationRate       = 0.35f;
        rp.lowCut               = 80.f;
        rp.wetLevel             = 1.0f;
        rp.dryLevel             = 0.f;
    }

    reverbEngine.setParams(rp);
    reverbOn.store(s.reverbOn);

    {
        float amount = std::clamp(s.reverbAmount, 0.f, 100.f) / 100.f;
        float mix    = std::clamp(s.reverbMix,    0.f, 100.f) / 100.f;
        wetGain.store(s.reverbOn ? mix * amount : 0.f);
        dryGain.store(std::max(0.f, s.songVolume / 100.f));
    }

    resonanceFilter.setType(BiquadFilter::Peaking,
                            sampleRate,
                            s.reverbResonanceHz > 20.f ? s.reverbResonanceHz : 1000.f,
                            std::max(0.01f, s.reverbResonanceQ),
                            0.f);

    rotator.setRate(s.speed * 0.15f);
    surroundGain.store(0.f);

    bypass.store(s.bypass);
    processedMasterGain_.store(s.bypass ? 0.f : 1.f);

    acousticEngineOn.store(s.acousticEngineOn);
    if (s.acousticEngineOn) {
        acousticEngine.update(s.fxSurround, s.fxCrystalizer,
                              s.fxBass, s.fxSmartVolume,
                              s.fxDialogPlus, s.fxCrossover);
    }

    eqOn.store(s.eqOn);
    if (s.eqOn) equalizer.setBands(s.eqBands);

    dynBassOn.store(s.dynBassOn);
    if (s.dynBassOn) dynamicBass.setParams(s.dynBassSensitivity, s.dynBassStrength);

    compOn.store(s.compOn);
    compressor.setThreshold(s.compThreshold);
    compressor.setRatio(s.compRatio);
    compressor.setAttack(s.compAttack / 1000.f);
    compressor.setRelease(s.compRelease / 1000.f);
    compressor.setMakeupGain(s.compMakeup);

    limOn.store(s.limOn);
    limiter.setThreshold(s.limThreshold);
    limiter.setRelease(s.limRelease / 1000.f);

    stereoWidthOn.store(s.stereoWidthOn);
    if (s.stereoWidthOn) stereoWidth.setWidth(s.stereoWidth);

    pitchOn.store(s.pitchOn);
    if (s.pitchOn) pitchShifter.setPitchSemitones(s.pitch);

    // ── Echo Engine ────────────────────────────────────────────────────────────
    echoOn.store(s.echoOn);
    echoBypass.store(s.echoBypass);
    if (s.echoOn) {
        EchoEngine::Params ep;

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

        ep.aeOn = s.aeOn;
        ep.aeLeftDelayMs  = s.aeOn ? s.aeLeftDelayMs  : s.echoDelayMs;
        ep.aeRightDelayMs = s.aeOn ? s.aeRightDelayMs : s.echoDelayMs;

        if (s.aeOn) {
            ep.aeStereoOffset  = s.aeStereoOffset;
            ep.aeStereoWidthD  = s.aeStereoWidthD  / 100.f;
            ep.aeTempoSync     = s.aeTempoSync;
            ep.aeMillisecondMode = s.aeMillisecondMode;
            ep.aeCrossFeedback = s.aeCrossFeedback / 100.f;
            ep.aeFbSaturation  = s.aeFbSaturation  / 100.f;
            ep.aeFbDamping     = s.aeFbDamping     / 100.f;
            ep.aeFbLowCut      = s.aeFbLowCut;
            ep.aeFbHighCut     = s.aeFbHighCut;
            ep.aeFbDiffusion   = s.aeFbDiffusion   / 100.f;
            ep.aeBalance       = s.aeBalance       / 100.f;
            ep.aeLeftLevel     = s.aeLeftLevel      / 100.f;
            ep.aeRightLevel    = s.aeRightLevel     / 100.f;
            ep.aeMidSideMix    = s.aeMidSideMix    / 100.f;
            ep.aePingPongMode  = s.aePingPongMode;
            ep.aeSwapChannels  = s.aeSwapChannels;
            ep.aeToneLowCut    = s.aeToneLowCut;
            ep.aeToneHighCut   = s.aeToneHighCut;
            ep.aeToneBass      = s.aeToneBass;
            ep.aeToneMid       = s.aeToneMid;
            ep.aeToneTreble    = s.aeToneTreble;
            ep.aeTonePresence  = s.aeTonePresence;
            ep.aeToneBrightness= s.aeToneBrightness;
            ep.aeTapeSat       = s.aeTapeSat        / 100.f;
            ep.aeAnalogSat     = s.aeAnalogSat      / 100.f;
            ep.aeDrive         = s.aeDrive          / 100.f;
            ep.aeWarmth        = s.aeWarmth         / 100.f;
            ep.aeSoftClip      = s.aeSoftClip;
            ep.aeInputGainDb   = s.aeInputGainDb;
            ep.aeOutputGainDb  = s.aeOutputGainDb;
            ep.aeWetGainDb     = s.aeWetGainDb;
            ep.aeDryGainDb     = s.aeDryGainDb;
            ep.aeIntLimiter    = s.aeIntLimiter;
            ep.aeSoftLimiter   = s.aeSoftLimiter;
            ep.aeWetLevel2     = s.aeWetLevel2      / 100.f;
            ep.aeDryLevel2     = s.aeDryLevel2      / 100.f;
            ep.aeBlend         = s.aeBlend          / 100.f;
            ep.aeMix           = (s.aeMixOverride >= 0.f)
                                   ? s.aeMixOverride / 100.f
                                   : -1.f;
            ep.aeWow           = s.aeWow            / 100.f;
            ep.aeFlutter       = s.aeFlutter        / 100.f;
            ep.aeModDepth      = s.aeModDepth       / 100.f;
            ep.aeModRate       = s.aeModRate;
            ep.aeRandomDrift   = s.aeRandomDrift    / 100.f;
            ep.aeHaasWidth        = s.aeHaasWidth;
            ep.aeStereoSpread     = s.aeStereoSpread / 100.f;
            ep.aeEarlyReflections = s.aeEarlyReflections / 100.f;
            ep.aeReflLevel        = s.aeReflLevel    / 100.f;
            ep.aeReflDelay        = s.aeReflDelay;
        }

        if (!s.aeOn) {
            const QString& alg = s.echoAlgorithm;
            if (alg == "digital") {
                ep.aeOn = true;
            } else if (alg == "analog") {
                ep.aeOn          = true;
                ep.aeAnalogSat   = 0.20f;
                ep.aeWarmth      = 0.15f;
                ep.aeFbHighCut   = 8000.f;
                ep.aeWow         = 0.05f;
                ep.aeModDepth    = 0.15f;
                ep.aeModRate     = 0.6f;
            } else if (alg == "tape") {
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
                ep.aeOn          = true;
                ep.aeTapeSat     = 0.20f;
                ep.aeWarmth      = 0.30f;
                ep.aeToneBass    = 2.f;
                ep.aeToneTreble  = -2.f;
                ep.aeFbHighCut   = 9000.f;
            } else if (alg == "dark") {
                ep.aeOn          = true;
                ep.aeTapeSat     = 0.30f;
                ep.aeWarmth      = 0.20f;
                ep.aeFbHighCut   = 3000.f;
                ep.aeToneHighCut = 8000.f;
                ep.aeToneBass    = 3.f;
                ep.aeToneTreble  = -4.f;
                ep.aeFbDamping   = 0.20f;
            } else if (alg == "bright") {
                ep.aeOn           = true;
                ep.aeAnalogSat    = 0.10f;
                ep.aeToneTreble   = 3.f;
                ep.aeTonePresence = 2.f;
                ep.aeToneBrightness = 2.f;
            } else if (alg == "lofi") {
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
        }

        echoEngine.setParams(ep);
    }

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

    spectrumOn.store(s.spectrumOn);
}

void AudioProcessor::consumePendingSettings() {
    int idx = settingsReadyIndex_.exchange(-1, std::memory_order_acquire);
    if (idx < 0) return;
    applySettingsInternal(settingsBuf_[idx]);
}

// ──────────────────────────────────────────────────────────────────────────────
// Main audio processing entry-point (called from PortAudio callback thread)
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::processStereo(float& l, float& r) {
    consumePendingSettings();

    if (!enabled.load()) {
        return;
    }

    if (!smoothersInited_) setSampleRate(sampleRate);

    float inL = l, inR = r;

    // ── Smoothed control-rate gains ───────────────────────────────────────────
    speakerOutSm_.setTarget(speakerOutputGain_.load());
    reverbVolSm_.setTarget(reverbVolScale_.load());
    echoVolSm_.setTarget(echoVolScale_.load());
    volumeSm_.setTarget(volumeGain.load());
    drySm_.setTarget(dryGain.load());
    wetSm_.setTarget(wetGain.load());
    procGainSm_.setTarget(processedMasterGain_.load());
    bypassGainSm_.setTarget(bypass.load() ? 1.f : 0.f);
    micGainSm_.setTarget(micGain.load());

    // ── Mic gain (mic-only mode only) ─────────────────────────────────────────
    // In "microphone" mode: apply gain here — the mic signal IS the primary input.
    // In "both" mode: AudioCapture::outCallback already scaled the mic ring
    //   frames before summing with loopback, so we skip here to avoid
    //   applying the gain twice.
    // In "playback" mode: micInMix=false, skip (no mic in the signal at all).
    if (micInMix.load() && !audioSourceBoth.load()) {
        float mg = micGainSm_.next();
        l *= mg; r *= mg;
    } else {
        micGainSm_.next(); // keep smoother in sync
    }

    // ── Bass low-shelf + pre-DSP volume ───────────────────────────────────────
    bassFilter.processStereo(l, r);
    float vol = volumeSm_.next();
    l *= vol; r *= vol;

    // ── Dry / Wet split ───────────────────────────────────────────────────────
    float dry = drySm_.next();
    float dl = l * dry;
    float dr = r * dry;

    float wl = l, wr = r;
    float reverbVol = reverbVolSm_.next();
    if (reverbOn.load()) {
        float revOutL = 0.f, revOutR = 0.f;
        reverbEngine.processStereo(wl, wr, revOutL, revOutR);

        resonanceFilter.processStereo(revOutL, revOutR);

        float wet = wetSm_.next();
        // Apply both the outer wet gain AND the independent reverb volume scale.
        wl = revOutL * wet * reverbVol;
        wr = revOutR * wet * reverbVol;
    } else {
        wetSm_.next();
        reverbVolSm_.next(); // keep smoother in sync even when reverb is off
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
    // echoVolScale scales only the echo wet contribution (the delta between
    // post-echo and pre-echo) so the dry signal is unaffected.
    if (echoOn.load() && !echoBypass.load()) {
        float preL = outL, preR = outR;
        echoEngine.processStereo(outL, outR);
        float echoVol = echoVolSm_.next();
        // Mix = dry (unchanged) + echo_wet_delta * echoVol
        outL = preL + (outL - preL) * echoVol;
        outR = preR + (outR - preR) * echoVol;
    } else {
        echoVolSm_.next(); // keep smoother in sync
    }

    // ── Advanced Audio chain ──────────────────────────────────────────────────
    if (eqOn.load())          equalizer.processStereo(outL, outR);
    if (dynBassOn.load())     dynamicBass.processStereo(outL, outR);
    if (compOn.load())        compressor.processStereo(outL, outR);
    if (limOn.load())         limiter.processStereo(outL, outR);
    if (stereoWidthOn.load()) stereoWidth.processStereo(outL, outR);
    if (pitchOn.load())       pitchShifter.processStereo(outL, outR);

    // ── Speaker Config ────────────────────────────────────────────────────────
    if (speakerConfigOn.load()) speakerConfig.processStereo(outL, outR);

    // ── Speaker Output Gain ───────────────────────────────────────────────────
    // Applied LAST — after all reverb, echo, EQ, compression, etc.
    // Lowering this slider reduces the final output level without changing
    // the reverb/echo wet-to-dry ratio in the mix.
    {
        float spkVol = speakerOutSm_.next();
        outL *= spkVol;
        outR *= spkVol;
    }

    // ── Spectrum / VU metering ────────────────────────────────────────────────
    if (spectrumOn.load()) updateSpectrum(outL, outR);
    {
        meterBufL[meterWritePos] = outL;
        meterBufR[meterWritePos] = outR;
        meterWritePos = (meterWritePos + 1) % METER_BUF;
    }

    // ── Clamp (soft tanh at 0.8) ──────────────────────────────────────────────
    clamp.processStereo(outL, outR);

    l = outL;
    r = outR;

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
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::updateSpectrum(float l, float r) {
    fftBuf_[fftWriteSlot_][fftPos_] = 0.5f * (l + r);
    if (++fftPos_ < FFT_SIZE) return;
    fftPos_ = 0;
    fftReadyIndex_.store(fftWriteSlot_, std::memory_order_release);
    fftWriteSlot_ = 1 - fftWriteSlot_;
}

void AudioProcessor::computeSpectrumFromBlock(const std::array<float, FFT_SIZE>& block,
                                              std::vector<float>& outBins) const {
    outBins.assign(FFT_SIZE/2, 0.f);
    const float twoPiOverN = 2.f * (float)M_PI / FFT_SIZE;
    for (int k = 0; k < FFT_SIZE/2; ++k) {
        float angle = twoPiOverN * k;
        float cosA = std::cos(angle), sinA = std::sin(angle);
        float cr = 1.f, ci = 0.f;
        double re = 0.0, im = 0.0;
        for (int n = 0; n < FFT_SIZE; ++n) {
            float w = hannWindow_[n] * block[n];
            re += w * cr;
            im -= w * ci;
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
// Recording
// ──────────────────────────────────────────────────────────────────────────────
void AudioProcessor::startRecording() {
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
    if (!recordFile_.is_open()) return;
    writeWavHeaderPlaceholder(recordFile_);

    recordThreadRunning_.store(true);
    recordThread_ = std::thread(&AudioProcessor::recordThreadLoop, this);
    recording.store(true);
}

bool AudioProcessor::stopRecording(const QString& outPath) {
    recording.store(false);
    recordThreadRunning_.store(false);
    if (recordThread_.joinable()) recordThread_.join();

    uint64_t frames = recordedFrames_.load();
    if (recordFile_.is_open()) {
        patchWavHeader(recordFile_, frames);
        recordFile_.close();
    }

    if (frames == 0 || outPath.isEmpty()) {
        QFile::remove(recordPath_);
        return false;
    }

    QFile::remove(outPath);
    bool ok = QFile::rename(recordPath_, outPath);
    if (!ok) {
        ok = QFile::copy(recordPath_, outPath);
        QFile::remove(recordPath_);
    }
    return ok;
}

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
    drainOnce();
}

void AudioProcessor::writeWavHeaderPlaceholder(std::ofstream& f) {
    int channels = 2, bitsPerSample = 32;
    int sr = (int)sampleRate;
    int blockAlign = channels * bitsPerSample / 8;
    int byteRate   = sr * blockAlign;
    auto write4 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v),4); };
    auto write2 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v),2); };
    f.write("RIFF", 4); write4(0);
    f.write("WAVE", 4);
    f.write("fmt ", 4); write4(18);
    write2(3); write2(channels);
    write4(sr); write4(byteRate);
    write2(blockAlign); write2(bitsPerSample);
    write2(0);
    f.write("data", 4); write4(0);
}

void AudioProcessor::patchWavHeader(std::ofstream& f, uint64_t frames) {
    uint32_t dataSize  = (uint32_t)(frames * 2 * sizeof(float));
    uint32_t chunkSize = 38 + dataSize;
    f.seekp(4, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&chunkSize), 4);
    f.seekp(42, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&dataSize), 4);
}
