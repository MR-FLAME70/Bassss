#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <array>
#include "CustomWidgets.h"
#include "../settings.h"

// ──────────────────────────────────────────────────────────────────────────────
// AdvancedAudioTab — matches the "Advanced Audio" tab from popup.html.
// Contains: 10-band EQ, Dynamic Bass, Compressor, Limiter,
//           Stereo Width, Pitch Shifter.
// ──────────────────────────────────────────────────────────────────────────────
class AdvancedAudioTab : public QWidget {
    Q_OBJECT
public:
    explicit AdvancedAudioTab(QWidget* parent = nullptr);
    void refreshFromSettings(const AppSettings& s);

signals:
    void settingsChanged(const AppSettings& s);

private:
    AppSettings m_settings;

    // EQ
    ToggleSwitch* toggleEq;
    QComboBox*    comboEqPreset;
    std::array<DarkSlider*, 10> eqSliders;
    std::array<QLabel*, 10>    eqLabels;

    // Dynamic Bass
    ToggleSwitch* toggleDynBass;
    DarkSlider*   sliderDynSens;
    DarkSlider*   sliderDynStr;
    QLabel*       lblDynSens;
    QLabel*       lblDynStr;

    // Compressor
    ToggleSwitch* toggleComp;
    DarkSlider*   sliderCompThr, *sliderCompRatio, *sliderCompAtk, *sliderCompRel, *sliderCompMakeup;
    QLabel*       lblCompThr, *lblCompRatio, *lblCompAtk, *lblCompRel, *lblCompMakeup;

    // Limiter
    ToggleSwitch* toggleLim;
    DarkSlider*   sliderLimThr, *sliderLimRel;
    QLabel*       lblLimThr, *lblLimRel;

    // Stereo Width
    ToggleSwitch* toggleWidth;
    DarkSlider*   sliderWidth;
    QLabel*       lblWidth;

    // Pitch
    ToggleSwitch* togglePitch;
    DarkSlider*   sliderPitch;
    QLabel*       lblPitch;

    void buildUI();
    void connectAll();
    void emitSettings();
};
