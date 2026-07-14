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
//   Left column:  Bass Boost controls + Volume
//   Center column: Reverb on/off + presets + advanced reverb settings
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

    // Reverb column
    ToggleSwitch* toggleReverb;
    QComboBox*    comboPreset;
    DarkSlider*   sliderReverbMix;
    DarkSlider*   sliderDecay;
    DarkSlider*   sliderPredelay;
    DarkSlider*   sliderDiffuse;
    DarkSlider*   sliderTone;
    QLabel*       lblReverbMixVal;
    QLabel*       lblDecayVal;
    QLabel*       lblPredelayVal;
    QLabel*       lblDiffuseVal;
    QLabel*       lblToneVal;

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
