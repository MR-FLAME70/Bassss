#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include "CustomWidgets.h"
#include "../settings.h"

// ──────────────────────────────────────────────────────────────────────────────
// AdvancedTab — matches the "Advanced" tab from popup.html.
// Contains:
//   1. Acoustic Engine (SBX Pro Studio): Surround, Crystalizer, Bass,
//      Crossover, Smart Volume, Dialog Plus
//   2. Speaker Configuration: mode select + Virtual Speaker Shifter
//      (Front width, Rear width, Center distance, Rear distance,
//       Sub distance, per-channel levels FL/FR/C/Sub/RL/RR)
//   3. Advanced Reverb Engine: Room Size, Early Reflections, HF/LF damping,
//      Stereo Width, Modulation depth/rate, Low Cut, Density, Wet/Dry
// ──────────────────────────────────────────────────────────────────────────────
class AdvancedTab : public QWidget {
    Q_OBJECT
public:
    explicit AdvancedTab(QWidget* parent = nullptr);
    void refreshFromSettings(const AppSettings& s);

signals:
    void settingsChanged(const AppSettings& s);

private:
    AppSettings m_settings;

    // Acoustic engine
    ToggleSwitch* toggleAcoustic;
    DarkSlider*   slSurround, *slCrystal, *slBass, *slCrossover, *slSmartVol, *slDialog;
    QLabel*       lblSurround, *lblCrystal, *lblBass, *lblCrossover, *lblSmartVol, *lblDialog;

    // Speaker config
    ToggleSwitch* toggleSpeaker;
    QComboBox*    comboSpeakerMode;
    DarkSlider*   slFrontW, *slRearW, *slCenterDist, *slRearDist, *slSubDist;
    QLabel*       lblFrontW, *lblRearW, *lblCenterDist, *lblRearDist, *lblSubDist;
    DarkSlider*   slLvFL, *slLvFR, *slLvC, *slLvSub, *slLvRL, *slLvRR;
    QLabel*       lblLvFL, *lblLvFR, *lblLvC, *lblLvSub, *lblLvRL, *lblLvRR;

    // Advanced reverb
    ToggleSwitch* toggleReverbEngine;
    DarkSlider*   slRoomSize, *slERLevel, *slLateLevel, *slHfDamp, *slLfDamp;
    DarkSlider*   slRevWidth, *slModDepth, *slModRate, *slLowCut, *slHighCut;
    DarkSlider*   slDensity, *slWetLevel, *slDryLevel;
    QLabel*       lblRoomSize, *lblERLevel, *lblLateLevel, *lblHfDamp, *lblLfDamp;
    QLabel*       lblRevWidth, *lblModDepth, *lblModRate, *lblLowCut, *lblHighCut;
    QLabel*       lblDensity, *lblWetLevel, *lblDryLevel;

    void buildUI();
    void connectAll();
    void emitSettings();

    // Helper: add a row {label, slider, valueLabel} to a layout
    static void addRow(QLayout* lay, const char* name,
                       DarkSlider* sl, QLabel* vl);
};
