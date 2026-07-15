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

// ──────────────────────────────────────────────────────────────────────────────
// LiveTab — the first tab in the main window.
// Matches the Live Tab Audio grid from popup.html:
//   Left column:   Bass Boost controls + Volume
//   Center column: "Main" — the everyday reverb controls:
//                    Reverb Enable → Effect Amount → Preset → Mix.
//                    The deep-dive parameter set lives exclusively in the
//                    "Advanced Reverb Engine" collapsible on the Advanced tab.
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

    // ── Main reverb column — always-visible controls ────────────────────────
    // Renamed from "Reverb" to "Main" now that the deep-dive Advanced Reverb
    // Engine panel lives exclusively on the Advanced tab; this column only
    // holds the everyday controls (Enable, Effect Amount, Preset, Mix).
    ToggleSwitch* toggleReverb;
    DarkSlider*   sliderReverbAmount;   QLabel* lblReverbAmountVal;
    QComboBox*    comboPreset;
    DarkSlider*   sliderReverbMix;      QLabel* lblReverbMixVal;

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
};
