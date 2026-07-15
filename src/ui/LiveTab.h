#pragma once
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <vector>
#include "CustomWidgets.h"
#include "../settings.h"

class AudioProcessor;
class QLayout;

// ──────────────────────────────────────────────────────────────────────────────
// LiveTab — the first tab in the main window.
// Matches the Live Tab Audio grid from popup.html:
//   Left column:   Bass Boost controls + Volume
//   Center column: Reverb — one cohesive, professional-plugin-style section:
//                    Reverb Enable → Effect Amount → Preset → Mix →
//                    collapsible "Advanced Reverb Engine" (every other
//                    reverb parameter, hidden until expanded).
//   Right column:  Output controls (record, VU meter, spectrum, bypass)
// ──────────────────────────────────────────────────────────────────────────────
class LiveTab : public QWidget {
    Q_OBJECT
public:
    explicit LiveTab(AudioProcessor* proc, QWidget* parent = nullptr);
    void refreshFromSettings(const AppSettings& s);
    void startAudioDevice(int inputIdx, int outputIdx, double sr, int bufSize);
    void stopAudioDevice();

signals:
    void settingsChanged(const AppSettings& s);

private slots:
    void onMeterTick();
    void onSpectrumTick();
    void onRecordClicked();
    void onPresetChanged(const QString& name);
    void onBypassToggled(bool on);

private:
    AudioProcessor* m_proc;

    // Bass column
    DarkSlider*  sliderFreq;
    DarkSlider*  sliderGain;
    DarkSlider*  sliderVolume;
    ToggleSwitch* toggleBass;
    QLabel*      lblFreqVal;
    QLabel*      lblGainVal;
    QLabel*      lblVolVal;

    // ── Reverb column — always-visible controls ─────────────────────────────
    ToggleSwitch* toggleReverb;
    DarkSlider*   sliderReverbAmount;   QLabel* lblReverbAmountVal;
    QComboBox*    comboPreset;
    DarkSlider*   sliderReverbMix;      QLabel* lblReverbMixVal;

    // ── Advanced Reverb Engine — collapsible, hidden until expanded ─────────
    CollapsibleSection* sectionAdvancedReverb;
    ToggleSwitch*        toggleReverbEngine; // == sectionAdvancedReverb->toggle()

    DarkSlider* sliderDecay;     QLabel* lblDecayVal;
    DarkSlider* sliderPredelay;  QLabel* lblPredelayVal;
    DarkSlider* sliderDiffuse;   QLabel* lblDiffuseVal;
    DarkSlider* sliderTone;      QLabel* lblToneVal;   // shared Tone / High Cut control

    DarkSlider* slRoomSize;   QLabel* lblRoomSize;
    DarkSlider* slDensity;    QLabel* lblDensity;
    DarkSlider* slModDepth;   QLabel* lblModDepth;
    DarkSlider* slModRate;    QLabel* lblModRate;
    DarkSlider* slERDelay;    QLabel* lblERDelay;
    DarkSlider* slERLevel;    QLabel* lblERLevel;
    DarkSlider* slLateLevel;  QLabel* lblLateLevel;
    DarkSlider* slHfDamp;     QLabel* lblHfDamp;
    DarkSlider* slLfDamp;     QLabel* lblLfDamp;
    DarkSlider* slLowCut;     QLabel* lblLowCut;
    DarkSlider* slRevWidth;   QLabel* lblRevWidth;
    DarkSlider* slWetLevel;   QLabel* lblWetLevel;
    DarkSlider* slDryLevel;   QLabel* lblDryLevel;

    // Output column
    VUMeter*      vuMeter;
    SpectrumWidget* spectrumW;
    ToggleSwitch* toggleSpectrum;
    ToggleSwitch* toggleBypass;
    QPushButton*  btnRecord;
    QLabel*       lblRecordTime;
    QLabel*       lblStatus;

    QTimer* meterTimer;
    QTimer* spectrumTimer;
    bool    m_recording = false;
    int     m_recordSecs = 0;

    AppSettings m_settings;

    void buildUI();
    void connectSignals();
    void updateLabels();
    void emitSettings();
    QWidget* buildBassColumn();
    QWidget* buildReverbColumn();
    QWidget* buildOutputColumn();

    // Helper: add a {label, slider, valueLabel} row with consistent
    // alignment/height, matching AdvancedTab's row layout.
    static void addRow(QLayout* lay, const char* name, DarkSlider* sl, QLabel* vl);
};
