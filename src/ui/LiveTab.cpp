#include "LiveTab.h"
#include "../audio/AudioProcessor.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractItemView>
#include <cmath>

// EAX Preset definitions — transcribed field-for-field from the original
// extension's EAX_PRESETS table (popup.js), after applying its fallback-fill
// loop (presets that only specify {decay, mix} inherit predelay/diffuse/tone/
// roomSize/etc. from REVERB_ENGINE_DEFAULTS and the decay-derived formulas).
// Field order matches ReverbEngine::Params / AppSettings exactly.
struct EaxPreset {
    const char* name;
    float decay, mix, predelay, diffuse, toneHz, resonanceHz, resonanceQ;
    float roomSize, earlyReflectionDelay, earlyReflectionLevel, lateReverbLevel;
    float density, hfDamp, lfDamp, stereoWidth, modDepth, modRate, lowCut;
    float wetLevel, dryLevel;
};
// Field order: name, decay, mix, predelay, diffuse, toneHz, resonanceHz,
//              resonanceQ, roomSize, earlyReflectionDelay,
//              earlyReflectionLevel, lateReverbLevel, density,
//              hfDamp, lfDamp, stereoWidth, modDepth, modRate,
//              lowCut, wetLevel, dryLevel
//
// Order mirrors the Chrome extension's popup.html <select> dropdown:
// Haunted Cavern variants first (the star presets), then performance venues,
// then the standard EAX room presets (hidden "extra" group in the extension).
static const EaxPreset EAX_PRESETS[] = {
  // ── Haunted Cavern family ─────────────────────────────────────────────────
  {"hauntedcavernv1",       5.6f,  68.0f, 22.0f, 88.0f, 2600.0f, 750.0f,  5.0f, 1.0f,  0.0f,  35.0f, 100.0f,  88.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"hauntedcavernv2",       9.0f,  74.0f, 38.0f, 78.0f, 3600.0f, 1000.0f, 0.0f, 2.6f,  0.0f,  38.0f, 100.0f,  78.0f, 35.0f, 15.0f, 165.0f, 55.0f, 30.0f,  90.0f, 100.0f, 0.0f},
  {"hauntedcavernv3",       5.5f,  15.0f, 28.0f, 90.0f, 3600.0f, 1000.0f, 0.0f, 2.7f,  0.0f, 280.0f, 667.0f,  90.0f, 30.0f, 12.0f, 150.0f, 50.0f, 25.0f,  85.0f, 100.0f, 0.0f},
  // Literal I3DL2/EAX registry port (decay 17.01 s, diffuse 10 %, slap-style
  // predelay capped at 45 ms, no HF resonance ring — see popup.js comment).
  {"hauntedcavernregistry", 17.01f, 50.0f, 45.0f, 10.0f, 5750.0f, 1000.0f, 0.0f, 3.0f, 0.0f, 100.0f, 100.0f,  40.0f,  5.0f,  0.0f, 140.0f, 60.0f, 20.0f,  80.0f, 100.0f, 0.0f},
  // ── Performance venues ────────────────────────────────────────────────────
  {"concerthall",           3.92f, 45.0f, 20.0f, 90.0f, 8000.0f, 1000.0f, 0.0f, 1.7f,  0.0f,  24.0f, 100.0f,  90.0f, 18.0f,  0.0f, 130.0f, 45.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"concerthallv2",         5.6f,  58.0f, 32.0f, 88.0f, 6200.0f, 1000.0f, 0.0f, 2.15f, 4.0f,  30.0f, 100.0f,  92.0f, 32.0f,  8.0f, 155.0f, 40.0f, 28.0f,  90.0f, 100.0f, 0.0f},
  {"amphitheater",          4.6f,  42.0f, 37.0f, 63.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  63.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"orchestrapit",          1.8f,  34.0f, 14.0f, 46.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  46.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"stonehall",             3.3f,  50.0f, 26.0f, 55.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  55.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"jazzclub",              1.1f,  28.0f,  9.0f, 42.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  42.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"recitalhall",           2.6f,  38.0f, 21.0f, 51.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  51.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"theater",               1.7f,  33.0f, 14.0f, 45.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  45.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"operahall",             3.6f,  45.0f, 29.0f, 57.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  57.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"royalhall",             4.2f,  48.0f, 34.0f, 60.0f, 9000.0f, 1000.0f, 0.0f, 1.0f,  0.0f,  35.0f, 100.0f,  60.0f,  0.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  // ── Standard EAX / I3DL2 room presets ────────────────────────────────────
  {"generic",               1.49f, 35.0f,  7.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 0.8f,  0.0f,   5.0f, 100.0f,  90.0f, 10.0f,  0.0f, 100.0f, 35.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"paddedcell",            0.17f, 20.0f,  1.0f, 90.0f, 1500.0f, 1000.0f, 0.0f, 0.35f, 0.0f,  25.0f, 100.0f,  90.0f, 54.0f, 15.0f,  80.0f, 15.0f, 20.0f,  80.0f, 100.0f, 0.0f},
  {"room",                  0.4f,  25.0f,  2.0f, 90.0f, 8200.0f, 1000.0f, 0.0f, 0.4f,  0.0f,  15.0f, 100.0f,  90.0f, 10.0f,  0.0f,  90.0f, 20.0f, 25.0f,  80.0f, 100.0f, 0.0f},
  {"bathroom",              1.49f, 55.0f,  7.0f, 90.0f, 6000.0f, 1000.0f, 0.0f, 0.8f,  0.0f,  65.0f, 100.0f,  60.0f, 28.0f,  0.0f,  85.0f, 30.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"livingroom",            0.5f,  20.0f,  3.0f, 90.0f, 1500.0f, 1000.0f, 0.0f, 0.44f, 0.0f,  21.0f, 100.0f,  90.0f, 54.0f, 10.0f,  85.0f, 20.0f, 20.0f,  80.0f, 100.0f, 0.0f},
  {"stoneroom",             2.31f, 40.0f, 12.0f, 90.0f, 8600.0f, 1000.0f, 0.0f, 1.1f,  0.0f,  44.0f, 100.0f,  90.0f, 22.0f,  0.0f, 105.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"auditorium",            4.32f, 40.0f, 20.0f, 90.0f, 8100.0f, 1000.0f, 0.0f, 1.85f, 0.0f,  40.0f, 100.0f,  90.0f, 25.0f,  0.0f, 120.0f, 45.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"cave",                  2.91f, 45.0f, 15.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 1.35f, 0.0f,  50.0f, 100.0f,  90.0f,  0.0f,  0.0f, 110.0f, 45.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"arena",                 7.24f, 45.0f, 20.0f, 90.0f, 7200.0f, 1000.0f, 0.0f, 2.9f,  0.0f,  26.0f, 100.0f,  90.0f, 40.0f,  0.0f, 145.0f, 50.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"hangar",               10.05f, 45.0f, 20.0f, 90.0f, 6400.0f, 1000.0f, 0.0f, 3.0f,  0.0f,  50.0f, 100.0f,  90.0f, 46.0f, 10.0f, 160.0f, 55.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"carpetedhallway",       0.3f,  15.0f,  2.0f, 90.0f, 2500.0f, 1000.0f, 0.0f, 0.36f, 0.0f,  12.0f, 100.0f,  90.0f, 54.0f, 15.0f,  80.0f, 15.0f, 20.0f,  80.0f, 100.0f, 0.0f},
  {"hallway",               1.49f, 25.0f,  7.0f, 90.0f, 8600.0f, 1000.0f, 0.0f, 0.8f,  0.0f,  25.0f, 100.0f,  90.0f, 25.0f,  0.0f,  95.0f, 35.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"stonecorridor",         2.7f,  30.0f, 13.0f, 90.0f, 8800.0f, 1000.0f, 0.0f, 1.25f, 0.0f,  25.0f, 100.0f,  90.0f, 13.0f,  0.0f, 100.0f, 40.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"alley",                 1.49f, 25.0f,  7.0f, 90.0f, 8700.0f, 1000.0f, 0.0f, 0.8f,  0.0f,  25.0f, 100.0f,  90.0f,  8.0f,  0.0f, 105.0f, 35.0f, 35.0f,  80.0f, 100.0f, 0.0f},
  {"forest",                1.49f, 20.0f, 45.0f, 79.0f, 3000.0f, 1000.0f, 0.0f, 0.8f,  0.0f,   5.0f,  80.0f,  79.0f, 28.0f,  0.0f, 130.0f, 50.0f, 20.0f,  60.0f, 100.0f, 0.0f},
  {"city",                  1.49f, 15.0f,  7.0f, 50.0f, 6900.0f, 1000.0f, 0.0f, 0.8f,  0.0f,   7.0f,  70.0f,  50.0f, 20.0f,  0.0f, 120.0f, 35.0f, 25.0f,  75.0f, 100.0f, 0.0f},
  {"mountains",             1.49f, 12.0f, 45.0f, 27.0f, 4000.0f, 1000.0f, 0.0f, 0.8f,  0.0f,   4.0f,  60.0f,  27.0f, 47.0f,  0.0f, 160.0f, 55.0f, 15.0f,  50.0f, 100.0f, 0.0f},
  {"quarry",                1.49f, 30.0f, 45.0f, 90.0f, 6400.0f, 1000.0f, 0.0f, 0.8f,  0.0f,   0.0f, 100.0f,  90.0f, 10.0f,  0.0f, 120.0f, 40.0f, 30.0f,  70.0f, 100.0f, 0.0f},
  {"plain",                 1.49f, 10.0f, 45.0f, 21.0f, 4800.0f, 1000.0f, 0.0f, 0.8f,  0.0f,   6.0f,  55.0f,  21.0f, 30.0f,  0.0f, 150.0f, 50.0f, 15.0f,  50.0f, 100.0f, 0.0f},
  {"parkinglot",            1.65f, 20.0f,  8.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 0.86f, 0.0f,  21.0f,  85.0f,  90.0f,  0.0f,  0.0f, 115.0f, 35.0f, 30.0f,  80.0f, 100.0f, 0.0f},
  {"sewerpipe",             2.81f, 60.0f, 14.0f, 80.0f, 6400.0f, 1000.0f, 0.0f, 1.29f, 0.0f, 100.0f, 100.0f,  60.0f, 51.0f,  5.0f,  75.0f, 30.0f, 40.0f, 100.0f, 100.0f, 0.0f},
  {"underwater",            1.49f, 65.0f,  7.0f, 90.0f, 2500.0f, 1000.0f, 0.0f, 0.8f,  0.0f,  60.0f, 100.0f,  90.0f, 54.0f, 20.0f,  85.0f, 60.0f, 20.0f, 100.0f, 100.0f, 0.0f},
  {"drugged",               8.39f, 50.0f,  2.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 3.0f,  0.0f,  88.0f, 100.0f,  90.0f,  0.0f,  0.0f, 130.0f, 75.0f, 15.0f,  80.0f, 100.0f, 0.0f},
  {"dizzy",                17.23f, 50.0f, 20.0f, 90.0f, 8400.0f, 1000.0f, 0.0f, 3.0f,  0.0f,  14.0f, 100.0f,  90.0f, 26.0f,  0.0f, 145.0f, 70.0f, 20.0f,  80.0f, 100.0f, 0.0f},
  {"psychotic",             7.56f, 55.0f, 20.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 3.0f,  0.0f,  49.0f, 100.0f,  90.0f,  5.0f,  0.0f, 140.0f, 65.0f, 40.0f,  80.0f, 100.0f, 0.0f},
  {nullptr,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

LiveTab::LiveTab(AudioProcessor* proc, QWidget* parent)
    : QWidget(parent), m_proc(proc) {
    m_settings = globalSettings();
    buildUI();
    connectSignals();
    refreshFromSettings(m_settings);

    meterTimer = new QTimer(this);
    connect(meterTimer, &QTimer::timeout, this, &LiveTab::onMeterTick);
    meterTimer->start(33); // ~30fps

    spectrumTimer = new QTimer(this);
    connect(spectrumTimer, &QTimer::timeout, this, &LiveTab::onSpectrumTick);
    spectrumTimer->start(50);
}

void LiveTab::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 10, 12, 10);

    // ── Top row: 3 equal columns ──────────────────────────────────────────────
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(12);
    topRow->addWidget(buildBassColumn(), 1);
    topRow->addWidget(buildReverbColumn(), 1);
    topRow->addWidget(buildOutputColumn(), 1);
    mainLayout->addLayout(topRow, 3);

    // ── Bottom: Basic Echo section (full-width) ───────────────────────────────
    mainLayout->addWidget(buildEchoSection(), 2);
}

QWidget* LiveTab::buildBassColumn() {
    auto* col = new DarkCard();
    auto* lay = new QVBoxLayout(col);
    lay->setSpacing(10);

    // Header
    auto* header = new QHBoxLayout();
    auto* title = makeLabel("Bass Boost", 13, true, "#ffffff");
    toggleBass = new ToggleSwitch();
    header->addWidget(title);
    header->addStretch();
    header->addWidget(toggleBass);
    lay->addLayout(header);

    lay->addWidget(makeDimLabel("Frequency (Hz)"));
    sliderFreq  = new DarkSlider(Qt::Horizontal);
    sliderFreq->setRangeF(20, 500, 1);
    lblFreqVal  = makeLabel("150 Hz", 11, false, "#d0d0d0");
    lay->addWidget(sliderFreq);
    lay->addWidget(lblFreqVal);

    lay->addWidget(makeDimLabel("Gain (dB)"));
    sliderGain  = new DarkSlider(Qt::Horizontal);
    sliderGain->setRangeF(-12, 24, 0.5);
    lblGainVal  = makeLabel("12 dB", 11, false, "#d0d0d0");
    lay->addWidget(sliderGain);
    lay->addWidget(lblGainVal);

    lay->addSpacing(8);
    lay->addWidget(makeLabel("Speaker Volume", 12, true));
    sliderVolume = new DarkSlider(Qt::Horizontal);
    sliderVolume->setRangeF(0, 200, 1);
    lblVolVal    = makeLabel("100 %", 11, false, "#d0d0d0");
    lay->addWidget(sliderVolume);
    lay->addWidget(lblVolVal);

    lay->addSpacing(4);
    lay->addWidget(makeLabel("Mic Volume", 12, true));
    sliderMicVolume = new DarkSlider(Qt::Horizontal);
    sliderMicVolume->setRangeF(0, 200, 1);
    lblMicVolVal    = makeLabel("100 %", 11, false, "#d0d0d0");
    lay->addWidget(sliderMicVolume);
    lay->addWidget(lblMicVolVal);

    lay->addStretch();
    return col;
}

QWidget* LiveTab::buildReverbColumn() {
    auto* col = new DarkCard();
    auto* lay = new QVBoxLayout(col);
    lay->setSpacing(12);
    lay->setContentsMargins(16, 16, 16, 16);

    auto makeRow = [&](double lo, double hi, double step,
                       DarkSlider*& sl, QLabel*& lbl, const QString& initial) {
        sl  = new DarkSlider(Qt::Horizontal);
        sl->setRangeF(lo, hi, step);
        sl->setFixedHeight(22);
        lbl = new QLabel(initial);
    };

    auto* header = new QHBoxLayout();
    auto* title  = makeLabel("Main", 14, true, "#ffffff");
    toggleReverb = new ToggleSwitch();
    header->addWidget(title);
    header->addStretch();
    header->addWidget(toggleReverb);
    lay->addLayout(header);

    lay->addWidget(makeDimLabel("Effect Amount"));
    makeRow(0, 100, 1, sliderReverbAmount, lblReverbAmountVal, "100 %");
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        lblReverbAmountVal->setStyleSheet("color:#d0d0d0; font-size:11px; font-weight:600;");
        lblReverbAmountVal->setFixedWidth(48);
        lblReverbAmountVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(sliderReverbAmount, 1);
        row->addWidget(lblReverbAmountVal);
        lay->addLayout(row);
    }

    lay->addWidget(makeDimLabel("Preset"));
    comboPreset = new QComboBox();
    comboPreset->setMinimumHeight(36);
    comboPreset->setStyleSheet(R"(
        QComboBox {
            background: #171717; color: #f2f2f2; border: 1px solid #2a2a2a;
            border-radius: 8px; padding: 0 14px; font-size: 13px;
            min-height: 36px;
        }
        QComboBox:hover  { background: #1c1c1c; border: 1px solid #3a3a3a; }
        QComboBox:focus  { border: 1px solid #4a4a4a; }
        QComboBox::drop-down {
            subcontrol-origin: padding; subcontrol-position: top right;
            width: 30px; border-left: 1px solid #2a2a2a;
        }
        QComboBox QAbstractItemView {
            background: #1a1a1a; color: #f2f2f2; border: 1px solid #333;
            border-radius: 6px; padding: 4px;
            selection-background-color: #2e2e2e; selection-color: #ffffff;
            outline: none; font-size: 13px;
        }
        QComboBox QAbstractItemView::item {
            min-height: 28px; padding: 2px 10px; border-radius: 4px;
        }
    )");
    for (int i = 0; EAX_PRESETS[i].name; ++i)
        comboPreset->addItem(EAX_PRESETS[i].name);
    comboPreset->view()->setMinimumWidth(comboPreset->minimumSizeHint().width() + 60);
    comboPreset->setMaxVisibleItems(14);
    lay->addWidget(comboPreset);

    lay->addWidget(makeDimLabel("Mix (%)"));
    makeRow(0, 100, 1, sliderReverbMix, lblReverbMixVal, "74 %");
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(10);
        lblReverbMixVal->setStyleSheet("color:#d0d0d0; font-size:11px; font-weight:600;");
        lblReverbMixVal->setFixedWidth(48);
        lblReverbMixVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(sliderReverbMix, 1);
        row->addWidget(lblReverbMixVal);
        lay->addLayout(row);
    }

    lay->addStretch();
    return col;
}

QWidget* LiveTab::buildOutputColumn() {
    auto* col = new DarkCard();
    auto* lay = new QVBoxLayout(col);
    lay->setSpacing(10);

    lay->addWidget(makeLabel("Output", 13, true));

    lay->addWidget(makeDimLabel("Level"));
    vuMeter = new VUMeter();
    lay->addWidget(vuMeter);

    auto* specHeader = new QHBoxLayout();
    specHeader->addWidget(makeDimLabel("Spectrum"));
    specHeader->addStretch();
    toggleSpectrum = new ToggleSwitch();
    specHeader->addWidget(toggleSpectrum);
    lay->addLayout(specHeader);
    spectrumW = new SpectrumWidget();
    spectrumW->setVisible(false);
    lay->addWidget(spectrumW);

    auto* bypassRow = new QHBoxLayout();
    bypassRow->addWidget(makeDimLabel("A/B Bypass"));
    bypassRow->addStretch();
    toggleBypass = new ToggleSwitch();
    bypassRow->addWidget(toggleBypass);
    lay->addLayout(bypassRow);

    btnRecord     = new QPushButton("● Record");
    lblRecordTime = makeLabel("00:00", 11, false, "#888888");
    btnRecord->setStyleSheet(R"(
        QPushButton {
            background: #171717; color: #ef4444; border: 1px solid #2a2a2a;
            border-radius: 6px; padding: 7px 14px; font-size: 12px; font-weight: 500;
        }
        QPushButton:hover   { background: #232323; border-color: #3a3a3a; }
        QPushButton:pressed { background: #0d0d0d; }
        QPushButton:checked { background: #2a0e0e; border-color: #ef4444; }
    )");
    btnRecord->setCheckable(true);
    lay->addWidget(btnRecord);
    lay->addWidget(lblRecordTime);

    lblStatus = makeLabel("Ready", 11, false, "#22c55e");
    lay->addWidget(lblStatus);

    lay->addStretch();
    return col;
}

// ──────────────────────────────────────────────────────────────────────────────
// buildEchoSection — "Basic Echo" full-width panel.
//
// All 8 controls live here and are wired directly to the EchoEngine DSP in
// connectSignals() / refreshFromSettings(). The panel uses a single DarkCard
// with a two-row grid of (label, slider, value-label) triplets so the layout
// reads like a mixer strip rather than a vertical list.
//
// Control → AppSettings field → EchoEngine::Params field mapping:
//   Delay Time    → echoDelayMs     → p.delayMs      (1–2000 ms)
//   Feedback      → echoFeedback    → p.feedback      (0–95 %, hard-clamped)
//   Num Echoes    → echoNumEchoes   → p.numEchoes     (0 = ∞, 1–10 discrete)
//   Echo Amount   → echoAmount      → p.echoAmount    (0–100 % input drive)
//   Wet Level     → echoWetLevel    → p.wetLevel      (0–100 %)
//   Dry Level     → echoDryLevel    → p.dryLevel      (0–100 %)
//   Wet/Dry Mix   → echoMix         → p.mix           (0–100 % crossfade)
//   Output Gain   → echoOutputGain  → p.outputGain    (0–200 %, 100 = unity)
// ──────────────────────────────────────────────────────────────────────────────
QWidget* LiveTab::buildEchoSection() {
    auto* card = new DarkCard();
    auto* lay  = new QVBoxLayout(card);
    lay->setSpacing(6);
    lay->setContentsMargins(14, 10, 14, 10);

    // ── Section header ────────────────────────────────────────────────────────
    auto* hdr = new QHBoxLayout();
    hdr->addWidget(makeLabel("Basic Echo", 13, true, "#ffffff"));
    hdr->addSpacing(10);
    hdr->addWidget(makeDimLabel("Echo Enable"));
    toggleEchoEnable = new ToggleSwitch();
    hdr->addWidget(toggleEchoEnable);
    hdr->addStretch();
    // Hint to the user that advanced controls exist on the Advanced Audio tab.
    auto* hint = makeDimLabel("Advanced controls → Advanced Audio tab");
    hint->setStyleSheet("color:#444; font-size:10px;");
    hdr->addWidget(hint);
    lay->addLayout(hdr);

    // Thin separator
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color:#222;");
    lay->addWidget(sep);

    // ── 2-row × 4-column control grid ────────────────────────────────────────
    // Each cell is: [param label] / [slider] / [value label] stacked vertically.
    // The grid layout gives each column equal stretch so sliders fill the width.
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(4);

    // Helper: create one (label / slider / value) triplet in a grid column.
    // Rows 0/1/2 = label, slider, value-label.
    // Rows 3/4/5 = second row of controls.
    auto addControl = [&](int row, int col,
                          const QString& caption,
                          double lo, double hi, double step,
                          DarkSlider*& sl, QLabel*& val,
                          const QString& initVal) {
        int baseRow = row * 3;                  // 3 Qt rows per logical row
        auto* capLbl = makeDimLabel(caption);
        capLbl->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
        grid->addWidget(capLbl, baseRow,   col);

        sl = new DarkSlider(Qt::Horizontal);
        sl->setRangeF(lo, hi, step);
        sl->setFixedHeight(20);
        grid->addWidget(sl, baseRow+1, col);

        val = makeLabel(initVal, 10, false, "#c0c0c0");
        val->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(val, baseRow+2, col);
    };

    // ── Row 0: Delay Time | Feedback | Num Echoes | Echo Amount ──────────────
    addControl(0, 0, "Delay Time",
               1, 2000, 1,
               sliderEchoDelay, lblEchoDelayVal, "350 ms");

    addControl(0, 1, "Feedback",
               0, 95, 1,
               sliderEchoFeedback, lblEchoFeedbackVal, "55 %");

    addControl(0, 2, "Num Echoes  (0=∞)",
               0, 10, 1,
               sliderEchoNumEchoes, lblEchoNumEchoesVal, "0 (∞)");

    addControl(0, 3, "Echo Amount",
               0, 100, 1,
               sliderEchoAmount, lblEchoAmountVal, "100 %");

    // ── Row 1: Wet Level | Dry Level | Wet/Dry Mix | Output Gain ─────────────
    addControl(1, 0, "Wet Level",
               0, 100, 1,
               sliderEchoWetLevel, lblEchoWetLevelVal, "100 %");

    addControl(1, 1, "Dry Level",
               0, 100, 1,
               sliderEchoDryLevel, lblEchoDryLevelVal, "100 %");

    addControl(1, 2, "Wet/Dry Mix",
               0, 100, 1,
               sliderEchoMix, lblEchoMixVal, "38 %");

    addControl(1, 3, "Output Gain",
               0, 200, 1,
               sliderEchoOutputGain, lblEchoOutputGainVal, "100 %");

    // Equal column stretch — each control gets the same horizontal share.
    for (int c = 0; c < 4; ++c)
        grid->setColumnStretch(c, 1);

    lay->addLayout(grid);
    return card;
}

// ──────────────────────────────────────────────────────────────────────────────
// connectSignals — wire every widget change to AppSettings + DSP.
// ──────────────────────────────────────────────────────────────────────────────
void LiveTab::connectSignals() {
    // ── Bass ──────────────────────────────────────────────────────────────────
    connect(toggleBass, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.bassOn = on; emitSettings();
    });
    connect(sliderFreq, &QSlider::valueChanged, this, [this]{
        m_settings.frequency = (float)sliderFreq->valueF();
        lblFreqVal->setText(QString::number((int)m_settings.frequency) + " Hz");
        emitSettings();
    });
    connect(sliderGain, &QSlider::valueChanged, this, [this]{
        m_settings.gain = (float)sliderGain->valueF();
        lblGainVal->setText(QString::number(m_settings.gain,'f',1) + " dB");
        emitSettings();
    });
    connect(sliderVolume, &QSlider::valueChanged, this, [this]{
        m_settings.volume = (float)sliderVolume->valueF();
        lblVolVal->setText(QString::number((int)m_settings.volume) + " %");
        emitSettings();
    });
    connect(sliderMicVolume, &QSlider::valueChanged, this, [this]{
        m_settings.micVolume = (float)sliderMicVolume->valueF();
        lblMicVolVal->setText(QString::number((int)m_settings.micVolume) + " %");
        emitSettings();
    });

    // ── Main reverb ───────────────────────────────────────────────────────────
    connect(toggleReverb, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.reverbOn = on; emitSettings();
    });
    connect(sliderReverbAmount, &QSlider::valueChanged, this, [this]{
        m_settings.reverbAmount = (float)sliderReverbAmount->valueF();
        lblReverbAmountVal->setText(QString::number((int)m_settings.reverbAmount)+" %");
        emitSettings();
    });
    connect(comboPreset, &QComboBox::currentTextChanged,
            this, &LiveTab::onPresetChanged);
    connect(sliderReverbMix, &QSlider::valueChanged, this, [this]{
        m_settings.reverbMix = (float)sliderReverbMix->valueF();
        lblReverbMixVal->setText(QString::number((int)m_settings.reverbMix)+" %");
        emitSettings();
    });

    // ── Output ────────────────────────────────────────────────────────────────
    connect(toggleSpectrum, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.spectrumOn = on;
        spectrumW->setVisible(on);
        emitSettings();
    });
    connect(toggleBypass, &ToggleSwitch::toggled, this, &LiveTab::onBypassToggled);
    connect(btnRecord,    &QPushButton::clicked,   this, &LiveTab::onRecordClicked);

    // ── Basic Echo — all 8 controls ───────────────────────────────────────────
    // Each lambda writes into m_settings, updates the value label, then calls
    // emitSettings() which calls AudioProcessor::applySettings() — so the DSP
    // sees the new parameter within the next audio callback's consumePending().

    connect(toggleEchoEnable, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.echoOn = on;
        emitSettings();
    });

    // Delay Time (1–2000 ms) → echoDelayMs → EchoEngine::Params::delayMs
    connect(sliderEchoDelay, &QSlider::valueChanged, this, [this]{
        m_settings.echoDelayMs = (float)sliderEchoDelay->valueF();
        lblEchoDelayVal->setText(QString::number((int)m_settings.echoDelayMs) + " ms");
        emitSettings();
    });

    // Feedback (0–95 %) → echoFeedback → p.feedback (hard-clamped to 0.95 in DSP)
    connect(sliderEchoFeedback, &QSlider::valueChanged, this, [this]{
        m_settings.echoFeedback = (float)sliderEchoFeedback->valueF();
        lblEchoFeedbackVal->setText(QString::number((int)m_settings.echoFeedback) + " %");
        emitSettings();
    });

    // Num Echoes (0–10) → echoNumEchoes → p.numEchoes
    // 0 = infinite feedback mode; 1–10 = discrete N-tap mode.
    connect(sliderEchoNumEchoes, &QSlider::valueChanged, this, [this]{
        m_settings.echoNumEchoes = (int)std::round(sliderEchoNumEchoes->valueF());
        if (m_settings.echoNumEchoes == 0)
            lblEchoNumEchoesVal->setText("0 (∞)");
        else
            lblEchoNumEchoesVal->setText(QString::number(m_settings.echoNumEchoes));
        emitSettings();
    });

    // Echo Amount (0–100 %) → echoAmount → p.echoAmount (input drive into delay)
    connect(sliderEchoAmount, &QSlider::valueChanged, this, [this]{
        m_settings.echoAmount = (float)sliderEchoAmount->valueF();
        lblEchoAmountVal->setText(QString::number((int)m_settings.echoAmount) + " %");
        emitSettings();
    });

    // Wet Level (0–100 %) → echoWetLevel → p.wetLevel (effected signal scale)
    connect(sliderEchoWetLevel, &QSlider::valueChanged, this, [this]{
        m_settings.echoWetLevel = (float)sliderEchoWetLevel->valueF();
        lblEchoWetLevelVal->setText(QString::number((int)m_settings.echoWetLevel) + " %");
        emitSettings();
    });

    // Dry Level (0–100 %) → echoDryLevel → p.dryLevel (original signal scale)
    connect(sliderEchoDryLevel, &QSlider::valueChanged, this, [this]{
        m_settings.echoDryLevel = (float)sliderEchoDryLevel->valueF();
        lblEchoDryLevelVal->setText(QString::number((int)m_settings.echoDryLevel) + " %");
        emitSettings();
    });

    // Wet/Dry Mix (0–100 %) → echoMix → p.mix (crossfade: 0=dry, 100=wet)
    connect(sliderEchoMix, &QSlider::valueChanged, this, [this]{
        m_settings.echoMix = (float)sliderEchoMix->valueF();
        lblEchoMixVal->setText(QString::number((int)m_settings.echoMix) + " %");
        emitSettings();
    });

    // Output Gain (0–200 %) → echoOutputGain → p.outputGain (100 = unity)
    connect(sliderEchoOutputGain, &QSlider::valueChanged, this, [this]{
        m_settings.echoOutputGain = (float)sliderEchoOutputGain->valueF();
        lblEchoOutputGainVal->setText(QString::number((int)m_settings.echoOutputGain) + " %");
        emitSettings();
    });
}

void LiveTab::onPresetChanged(const QString& name) {
    for (int i = 0; EAX_PRESETS[i].name; ++i) {
        if (name == EAX_PRESETS[i].name) {
            const auto& pr = EAX_PRESETS[i];
            m_settings.reverbPreset              = name;
            m_settings.reverbDecay               = pr.decay;
            m_settings.reverbMix                 = pr.mix;
            m_settings.reverbPredelay            = pr.predelay;
            m_settings.reverbDiffuse             = pr.diffuse;
            m_settings.reverbToneHz              = pr.toneHz;
            m_settings.reverbResonanceHz         = pr.resonanceHz;
            m_settings.reverbResonanceQ          = pr.resonanceQ;
            m_settings.reverbRoomSize            = pr.roomSize;
            m_settings.reverbEarlyReflectionDelay= pr.earlyReflectionDelay;
            m_settings.reverbEarlyReflectionLevel= pr.earlyReflectionLevel;
            m_settings.reverbLateReverbLevel     = pr.lateReverbLevel;
            m_settings.reverbDensity             = pr.density;
            m_settings.reverbHfDamping           = pr.hfDamp;
            m_settings.reverbLfDamping           = pr.lfDamp;
            m_settings.reverbStereoWidth         = pr.stereoWidth;
            m_settings.reverbModulationDepth     = pr.modDepth;
            m_settings.reverbModulationRate      = pr.modRate;
            m_settings.reverbLowCut              = pr.lowCut;
            m_settings.reverbWetLevel            = pr.wetLevel;
            m_settings.reverbDryLevel            = pr.dryLevel;
            // Enable the Advanced Reverb Engine so the preset's room-size,
            // early-reflection, late-reverb, damping, etc. values actually
            // reach the DSP.  Without this, AudioProcessor bypasses those
            // fields and falls back to neutral defaults, which is why the
            // preset sounds different from the Chrome extension (where the
            // Advanced Engine is always active when a preset is selected).
            m_settings.reverbEngineOn            = true;
            refreshFromSettings(m_settings);
            emitSettings();
            break;
        }
    }
}

void LiveTab::onBypassToggled(bool on) {
    m_settings.bypass = on;
    emitSettings();
}

void LiveTab::onRecordClicked() {
    if (!m_recording) {
        m_recording  = true;
        m_recordSecs = 0;
        btnRecord->setText("■ Stop");
        lblStatus->setText("Recording…");
        lblStatus->setStyleSheet("color: #ef4444;");
        m_proc->startRecording();
        QTimer* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this, t]{
            if (!m_recording) { t->stop(); t->deleteLater(); return; }
            ++m_recordSecs;
            lblRecordTime->setText(
                QString("%1:%2").arg(m_recordSecs/60, 2, 10, QChar('0'))
                                .arg(m_recordSecs%60, 2, 10, QChar('0')));
        });
        t->start(1000);
    } else {
        m_recording = false;
        btnRecord->setText("● Record");
        lblStatus->setText("Ready");
        lblStatus->setStyleSheet("color: #22c55e;");

        QString path = QFileDialog::getSaveFileName(
            this, "Save Recording", QDir::homePath() + "/recording.wav",
            "WAV Files (*.wav);;All Files (*)");
        if (!path.isEmpty()) {
            bool ok = m_proc->stopRecording(path);
            lblStatus->setText(ok ? "Saved." : "Save failed.");
            lblStatus->setStyleSheet(ok ? "color: #22c55e;" : "color: #ef4444;");
        } else {
            m_proc->stopRecording("");
        }
    }
}

void LiveTab::onMeterTick() {
    auto md = m_proc->getMeterData();
    vuMeter->setLevels(md.rms, md.rms, md.peak, md.clipping);
}

void LiveTab::onSpectrumTick() {
    if (!m_settings.spectrumOn) return;
    std::vector<float> bins;
    if (m_proc->getSpectrum(bins)) spectrumW->setBins(bins);
}

// ──────────────────────────────────────────────────────────────────────────────
// refreshFromSettings — called by MainWindow whenever any tab emits
// settingsChanged, so all tabs stay in sync.
// ──────────────────────────────────────────────────────────────────────────────
void LiveTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;

    toggleBass->setChecked(s.bassOn);
    sliderFreq->setValueF(s.frequency);
    sliderGain->setValueF(s.gain);
    sliderVolume->setValueF(s.volume);
    sliderMicVolume->setValueF(s.micVolume);

    toggleReverb->setChecked(s.reverbOn);
    sliderReverbAmount->setValueF(s.reverbAmount);
    int pIdx = comboPreset->findText(s.reverbPreset);
    if (pIdx >= 0) comboPreset->setCurrentIndex(pIdx);
    comboPreset->setToolTip(s.reverbPreset);
    sliderReverbMix->setValueF(s.reverbMix);

    toggleSpectrum->setChecked(s.spectrumOn);
    spectrumW->setVisible(s.spectrumOn);
    toggleBypass->setChecked(s.bypass);

    // ── Basic Echo ────────────────────────────────────────────────────────────
    toggleEchoEnable->setChecked(s.echoOn);
    sliderEchoDelay->setValueF(s.echoDelayMs);
    sliderEchoFeedback->setValueF(s.echoFeedback);
    sliderEchoNumEchoes->setValueF((double)s.echoNumEchoes);
    sliderEchoAmount->setValueF(s.echoAmount);
    sliderEchoWetLevel->setValueF(s.echoWetLevel);
    sliderEchoDryLevel->setValueF(s.echoDryLevel);
    sliderEchoMix->setValueF(s.echoMix);
    sliderEchoOutputGain->setValueF(s.echoOutputGain);

    updateLabels();
    updateEchoLabels();
}

void LiveTab::updateLabels() {
    lblFreqVal->setText(QString::number((int)m_settings.frequency) + " Hz");
    lblGainVal->setText(QString::number(m_settings.gain,'f',1) + " dB");
    lblVolVal->setText(QString::number((int)m_settings.volume) + " %");
    lblMicVolVal->setText(QString::number((int)m_settings.micVolume) + " %");
    lblReverbAmountVal->setText(QString::number((int)m_settings.reverbAmount) + " %");
    lblReverbMixVal->setText(QString::number((int)m_settings.reverbMix) + " %");
}

void LiveTab::updateEchoLabels() {
    lblEchoDelayVal->setText(QString::number((int)m_settings.echoDelayMs) + " ms");
    lblEchoFeedbackVal->setText(QString::number((int)m_settings.echoFeedback) + " %");
    lblEchoNumEchoesVal->setText(m_settings.echoNumEchoes == 0
        ? "0 (∞)"
        : QString::number(m_settings.echoNumEchoes));
    lblEchoAmountVal->setText(QString::number((int)m_settings.echoAmount) + " %");
    lblEchoWetLevelVal->setText(QString::number((int)m_settings.echoWetLevel) + " %");
    lblEchoDryLevelVal->setText(QString::number((int)m_settings.echoDryLevel) + " %");
    lblEchoMixVal->setText(QString::number((int)m_settings.echoMix) + " %");
    lblEchoOutputGainVal->setText(QString::number((int)m_settings.echoOutputGain) + " %");
}

void LiveTab::emitSettings() {
    m_proc->applySettings(m_settings);
    m_settings.save();
    emit settingsChanged(m_settings);
}

void LiveTab::startAudioDevice(int inputIdx, int outputIdx, double sr, int bufSize) {
    (void)inputIdx; (void)outputIdx; (void)sr; (void)bufSize;
    lblStatus->setText("Active");
    lblStatus->setStyleSheet("color: #22c55e;");
    m_proc->setEnabled(true);
    m_proc->applySettings(m_settings);
}

void LiveTab::stopAudioDevice() {
    lblStatus->setText("Stopped");
    lblStatus->setStyleSheet("color: #888;");
    m_proc->setEnabled(false);
}
