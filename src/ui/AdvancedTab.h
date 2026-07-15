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
//   3. Advanced Reverb Engine: every reverb parameter beyond the Live tab's
//      Enable/Effect Amount/Preset/Mix — collapsible, same as the copy that
//      lives on the Live tab's Reverb card (see LiveTab). Both copies are
//      bound to the same AppSettings fields, so dragging either one updates
//      the other the moment MainWindow::onSettingsChanged refreshes every
//      tab — the two are always in sync, never a source of drift.
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

    // Each effect lives in a CollapsibleSection: title + Enable/Disable switch
    // always visible; body (sliders/dropdowns/labels) hidden while disabled,
    // shown with an animated expand when enabled. toggleXxx below now just
    // aliases sectionXxx->toggle() so the rest of the wiring code is unchanged.
    CollapsibleSection* sectionAcoustic;
    CollapsibleSection* sectionSpeaker;
    CollapsibleSection* sectionAdvancedReverb;

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

    // Advanced Reverb Engine (mirrors the collapsible section on LiveTab's
    // Reverb card — same AppSettings fields, kept in sync via MainWindow's
    // broadcast refreshFromSettings on every settingsChanged).
    ToggleSwitch* toggleReverbEngine;
    DarkSlider*   sliderDecay;    QLabel* lblDecayVal;
    DarkSlider*   sliderPredelay; QLabel* lblPredelayVal;
    DarkSlider*   sliderDiffuse;  QLabel* lblDiffuseVal;
    DarkSlider*   sliderTone;     QLabel* lblToneVal;
    DarkSlider*   slRoomSize;   QLabel* lblRoomSize;
    DarkSlider*   slDensity;    QLabel* lblDensity;
    DarkSlider*   slModDepth;   QLabel* lblModDepth;
    DarkSlider*   slModRate;    QLabel* lblModRate;
    DarkSlider*   slERDelay;    QLabel* lblERDelay;
    DarkSlider*   slERLevel;    QLabel* lblERLevel;
    DarkSlider*   slLateLevel;  QLabel* lblLateLevel;
    DarkSlider*   slHfDamp;     QLabel* lblHfDamp;
    DarkSlider*   slLfDamp;     QLabel* lblLfDamp;
    DarkSlider*   slLowCut;     QLabel* lblLowCut;
    DarkSlider*   slRevWidth;   QLabel* lblRevWidth;
    DarkSlider*   slWetLevel;   QLabel* lblWetLevel;
    DarkSlider*   slDryLevel;   QLabel* lblDryLevel;

    void buildUI();
    void connectAll();
    void emitSettings();

    // Helper: add a {label, slider, valueLabel} row to a VBoxLayout
    static void addRow(QLayout* lay, const char* name,
                       DarkSlider* sl, QLabel* vl);
};
