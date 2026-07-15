#include "settings.h"
#include <QSettings>
#include <QCoreApplication>

static AppSettings s_instance;

AppSettings& globalSettings() { return s_instance; }

static const char* ORG  = "BassNuker";
static const char* APP  = "BassNuker";

void AppSettings::load() {
    QSettings q(ORG, APP);
#define LOAD_F(k, def) k = q.value(#k, def).toFloat()
#define LOAD_B(k, def) k = q.value(#k, def).toBool()
#define LOAD_S(k, def) k = q.value(#k, def).toString()
#define LOAD_I(k, def) k = q.value(#k, def).toInt()

    LOAD_F(frequency, 150.f);
    LOAD_F(gain, 12.f);
    LOAD_B(bassOn, true);
    LOAD_F(volume, 100.f);
    LOAD_F(speed, 1.f);

    LOAD_B(reverbOn, false);
    LOAD_S(reverbPreset, "hauntedcavernv2");
    LOAD_F(reverbAmount, 100.f);
    LOAD_F(reverbDecay, 9.0f);
    LOAD_F(reverbMix, 74.f);
    LOAD_F(reverbPredelay, 38.f);
    LOAD_F(reverbDiffuse, 78.f);
    LOAD_F(reverbToneHz, 3600.f);
    LOAD_F(reverbResonanceHz, 1000.f);
    LOAD_F(reverbResonanceQ, 0.f);
    LOAD_F(songVolume, 100.f);
    LOAD_F(reverbRoomSize, 2.6f);
    LOAD_F(reverbEarlyReflectionDelay, 0.f);
    LOAD_F(reverbEarlyReflectionLevel, 38.f);
    LOAD_F(reverbLateReverbLevel, 100.f);
    LOAD_F(reverbHfDamping, 35.f);
    LOAD_F(reverbLfDamping, 15.f);
    LOAD_F(reverbStereoWidth, 165.f);
    LOAD_F(reverbModulationDepth, 55.f);
    LOAD_F(reverbModulationRate, 30.f);
    LOAD_F(reverbLowCut, 90.f);
    LOAD_F(reverbDensity, 78.f);
    LOAD_F(reverbWetLevel, 100.f);
    LOAD_F(reverbDryLevel, 0.f);

    LOAD_B(eqOn, false);
    LOAD_S(eqPreset, "flat");
    q.beginReadArray("eqBands");
    for (int i = 0; i < 10; ++i) {
        q.setArrayIndex(i);
        eqBands[i] = q.value("v", 0.f).toFloat();
    }
    q.endArray();

    LOAD_B(compOn, false);
    LOAD_F(compThreshold, -24.f);
    LOAD_F(compRatio, 4.f);
    LOAD_F(compAttack, 3.f);
    LOAD_F(compRelease, 250.f);
    LOAD_F(compMakeup, 0.f);

    LOAD_B(limOn, false);
    LOAD_F(limThreshold, -3.f);
    LOAD_F(limRelease, 50.f);

    LOAD_B(stereoWidthOn, false);
    LOAD_F(stereoWidth, 100.f);

    LOAD_B(pitchOn, false);
    LOAD_F(pitch, 0.f);

    LOAD_B(dynBassOn, false);
    LOAD_F(dynBassSensitivity, 50.f);
    LOAD_F(dynBassStrength, 50.f);

    LOAD_B(spectrumOn, false);

    LOAD_B(acousticEngineOn, false);
    LOAD_F(fxSurround, 0.f);
    LOAD_F(fxCrystalizer, 0.f);
    LOAD_F(fxBass, 0.f);
    LOAD_F(fxCrossover, 100.f);
    LOAD_F(fxSmartVolume, 0.f);
    LOAD_F(fxDialogPlus, 0.f);

    LOAD_B(speakerConfigOn, false);
    LOAD_S(speakerMode, "stereo");
    LOAD_F(speakerFrontWidth, 100.f);
    LOAD_F(speakerRearWidth, 100.f);
    LOAD_F(speakerCenterDistance, 100.f);
    LOAD_F(speakerRearDistance, 100.f);
    LOAD_F(speakerSubDistance, 0.f);
    LOAD_F(speakerLevelFL, 100.f);
    LOAD_F(speakerLevelFR, 100.f);
    LOAD_F(speakerLevelC, 100.f);
    LOAD_F(speakerLevelSub, 100.f);
    LOAD_F(speakerLevelRL, 100.f);
    LOAD_F(speakerLevelRR, 100.f);

    LOAD_B(reverbEngineOn, false);
    LOAD_B(bypass, false);

    LOAD_B(echoOn, false);
    LOAD_B(echoBypass, false);
    LOAD_S(echoPreset, "tapeecho");
    LOAD_F(echoDelayMs, 350.f);
    LOAD_F(echoFeedback, 55.f);
    LOAD_F(echoMix, 38.f);
    LOAD_F(echoTone, 60.f);
    LOAD_F(echoPingPong, 15.f);
    LOAD_I(echoNumEchoes, 0);
    LOAD_F(echoAmount, 100.f);
    LOAD_F(echoWetLevel, 100.f);
    LOAD_F(echoDryLevel, 100.f);
    LOAD_F(echoOutputGain, 100.f);

    // ── Advanced Echo Engine ─────────────────────────────────────────────────
    LOAD_B(aeOn, false);
    LOAD_F(aeLeftDelayMs,   350.f); LOAD_F(aeRightDelayMs,  350.f);
    LOAD_F(aeStereoOffset,    0.f); LOAD_F(aeStereoWidthD,    0.f);
    LOAD_B(aeTempoSync, false);     LOAD_B(aeMillisecondMode, true);
    LOAD_F(aeCrossFeedback,   0.f); LOAD_F(aeFbSaturation,    0.f);
    LOAD_F(aeFbDamping,       0.f); LOAD_F(aeFbLowCut,       20.f);
    LOAD_F(aeFbHighCut,   20000.f); LOAD_F(aeFbDiffusion,     0.f);
    LOAD_F(aeBalance,         0.f); LOAD_F(aeLeftLevel,     100.f);
    LOAD_F(aeRightLevel,  100.f);   LOAD_F(aeMidSideMix,      0.f);
    LOAD_B(aePingPongMode, false);  LOAD_B(aeSwapChannels, false);
    LOAD_F(aeToneLowCut,    20.f);  LOAD_F(aeToneHighCut, 20000.f);
    LOAD_F(aeToneBass,       0.f);  LOAD_F(aeToneMid,         0.f);
    LOAD_F(aeToneTreble,     0.f);  LOAD_F(aeTonePresence,    0.f);
    LOAD_F(aeToneBrightness, 0.f);
    LOAD_F(aeTapeSat,        0.f);  LOAD_F(aeAnalogSat,       0.f);
    LOAD_F(aeDrive,          0.f);  LOAD_F(aeWarmth,          0.f);
    LOAD_B(aeSoftClip, false);
    LOAD_F(aeInputGainDb,    0.f);  LOAD_F(aeOutputGainDb,    0.f);
    LOAD_F(aeWetGainDb,      0.f);  LOAD_F(aeDryGainDb,       0.f);
    LOAD_B(aeIntLimiter, false);    LOAD_B(aeSoftLimiter, false);
    LOAD_F(aeWetLevel2,  100.f);    LOAD_F(aeDryLevel2,   100.f);
    LOAD_F(aeBlend,      100.f);    LOAD_F(aeMixOverride,  -1.f);
    LOAD_F(aeWow,          0.f);    LOAD_F(aeFlutter,        0.f);
    LOAD_F(aeModDepth,     0.f);    LOAD_F(aeModRate,        1.f);
    LOAD_F(aeRandomDrift,  0.f);
    LOAD_F(aeHaasWidth,    0.f);    LOAD_F(aeStereoSpread,   0.f);
    LOAD_F(aeEarlyReflections, 0.f);LOAD_F(aeReflLevel,      0.f);
    LOAD_F(aeReflDelay,   20.f);

    // ── Audio routing: source + device IDs (stable string identifiers) ─────
    LOAD_S(audioSourceMode,  "playback");
    LOAD_S(playbackDeviceId, "");
    LOAD_S(micDeviceId,      "");
    LOAD_S(outputDeviceId,   "");
    LOAD_I(sampleRate, 48000);
    LOAD_I(bufferSize, 256);

#undef LOAD_F
#undef LOAD_B
#undef LOAD_S
#undef LOAD_I
}

void AppSettings::save() const {
    QSettings q(ORG, APP);
#define SAVE(k) q.setValue(#k, k)
    SAVE(frequency); SAVE(gain); SAVE(bassOn);
    SAVE(volume); SAVE(speed);
    SAVE(reverbOn); SAVE(reverbPreset); SAVE(reverbAmount);
    SAVE(reverbDecay); SAVE(reverbMix); SAVE(reverbPredelay);
    SAVE(reverbDiffuse); SAVE(reverbToneHz); SAVE(reverbResonanceHz);
    SAVE(reverbResonanceQ); SAVE(songVolume);
    SAVE(reverbRoomSize); SAVE(reverbEarlyReflectionDelay);
    SAVE(reverbEarlyReflectionLevel); SAVE(reverbLateReverbLevel);
    SAVE(reverbHfDamping); SAVE(reverbLfDamping);
    SAVE(reverbStereoWidth); SAVE(reverbModulationDepth);
    SAVE(reverbModulationRate); SAVE(reverbLowCut);
    SAVE(reverbDensity); SAVE(reverbWetLevel); SAVE(reverbDryLevel);
    SAVE(eqOn); SAVE(eqPreset);
    q.beginWriteArray("eqBands");
    for (int i = 0; i < 10; ++i) {
        q.setArrayIndex(i);
        q.setValue("v", eqBands[i]);
    }
    q.endArray();
    SAVE(compOn); SAVE(compThreshold); SAVE(compRatio);
    SAVE(compAttack); SAVE(compRelease); SAVE(compMakeup);
    SAVE(limOn); SAVE(limThreshold); SAVE(limRelease);
    SAVE(stereoWidthOn); SAVE(stereoWidth);
    SAVE(pitchOn); SAVE(pitch);
    SAVE(dynBassOn); SAVE(dynBassSensitivity); SAVE(dynBassStrength);
    SAVE(spectrumOn);
    SAVE(acousticEngineOn); SAVE(fxSurround); SAVE(fxCrystalizer);
    SAVE(fxBass); SAVE(fxCrossover); SAVE(fxSmartVolume); SAVE(fxDialogPlus);
    SAVE(speakerConfigOn); SAVE(speakerMode);
    SAVE(speakerFrontWidth); SAVE(speakerRearWidth);
    SAVE(speakerCenterDistance); SAVE(speakerRearDistance);
    SAVE(speakerSubDistance);
    SAVE(speakerLevelFL); SAVE(speakerLevelFR); SAVE(speakerLevelC);
    SAVE(speakerLevelSub); SAVE(speakerLevelRL); SAVE(speakerLevelRR);
    SAVE(reverbEngineOn); SAVE(bypass);
    SAVE(echoOn); SAVE(echoBypass); SAVE(echoPreset);
    SAVE(echoDelayMs); SAVE(echoFeedback); SAVE(echoMix);
    SAVE(echoTone); SAVE(echoPingPong);
    SAVE(echoNumEchoes); SAVE(echoAmount); SAVE(echoWetLevel);
    SAVE(echoDryLevel); SAVE(echoOutputGain);
    // Advanced Echo Engine
    SAVE(aeOn);
    SAVE(aeLeftDelayMs); SAVE(aeRightDelayMs); SAVE(aeStereoOffset); SAVE(aeStereoWidthD);
    SAVE(aeTempoSync); SAVE(aeMillisecondMode);
    SAVE(aeCrossFeedback); SAVE(aeFbSaturation); SAVE(aeFbDamping);
    SAVE(aeFbLowCut); SAVE(aeFbHighCut); SAVE(aeFbDiffusion);
    SAVE(aeBalance); SAVE(aeLeftLevel); SAVE(aeRightLevel); SAVE(aeMidSideMix);
    SAVE(aePingPongMode); SAVE(aeSwapChannels);
    SAVE(aeToneLowCut); SAVE(aeToneHighCut); SAVE(aeToneBass); SAVE(aeToneMid);
    SAVE(aeToneTreble); SAVE(aeTonePresence); SAVE(aeToneBrightness);
    SAVE(aeTapeSat); SAVE(aeAnalogSat); SAVE(aeDrive); SAVE(aeWarmth); SAVE(aeSoftClip);
    SAVE(aeInputGainDb); SAVE(aeOutputGainDb); SAVE(aeWetGainDb); SAVE(aeDryGainDb);
    SAVE(aeIntLimiter); SAVE(aeSoftLimiter);
    SAVE(aeWetLevel2); SAVE(aeDryLevel2); SAVE(aeBlend); SAVE(aeMixOverride);
    SAVE(aeWow); SAVE(aeFlutter); SAVE(aeModDepth); SAVE(aeModRate); SAVE(aeRandomDrift);
    SAVE(aeHaasWidth); SAVE(aeStereoSpread); SAVE(aeEarlyReflections);
    SAVE(aeReflLevel); SAVE(aeReflDelay);
    SAVE(audioSourceMode); SAVE(playbackDeviceId); SAVE(micDeviceId); SAVE(outputDeviceId);
    SAVE(sampleRate); SAVE(bufferSize);
#undef SAVE
}

void AppSettings::resetToDefaults() {
    *this = AppSettings{};
}

void AppSettings::applyEqPreset(const QString& name) {
    // Matches the EQ presets in the original popup.js
    eqPreset = name;
    if (name == "flat") {
        eqBands = {0,0,0,0,0,0,0,0,0,0};
    } else if (name == "bassboost") {
        eqBands = {8,7,6,3,1,0,0,0,0,0};
    } else if (name == "vocal") {
        eqBands = {-2,-1,0,2,4,5,4,2,0,-1};
    } else if (name == "rock") {
        eqBands = {5,4,2,0,-1,-1,0,3,4,4};
    } else if (name == "pop") {
        eqBands = {-1,0,2,4,4,2,0,-1,-1,-1};
    } else if (name == "edm") {
        eqBands = {6,5,2,-1,-2,-1,2,4,5,5};
    } else if (name == "cinema") {
        eqBands = {4,3,2,1,0,0,2,3,4,3};
    } else if (name == "gaming") {
        eqBands = {4,3,1,0,2,3,3,2,3,3};
    }
}
