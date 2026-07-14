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
    QString reverbPreset = "hauntedcavernv2";
    float reverbAmount  = 100.f;   // % (Effects Amount)
    float reverbDecay   = 9.0f;    // seconds (hauntedcavernv2 default)
    float reverbMix     = 74.f;    // % wet
    float reverbPredelay = 38.f;   // ms
    float reverbDiffuse = 78.f;    // %
    float reverbToneHz  = 3600.f;  // High Cut Hz — single tone/high-cut control,
                                    // shared by the Basic "Tone" slider and the
                                    // Advanced "High Cut" slider (same field in
                                    // the original extension: reverbFrequency /
                                    // reverbHighCut both edit currentTone.toneHz).
    float reverbResonanceHz = 1000.f;
    float reverbResonanceQ  = 0.f;
    float songVolume    = 100.f;   // % dry song level

    // ── Advanced reverb engine ────────────────────────────────────────────────
    float reverbRoomSize            = 2.6f;
    float reverbEarlyReflectionDelay  = 0.f;    // ms
    float reverbEarlyReflectionLevel  = 38.f;   // %
    float reverbLateReverbLevel       = 100.f;  // %
    float reverbHfDamping             = 35.f;   // %
    float reverbLfDamping             = 15.f;   // %
    float reverbStereoWidth           = 165.f;  // % (0-200)
    float reverbModulationDepth       = 55.f;   // %
    float reverbModulationRate        = 30.f;   // %
    float reverbLowCut                = 90.f;   // Hz
    float reverbDensity               = 78.f;   // % (hauntedcavernv2 falls back to its own diffuse=78)
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
    bool reverbEngineOn = false;

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
