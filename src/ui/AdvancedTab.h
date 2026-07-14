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
//   3. Advanced Reverb Engine: Room Size, ER Delay, Early Reflections,
//      Late Reverb, HF/LF Damping, Stereo Width, Modulation depth/rate,
//      Low Cut, High Cut, Density, Wet Level, Dry Level
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

    // Advanced Reverb Engine — all 14 controls
    ToggleSwitch* toggleReverbEngine;
    // FDN / room
    DarkSlider*   slRoomSize;   QLabel* lblRoomSize;
    DarkSlider*   slDensity;    QLabel* lblDensity;
    DarkSlider*   slModDepth;   QLabel* lblModDepth;
    DarkSlider*   slModRate;    QLabel* lblModRate;
    // Early reflections
    DarkSlider*   slERDelay;    QLabel* lblERDelay;   // ← was missing
    DarkSlider*   slERLevel;    QLabel* lblERLevel;
    // Late tail
    DarkSlider*   slLateLevel;  QLabel* lblLateLevel;
    // Spectral shaping
    DarkSlider*   slHfDamp;     QLabel* lblHfDamp;
    DarkSlider*   slLfDamp;     QLabel* lblLfDamp;
    DarkSlider*   slHighCut;    QLabel* lblHighCut;
    DarkSlider*   slLowCut;     QLabel* lblLowCut;
    // Stereo
    DarkSlider*   slRevWidth;   QLabel* lblRevWidth;
    // Mix
    DarkSlider*   slWetLevel;   QLabel* lblWetLevel;
    DarkSlider*   slDryLevel;   QLabel* lblDryLevel;

    void buildUI();
    void connectAll();
    void emitSettings();

    // Helper: add a {label, slider, valueLabel} row to a VBoxLayout
    static void addRow(QLayout* lay, const char* name,
                       DarkSlider* sl, QLabel* vl);
};
