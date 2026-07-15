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
//
// The "Advanced Reverb Engine" section (Room Size, ER Delay, Early
// Reflections, Late Reverb, HF/LF Damping, Stereo Width, Modulation
// depth/rate, Low Cut, High Cut, Density, Wet Level, Dry Level) now lives
// inside the redesigned Reverb section on the Live tab (see LiveTab), next
// to Reverb Enable / Effect Amount / Preset / Mix, instead of a separate
// tab — the whole Reverb feature is one cohesive, collapsible unit.
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

    void buildUI();
    void connectAll();
    void emitSettings();

    // Helper: add a {label, slider, valueLabel} row to a VBoxLayout
    static void addRow(QLayout* lay, const char* name,
                       DarkSlider* sl, QLabel* vl);
};
