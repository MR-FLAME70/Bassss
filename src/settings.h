#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <array>

// ──────────────────────────────────────────────────────────────────────────────
// AppSettings: mirrors the FACTORY_DEFAULTS / siteSettings structure from the
// original Chrome extension (background.js + popup.js). Every field name and
// default value is kept identical so presets are compatible.
// ──────────────────────────────────────────────────────────────────────────────

struct AppSettings {
    // ── Bass boost ────────────────────────────────────────────────────────────
    float frequency     = 150.f;   // Hz, bass low-shelf cutoff
    float gain          = 12.f;    // dB boost
    bool  bassOn        = true;

    // ── Volume / speed ────────────────────────────────────────────────────────
    // volume: input gain applied before the reverb/EQ chain (bass column).
    // speakerOutputGain: final output gain applied AFTER all DSP — controls
    //   the speaker level independently of reverb/echo wet levels.
    float volume        = 100.f;   // % input level (kept for backwards compat)
    float micVolume     = 100.f;   // % mic input gain (0–200)
    float speed         = 1.f;     // playback rate (live tab uses this conceptually)

    // Speaker volume — applied as the LAST gain stage, after all DSP
    // (reverb, echo, EQ, compressor, etc.) so that lowering it does not
    // change the reverb or echo wet levels in the mix.
    float speakerOutputGain = 100.f;  // % (0–200, 100 = unity)

    // ── Reverb top-level ──────────────────────────────────────────────────────
    bool  reverbOn      = false;
    QString reverbPreset = "hauntedcavernv3";
    float reverbAmount  = 100.f;   // % (Effects Amount)
    float reverbDecay   = 5.5f;    // seconds (hauntedcavernv3 default)
    float reverbMix     = 15.f;    // % wet
    float reverbPredelay = 28.f;   // ms
    float reverbDiffuse = 90.f;    // %
    float reverbToneHz  = 3600.f;  // High Cut Hz
    float reverbResonanceHz = 1000.f;
    float reverbResonanceQ  = 0.f;
    float songVolume    = 100.f;   // % dry song level

    // Reverb wet volume — scales only the reverb wet signal in the mix,
    // independently of the dry level and the speaker output gain.
    float reverbVolumeScale = 100.f;  // % (0–200, 100 = unity)

    // ── Advanced reverb engine ────────────────────────────────────────────────
    float reverbRoomSize            = 2.7f;
    float reverbEarlyReflectionDelay  = 0.f;    // ms
    float reverbEarlyReflectionLevel  = 280.f;  // %
    float reverbLateReverbLevel       = 667.f;  // %
    float reverbHfDamping             = 30.f;   // %
    float reverbLfDamping             = 12.f;   // %
    float reverbStereoWidth           = 150.f;  // % (0-200)
    float reverbModulationDepth       = 50.f;   // %
    float reverbModulationRate        = 25.f;   // %
    float reverbLowCut                = 85.f;   // Hz
    float reverbDensity               = 90.f;   // % (hauntedcavernv3)
    float reverbWetLevel              = 100.f;  // %
    float reverbDryLevel              = 0.f;    // %

    // ── Advanced audio modules ────────────────────────────────────────────────
    // EQ
    bool eqOn = false;
    QString eqPreset = "flat";
    std::array<float,10> eqBands = {0,0,0,0,0,0,0,0,0,0};

    // Compressor
    bool compOn = false;
    float compThreshold = -24.f; // dB
    float compRatio     = 4.f;
    float compAttack    = 3.f;   // ms
    float compRelease   = 250.f; // ms
    float compMakeup    = 0.f;   // dB

    // Limiter
    bool limOn = false;
    float limThreshold = -3.f;  // dB
    float limRelease   = 50.f;  // ms

    // Stereo Width
    bool stereoWidthOn = false;
    float stereoWidth  = 100.f; // %

    // Pitch
    bool pitchOn = false;
    float pitch  = 0.f; // semitones

    // Dynamic Bass
    bool dynBassOn = false;
    float dynBassSensitivity = 50.f; // %
    float dynBassStrength    = 50.f; // %

    // Spectrum
    bool spectrumOn = false;

    // ── Acoustic Engine (SBX Pro Studio) ──────────────────────────────────────
    bool acousticEngineOn = false;
    float fxSurround    = 0.f;  // %
    float fxCrystalizer = 0.f;  // %
    float fxBass        = 0.f;  // %
    float fxCrossover   = 100.f;// Hz
    float fxSmartVolume = 0.f;  // %
    float fxDialogPlus  = 0.f;  // %

    // ── Speaker Configuration ─────────────────────────────────────────────────
    bool speakerConfigOn = false;
    QString speakerMode  = "stereo";

    // Virtual Speaker Shifter
    float speakerFrontWidth    = 100.f; // %
    float speakerRearWidth     = 100.f; // %
    float speakerCenterDistance= 100.f; // %
    float speakerRearDistance  = 100.f; // %
    float speakerSubDistance   = 0.f;   // ft
    float speakerLevelFL       = 100.f; // %
    float speakerLevelFR       = 100.f; // %
    float speakerLevelC        = 100.f; // %
    float speakerLevelSub      = 100.f; // %
    float speakerLevelRL       = 100.f; // %
    float speakerLevelRR       = 100.f; // %

    // ── Advanced Reverb Engine on/off ─────────────────────────────────────────
    bool reverbEngineOn = true;

    // ── Echo Engine ────────────────────────────────────────────────────────────
    bool  echoOn      = false;
    bool  echoBypass  = false;
    QString echoPreset = "tapeecho";
    float echoDelayMs   = 350.f;  // ms
    float echoFeedback  = 55.f;   // %
    float echoMix       = 38.f;   // % wet/dry crossfade
    float echoTone      = 60.f;   // % damping (repeat darkening)
    float echoPingPong  = 15.f;   // % stereo cross-feed

    // ── Basic Echo section — additional controls ───────────────────────────
    int   echoNumEchoes  = 0;      // 0 = infinite/feedback, 1-10 = discrete taps
    float echoAmount     = 100.f;  // % input drive into the delay line
    float echoWetLevel   = 100.f;  // % wet (effected) signal level
    float echoDryLevel   = 100.f;  // % dry (original) signal level
    float echoOutputGain = 100.f;  // % post-mix output gain (100 = unity)

    // Echo wet volume — scales only the echo contribution (wet delta) in the
    // mix, independently of the dry level and the speaker output gain.
    float echoVolumeScale = 100.f;  // % (0–200, 100 = unity)

    // ── Echo Algorithm ────────────────────────────────────────────────────────
    QString echoAlgorithm = "clean";

    // ── Advanced Echo Engine ─────────────────────────────────────────────────
    bool  aeOn = false;

    // [Delay]
    float aeLeftDelayMs   = 350.f;  // ms
    float aeRightDelayMs  = 350.f;  // ms
    float aeStereoOffset  = 0.f;    // ms ±200
    float aeStereoWidthD  = 0.f;    // % 0-100
    bool  aeTempoSync     = false;
    bool  aeMillisecondMode = true;

    // [Feedback]
    float aeCrossFeedback = 0.f;     // % 0-100
    float aeFbSaturation  = 0.f;     // % 0-100
    float aeFbDamping     = 0.f;     // % 0-100
    float aeFbLowCut      = 20.f;    // Hz
    float aeFbHighCut     = 20000.f; // Hz
    float aeFbDiffusion   = 0.f;     // % 0-100

    // [Stereo]
    float aeBalance     = 0.f;    // -100..+100
    float aeLeftLevel   = 100.f;  // %
    float aeRightLevel  = 100.f;  // %
    float aeMidSideMix  = 0.f;    // %
    bool  aePingPongMode  = false;
    bool  aeSwapChannels  = false;

    // [Tone]
    float aeToneLowCut    = 20.f;    // Hz
    float aeToneHighCut   = 20000.f; // Hz
    float aeToneBass      = 0.f;     // dB
    float aeToneMid       = 0.f;     // dB
    float aeToneTreble    = 0.f;     // dB
    float aeTonePresence  = 0.f;     // dB
    float aeToneBrightness = 0.f;    // dB

    // [Saturation]
    float aeTapeSat   = 0.f;   // %
    float aeAnalogSat = 0.f;   // %
    float aeDrive     = 0.f;   // %
    float aeWarmth    = 0.f;   // %
    bool  aeSoftClip  = false;

    // [Dynamics]
    float aeInputGainDb  = 0.f;  // dB
    float aeOutputGainDb = 0.f;  // dB
    float aeWetGainDb    = 0.f;  // dB
    float aeDryGainDb    = 0.f;  // dB
    bool  aeIntLimiter   = false;
    bool  aeSoftLimiter  = false;

    // [Mix]
    float aeWetLevel2 = 100.f;  // % secondary wet level
    float aeDryLevel2 = 100.f;  // % secondary dry level
    float aeBlend     = 100.f;  // % master blend
    float aeMixOverride = -1.f; // ≥0 overrides basic mix; -1 = use basic

    // [Modulation]
    float aeWow         = 0.f;  // %
    float aeFlutter     = 0.f;  // %
    float aeModDepth    = 0.f;  // %
    float aeModRate     = 1.f;  // Hz
    float aeRandomDrift = 0.f;  // %

    // [Spatial]
    float aeHaasWidth       = 0.f;   // ms 0-40
    float aeStereoSpread    = 0.f;   // %
    float aeEarlyReflections = 0.f;  // %
    float aeReflLevel       = 0.f;   // %
    float aeReflDelay       = 20.f;  // ms

    // ── A/B bypass ───────────────────────────────────────────────────────────
    bool bypass = false;

    // ── Audio routing: source + device selection ──────────────────────────────
    QString audioSourceMode = "playback";   // "playback" | "microphone" | "both"
    QString playbackDeviceId;
    QString micDeviceId;
    QString outputDeviceId;

    // Advisory sample rate / buffer — actual rate is set by the device.
    int sampleRate = 48000;
    int bufferSize = 256;

    // Serialise / deserialise
    void load();
    void save() const;
    void resetToDefaults();
    void applyEqPreset(const QString& name);
};

// Singleton accessor
AppSettings& globalSettings();
