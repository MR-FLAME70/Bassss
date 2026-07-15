#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <array>
#include "CustomWidgets.h"
#include "../settings.h"

// ──────────────────────────────────────────────────────────────────────────────
// AdvancedAudioTab — matches the "Advanced Audio" tab from popup.html.
// Contains: 10-band EQ, Dynamic Bass, Compressor, Limiter,
//           Stereo Width, Pitch Shifter, Echo Engine, Advanced Echo Engine.
// ──────────────────────────────────────────────────────────────────────────────
class AdvancedAudioTab : public QWidget {
    Q_OBJECT
public:
    explicit AdvancedAudioTab(QWidget* parent = nullptr);
    void refreshFromSettings(const AppSettings& s);

signals:
    void settingsChanged(const AppSettings& s);

private slots:
    void onEchoPresetChanged(const QString& name);

private:
    AppSettings m_settings;

    // ── EQ ───────────────────────────────────────────────────────────────────
    ToggleSwitch* toggleEq;
    QComboBox*    comboEqPreset;
    std::array<DarkSlider*, 10> eqSliders;
    std::array<QLabel*, 10>    eqLabels;

    // ── Dynamic Bass ─────────────────────────────────────────────────────────
    ToggleSwitch* toggleDynBass;
    DarkSlider*   sliderDynSens;
    DarkSlider*   sliderDynStr;
    QLabel*       lblDynSens;
    QLabel*       lblDynStr;

    // ── Compressor ────────────────────────────────────────────────────────────
    ToggleSwitch* toggleComp;
    DarkSlider*   sliderCompThr, *sliderCompRatio, *sliderCompAtk,
                 *sliderCompRel, *sliderCompMakeup;
    QLabel*       lblCompThr, *lblCompRatio, *lblCompAtk,
                 *lblCompRel, *lblCompMakeup;

    // ── Limiter ───────────────────────────────────────────────────────────────
    ToggleSwitch* toggleLim;
    DarkSlider*   sliderLimThr, *sliderLimRel;
    QLabel*       lblLimThr, *lblLimRel;

    // ── Stereo Width ──────────────────────────────────────────────────────────
    ToggleSwitch* toggleWidth;
    DarkSlider*   sliderWidth;
    QLabel*       lblWidth;

    // ── Pitch ─────────────────────────────────────────────────────────────────
    ToggleSwitch* togglePitch;
    DarkSlider*   sliderPitch;
    QLabel*       lblPitch;

    // ── Echo Engine ───────────────────────────────────────────────────────────
    ToggleSwitch* toggleEchoEnable;
    ToggleSwitch* toggleEchoBypass;
    QLabel*       lblEchoStatus;
    QComboBox*    comboEchoPreset;
    DarkSlider*   sliderEchoDelay, *sliderEchoFeedback, *sliderEchoMix,
                 *sliderEchoTone, *sliderEchoPingPong;
    QLabel*       lblEchoDelay, *lblEchoFeedback, *lblEchoMix,
                 *lblEchoTone, *lblEchoPingPong;
    QWidget*      echoBody = nullptr;

    // ── Advanced Echo Engine ──────────────────────────────────────────────────
    CollapsibleSection* aeSection = nullptr;

    // Sub-groups (each is a nested CollapsibleSection)
    CollapsibleSection* aeDelayGroup    = nullptr;
    CollapsibleSection* aeFeedbackGroup = nullptr;
    CollapsibleSection* aeStereoGroup   = nullptr;
    CollapsibleSection* aeToneGroup     = nullptr;
    CollapsibleSection* aeSatGroup      = nullptr;
    CollapsibleSection* aeDynGroup      = nullptr;
    CollapsibleSection* aeMixGroup      = nullptr;
    CollapsibleSection* aeModGroup      = nullptr;
    CollapsibleSection* aeSpatialGroup  = nullptr;

    // [Delay]
    DarkSlider* sliderAeDelayTime    = nullptr;  QLabel* lblAeDelayTime    = nullptr;
    DarkSlider* sliderAeLeftDelay    = nullptr;  QLabel* lblAeLeftDelay    = nullptr;
    DarkSlider* sliderAeRightDelay   = nullptr;  QLabel* lblAeRightDelay   = nullptr;
    DarkSlider* sliderAeStereoOffset = nullptr;  QLabel* lblAeStereoOffset = nullptr;
    DarkSlider* sliderAeStereoWidthD = nullptr;  QLabel* lblAeStereoWidthD = nullptr;
    ToggleSwitch* toggleAeTempoSync     = nullptr;
    ToggleSwitch* toggleAeMillisecMode  = nullptr;

    // [Feedback]  (8 controls → 8 pointer pairs)
    DarkSlider* sliderAeCrossFb       = nullptr;  QLabel* lblAeCrossFb       = nullptr; // Feedback
    DarkSlider* sliderAeFbSat         = nullptr;  QLabel* lblAeFbSat         = nullptr; // Cross Feedback
    DarkSlider* sliderAeFbTone        = nullptr;  QLabel* lblAeFbTone        = nullptr; // Feedback Tone
    DarkSlider* sliderAeFbDamp        = nullptr;  QLabel* lblAeFbDamp        = nullptr; // Feedback Saturation
    DarkSlider* sliderAeFbLowCut      = nullptr;  QLabel* lblAeFbLowCut      = nullptr; // Feedback Damping
    DarkSlider* sliderAeFbHighCut     = nullptr;  QLabel* lblAeFbHighCut     = nullptr; // Feedback Low Cut (Hz)
    DarkSlider* sliderAeFbDiff        = nullptr;  QLabel* lblAeFbDiff        = nullptr; // Feedback High Cut (Hz)
    DarkSlider* sliderAeFbDiffusion   = nullptr;  QLabel* lblAeFbDiffusion   = nullptr; // Feedback Diffusion

    // [Stereo]  (5 sliders + 2 toggles → 5 pointer pairs)
    DarkSlider* sliderAeStereoWidthSt = nullptr;  QLabel* lblAeStereoWidthSt = nullptr; // Stereo Width
    DarkSlider* sliderAeBalance       = nullptr;  QLabel* lblAeBalance       = nullptr; // Balance
    DarkSlider* sliderAeLeftLevel     = nullptr;  QLabel* lblAeLeftLevel     = nullptr; // Left Level
    DarkSlider* sliderAeRightLevel    = nullptr;  QLabel* lblAeRightLevel    = nullptr; // Right Level
    DarkSlider* sliderAeMidSide       = nullptr;  QLabel* lblAeMidSide       = nullptr; // Mid/Side Mix
    ToggleSwitch* toggleAePingPongMode = nullptr;
    ToggleSwitch* toggleAeSwapCh       = nullptr;

    // [Tone]
    DarkSlider* sliderAeToneLowCut  = nullptr;  QLabel* lblAeToneLowCut  = nullptr;
    DarkSlider* sliderAeToneHighCut = nullptr;  QLabel* lblAeToneHighCut = nullptr;
    DarkSlider* sliderAeToneBass    = nullptr;  QLabel* lblAeToneBass    = nullptr;
    DarkSlider* sliderAeToneMid     = nullptr;  QLabel* lblAeToneMid     = nullptr;
    DarkSlider* sliderAeToneTreble  = nullptr;  QLabel* lblAeToneTreble  = nullptr;
    DarkSlider* sliderAeTonePresence= nullptr;  QLabel* lblAeTonePresence= nullptr;
    DarkSlider* sliderAeToneBright  = nullptr;  QLabel* lblAeToneBright  = nullptr;

    // [Saturation]
    DarkSlider* sliderAeTapeSat   = nullptr;  QLabel* lblAeTapeSat   = nullptr;
    DarkSlider* sliderAeAnalogSat = nullptr;  QLabel* lblAeAnalogSat = nullptr;
    DarkSlider* sliderAeDrive     = nullptr;  QLabel* lblAeDrive     = nullptr;
    DarkSlider* sliderAeWarmth    = nullptr;  QLabel* lblAeWarmth    = nullptr;
    ToggleSwitch* toggleAeSoftClip = nullptr;

    // [Dynamics]
    DarkSlider* sliderAeInGain   = nullptr;  QLabel* lblAeInGain   = nullptr;
    DarkSlider* sliderAeOutGain  = nullptr;  QLabel* lblAeOutGain  = nullptr;
    DarkSlider* sliderAeWetGain  = nullptr;  QLabel* lblAeWetGain  = nullptr;
    DarkSlider* sliderAeDryGain  = nullptr;  QLabel* lblAeDryGain  = nullptr;
    ToggleSwitch* toggleAeIntLimiter  = nullptr;
    ToggleSwitch* toggleAeSoftLimiter = nullptr;

    // [Mix]
    DarkSlider* sliderAeWetLvl2 = nullptr;  QLabel* lblAeWetLvl2 = nullptr;
    DarkSlider* sliderAeDryLvl2 = nullptr;  QLabel* lblAeDryLvl2 = nullptr;
    DarkSlider* sliderAeBlend   = nullptr;  QLabel* lblAeBlend   = nullptr;
    DarkSlider* sliderAeMixOvr  = nullptr;  QLabel* lblAeMixOvr  = nullptr;

    // [Modulation]
    DarkSlider* sliderAeWow       = nullptr;  QLabel* lblAeWow       = nullptr;
    DarkSlider* sliderAeFlutter   = nullptr;  QLabel* lblAeFlutter   = nullptr;
    DarkSlider* sliderAeModDepth  = nullptr;  QLabel* lblAeModDepth  = nullptr;
    DarkSlider* sliderAeModRate   = nullptr;  QLabel* lblAeModRate   = nullptr;
    DarkSlider* sliderAeRandDrift = nullptr;  QLabel* lblAeRandDrift = nullptr;

    // [Spatial]
    DarkSlider* sliderAeHaasW      = nullptr;  QLabel* lblAeHaasW      = nullptr;
    DarkSlider* sliderAeStSpread   = nullptr;  QLabel* lblAeStSpread   = nullptr;
    DarkSlider* sliderAeEarlyRefl  = nullptr;  QLabel* lblAeEarlyRefl  = nullptr;
    DarkSlider* sliderAeReflLvl    = nullptr;  QLabel* lblAeReflLvl    = nullptr;
    DarkSlider* sliderAeReflDelay  = nullptr;  QLabel* lblAeReflDelay  = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void buildUI();
    void connectAll();
    void emitSettings();
    void updateEchoStatus();

    void buildAdvancedEchoSection(QVBoxLayout* outerLay);
    void connectAdvancedEcho();
    void refreshAdvancedEchoFromSettings(const AppSettings& s);
};
