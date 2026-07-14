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
    LOAD_F(reverbDensity, 70.f);
    LOAD_F(reverbWetLevel, 100.f);
    LOAD_F(reverbDryLevel, 0.f);
    LOAD_F(reverbHighCut, 9000.f);

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

    // ── Device IDs (stable string identifiers) ─────────────────────────────
    LOAD_S(inputDeviceId,   "");
    LOAD_S(inputDeviceType, "loopback");
    LOAD_S(outputDeviceId,  "");
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
    SAVE(reverbHighCut);
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
    SAVE(inputDeviceId); SAVE(inputDeviceType); SAVE(outputDeviceId);
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
