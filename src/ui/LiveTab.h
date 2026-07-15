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
//
// Layout (vertical):
//   Top row (3 equal columns):
//     Left   — Bass Boost controls + Volume
//     Center — "Main": everyday reverb controls (Enable, Amount, Preset, Mix)
//     Right  — Output (VU meter, spectrum, A/B bypass, record)
//   Bottom section (full width):
//     Basic Echo — all 8 controls wired to EchoEngine DSP:
//       Delay Time · Feedback · Num Echoes · Echo Amount
//       Wet Level  · Dry Level · Wet/Dry Mix · Output Gain
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

    // ── Bass column ───────────────────────────────────────────────────────────
    DarkSlider*  sliderFreq;
    DarkSlider*  sliderGain;
    DarkSlider*  sliderVolume;
    ToggleSwitch* toggleBass;
    QLabel*      lblFreqVal;
    QLabel*      lblGainVal;
    QLabel*      lblVolVal;

    // ── Main reverb column ────────────────────────────────────────────────────
    ToggleSwitch* toggleReverb;
    DarkSlider*   sliderReverbAmount;   QLabel* lblReverbAmountVal;
    QComboBox*    comboPreset;
    DarkSlider*   sliderReverbMix;      QLabel* lblReverbMixVal;

    // ── Output column ─────────────────────────────────────────────────────────
    VUMeter*      vuMeter;
    SpectrumWidget* spectrumW;
    ToggleSwitch* toggleSpectrum;
    ToggleSwitch* toggleBypass;
    QPushButton*  btnRecord;
    QLabel*       lblRecordTime;
    QLabel*       lblStatus;

    // ── Basic Echo section — 8 controls, all wired to EchoEngine DSP ─────────
    ToggleSwitch* toggleEchoEnable;
    // Row 1
    DarkSlider*   sliderEchoDelay;      QLabel* lblEchoDelayVal;
    DarkSlider*   sliderEchoFeedback;   QLabel* lblEchoFeedbackVal;
    DarkSlider*   sliderEchoNumEchoes;  QLabel* lblEchoNumEchoesVal;
    DarkSlider*   sliderEchoAmount;     QLabel* lblEchoAmountVal;
    // Row 2
    DarkSlider*   sliderEchoWetLevel;   QLabel* lblEchoWetLevelVal;
    DarkSlider*   sliderEchoDryLevel;   QLabel* lblEchoDryLevelVal;
    DarkSlider*   sliderEchoMix;        QLabel* lblEchoMixVal;
    DarkSlider*   sliderEchoOutputGain; QLabel* lblEchoOutputGainVal;

    // ─────────────────────────────────────────────────────────────────────────
    QTimer* meterTimer;
    QTimer* spectrumTimer;
    bool    m_recording = false;
    int     m_recordSecs = 0;

    AppSettings m_settings;

    void buildUI();
    void connectSignals();
    void updateLabels();
    void updateEchoLabels();
    void emitSettings();
    QWidget* buildBassColumn();
    QWidget* buildReverbColumn();
    QWidget* buildOutputColumn();
    QWidget* buildEchoSection();
};
