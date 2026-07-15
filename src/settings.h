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
    float volume        = 100.f;   // % of original
    float speed         = 1.f;     // playback rate (live tab uses this conceptually)

    // ── Reverb top-level ──────────────────────────────────────────────────────
    bool  reverbOn      = false;
    QString reverbPreset = "hauntedcavernv3";
    float reverbAmount  = 100.f;   // % (Effects Amount)
    float reverbDecay   = 5.5f;    // seconds (hauntedcavernv3 default)
    float reverbMix     = 15.f;    // % wet
    float reverbPredelay = 28.f;   // ms
    float reverbDiffuse = 90.f;    // %
    float reverbToneHz  = 3600.f;  // High Cut Hz — single tone/high-cut control,
                                    // shared by the Basic "Tone" slider and the
                                    // Advanced "High Cut" slider (same field in
                                    // the original extension: reverbFrequency /
                                    // reverbHighCut both edit currentTone.toneHz).
    float reverbResonanceHz = 1000.f;
    float reverbResonanceQ  = 0.f;
    float songVolume    = 100.f;   // % dry song level

    // ── Advanced reverb engine ────────────────────────────────────────────────
    // Defaults match the hauntedcavernv3 preset exactly so the first-run
    // sound is identical to the Chrome extension's Haunted Cavern v3 sound.
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
    // Default true so the hauntedcavernv3 preset values above actually reach
    // the DSP on first run (matches Chrome extension behaviour).
    bool reverbEngineOn = true;

    // ── Echo Engine ────────────────────────────────────────────────────────────
    // Real-time feedback delay line (see dsp/EchoEngine.h). `echoOn` is the
    // module power switch (included in / excluded from the signal chain);
    // `echoBypass` is a separate true-bypass switch — when set, the DSP is
    // skipped entirely (input passed through unmodified) even if echoOn is
    // true, so parameters/preset stay dialed in while A/B-comparing, exactly
    // like a bypass footswitch on a hardware delay pedal.
    bool  echoOn      = false;
    bool  echoBypass  = false;
    QString echoPreset = "tapeecho";
    float echoDelayMs   = 350.f;  // ms
    float echoFeedback  = 55.f;   // %
    float echoMix       = 38.f;   // % wet/dry crossfade
    float echoTone      = 60.f;   // % damping (repeat darkening)
    float echoPingPong  = 15.f;   // % stereo cross-feed

    // ── Basic Echo section — additional controls ───────────────────────────
    // These map to the extended EchoEngine::Params fields added alongside
    // the Basic Echo UI panel. All stored as % (0-100) except numEchoes
    // (integer 0-10) and outputGain (0-200 % so unity = 100).
    int   echoNumEchoes  = 0;      // 0 = infinite/feedback, 1-10 = discrete taps
    float echoAmount     = 100.f;  // % input drive into the delay line
    float echoWetLevel   = 100.f;  // % wet (effected) signal level
    float echoDryLevel   = 100.f;  // % dry (original) signal level
    float echoOutputGain = 100.f;  // % post-mix output gain (100 = unity)

    // ── Echo Algorithm ────────────────────────────────────────────────────────
    // Selects the sonic character of the echo engine. Applied automatically
    // via the DSP layer (no Advanced Echo Engine toggle required). When the
    // Advanced Echo Engine is also enabled, user AE settings take precedence.
    // Values: "digital" | "analog" | "tape" | "bucketbrigade" | "vintage" |
    //         "clean" | "warm" | "dark" | "bright" | "lofi"
    QString echoAlgorithm = "clean";

    // ── Advanced Echo Engine ─────────────────────────────────────────────────
    // All stored in natural UI units (ms, dB, %, Hz, bool). The AudioProcessor
    // converts to normalised EchoEngine::Params values before calling setParams.
    // `aeOn` is the Advanced Echo Engine section's collapse/enable toggle;
    // when false all ae* params are passed as neutral defaults so the DSP
    // sounds identical to the basic echo path.
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
    // Stored as stable string IDs rather than PortAudio indices (which change
    // between sessions as devices are added/removed).
    //
    // audioSourceMode  — "playback" or "microphone". Determines which of the
    //                    two device IDs below is actually used for capture.
    //                    Defaults to "playback" so the app NEVER starts
    //                    listening on the microphone unless the user
    //                    explicitly selects it.
    // playbackDeviceId — Windows render-endpoint ID (IMMDevice::GetId) used
    //                    for WASAPI loopback capture when audioSourceMode ==
    //                    "playback"; empty = system default playback device.
    // micDeviceId      — Windows capture-endpoint ID (IMMDevice::GetId) used
    //                    for direct microphone capture when audioSourceMode
    //                    == "microphone"; empty = system default microphone.
    // outputDeviceId   — "<paIndex>:<name>" for the PortAudio render device
    //                    the processed signal is played back to; empty =
    //                    system default.
    //
    // Each of the three IDs is remembered independently, so switching the
    // Audio Source back and forth restores whatever device was last picked
    // for that source instead of forgetting it.
    QString audioSourceMode = "playback";   // "playback" | "microphone"
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
