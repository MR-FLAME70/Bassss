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
static const EaxPreset EAX_PRESETS[] = {
  {"generic", 1.49f, 35.0f, 7.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 5.0f, 100.0f, 90.0f, 10.0f, 0.0f, 100.0f, 35.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"paddedcell", 0.17f, 20.0f, 1.0f, 90.0f, 1500.0f, 1000.0f, 0.0f, 0.35f, 0.0f, 25.0f, 100.0f, 90.0f, 54.0f, 15.0f, 80.0f, 15.0f, 20.0f, 80.0f, 100.0f, 0.0f},
  {"room", 0.4f, 25.0f, 2.0f, 90.0f, 8200.0f, 1000.0f, 0.0f, 0.4f, 0.0f, 15.0f, 100.0f, 90.0f, 10.0f, 0.0f, 90.0f, 20.0f, 25.0f, 80.0f, 100.0f, 0.0f},
  {"bathroom", 1.49f, 55.0f, 7.0f, 90.0f, 6000.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 65.0f, 100.0f, 60.0f, 28.0f, 0.0f, 85.0f, 30.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"livingroom", 0.5f, 20.0f, 3.0f, 90.0f, 1500.0f, 1000.0f, 0.0f, 0.44f, 0.0f, 21.0f, 100.0f, 90.0f, 54.0f, 10.0f, 85.0f, 20.0f, 20.0f, 80.0f, 100.0f, 0.0f},
  {"stoneroom", 2.31f, 40.0f, 12.0f, 90.0f, 8600.0f, 1000.0f, 0.0f, 1.1f, 0.0f, 44.0f, 100.0f, 90.0f, 22.0f, 0.0f, 105.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"auditorium", 4.32f, 40.0f, 20.0f, 90.0f, 8100.0f, 1000.0f, 0.0f, 1.85f, 0.0f, 40.0f, 100.0f, 90.0f, 25.0f, 0.0f, 120.0f, 45.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"concerthall", 3.92f, 45.0f, 20.0f, 90.0f, 8000.0f, 1000.0f, 0.0f, 1.7f, 0.0f, 24.0f, 100.0f, 90.0f, 18.0f, 0.0f, 130.0f, 45.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"concerthallv2", 5.6f, 58.0f, 32.0f, 88.0f, 6200.0f, 1000.0f, 0.0f, 2.15f, 4.0f, 30.0f, 100.0f, 92.0f, 32.0f, 8.0f, 155.0f, 40.0f, 28.0f, 90.0f, 100.0f, 0.0f},
  {"cave", 2.91f, 45.0f, 15.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 1.35f, 0.0f, 50.0f, 100.0f, 90.0f, 0.0f, 0.0f, 110.0f, 45.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"arena", 7.24f, 45.0f, 20.0f, 90.0f, 7200.0f, 1000.0f, 0.0f, 2.9f, 0.0f, 26.0f, 100.0f, 90.0f, 40.0f, 0.0f, 145.0f, 50.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"hangar", 10.05f, 45.0f, 20.0f, 90.0f, 6400.0f, 1000.0f, 0.0f, 3.0f, 0.0f, 50.0f, 100.0f, 90.0f, 46.0f, 10.0f, 160.0f, 55.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"carpetedhallway", 0.3f, 15.0f, 2.0f, 90.0f, 2500.0f, 1000.0f, 0.0f, 0.36f, 0.0f, 12.0f, 100.0f, 90.0f, 54.0f, 15.0f, 80.0f, 15.0f, 20.0f, 80.0f, 100.0f, 0.0f},
  {"hallway", 1.49f, 25.0f, 7.0f, 90.0f, 8600.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 25.0f, 100.0f, 90.0f, 25.0f, 0.0f, 95.0f, 35.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"stonecorridor", 2.7f, 30.0f, 13.0f, 90.0f, 8800.0f, 1000.0f, 0.0f, 1.25f, 0.0f, 25.0f, 100.0f, 90.0f, 13.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"alley", 1.49f, 25.0f, 7.0f, 90.0f, 8700.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 25.0f, 100.0f, 90.0f, 8.0f, 0.0f, 105.0f, 35.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"forest", 1.49f, 20.0f, 45.0f, 79.0f, 3000.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 5.0f, 80.0f, 79.0f, 28.0f, 0.0f, 130.0f, 50.0f, 20.0f, 60.0f, 100.0f, 0.0f},
  {"city", 1.49f, 15.0f, 7.0f, 50.0f, 6900.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 7.0f, 70.0f, 50.0f, 20.0f, 0.0f, 120.0f, 35.0f, 25.0f, 75.0f, 100.0f, 0.0f},
  {"mountains", 1.49f, 12.0f, 45.0f, 27.0f, 4000.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 4.0f, 60.0f, 27.0f, 47.0f, 0.0f, 160.0f, 55.0f, 15.0f, 50.0f, 100.0f, 0.0f},
  {"quarry", 1.49f, 30.0f, 45.0f, 90.0f, 6400.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 0.0f, 100.0f, 90.0f, 10.0f, 0.0f, 120.0f, 40.0f, 30.0f, 70.0f, 100.0f, 0.0f},
  {"plain", 1.49f, 10.0f, 45.0f, 21.0f, 4800.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 6.0f, 55.0f, 21.0f, 30.0f, 0.0f, 150.0f, 50.0f, 15.0f, 50.0f, 100.0f, 0.0f},
  {"parkinglot", 1.65f, 20.0f, 8.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 0.86f, 0.0f, 21.0f, 85.0f, 90.0f, 0.0f, 0.0f, 115.0f, 35.0f, 30.0f, 80.0f, 100.0f, 0.0f},
  {"sewerpipe", 2.81f, 60.0f, 14.0f, 80.0f, 6400.0f, 1000.0f, 0.0f, 1.29f, 0.0f, 100.0f, 100.0f, 60.0f, 51.0f, 5.0f, 75.0f, 30.0f, 40.0f, 100.0f, 100.0f, 0.0f},
  {"underwater", 1.49f, 65.0f, 7.0f, 90.0f, 2500.0f, 1000.0f, 0.0f, 0.8f, 0.0f, 60.0f, 100.0f, 90.0f, 54.0f, 20.0f, 85.0f, 60.0f, 20.0f, 100.0f, 100.0f, 0.0f},
  {"drugged", 8.39f, 50.0f, 2.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 3.0f, 0.0f, 88.0f, 100.0f, 90.0f, 0.0f, 0.0f, 130.0f, 75.0f, 15.0f, 80.0f, 100.0f, 0.0f},
  {"dizzy", 17.23f, 50.0f, 20.0f, 90.0f, 8400.0f, 1000.0f, 0.0f, 3.0f, 0.0f, 14.0f, 100.0f, 90.0f, 26.0f, 0.0f, 145.0f, 70.0f, 20.0f, 80.0f, 100.0f, 0.0f},
  {"psychotic", 7.56f, 55.0f, 20.0f, 90.0f, 9000.0f, 1000.0f, 0.0f, 3.0f, 0.0f, 49.0f, 100.0f, 90.0f, 5.0f, 0.0f, 140.0f, 65.0f, 40.0f, 80.0f, 100.0f, 0.0f},
  {"hauntedcavernv1", 5.6f, 68.0f, 22.0f, 88.0f, 2600.0f, 750.0f, 5.0f, 1.0f, 0.0f, 35.0f, 100.0f, 88.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"hauntedcavernv2", 9.0f, 74.0f, 38.0f, 78.0f, 3600.0f, 1000.0f, 0.0f, 2.6f, 0.0f, 38.0f, 100.0f, 78.0f, 35.0f, 15.0f, 165.0f, 55.0f, 30.0f, 90.0f, 100.0f, 0.0f},
  {"amphitheater", 4.6f, 42.0f, 37.0f, 63.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 63.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"orchestrapit", 1.8f, 34.0f, 14.0f, 46.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 46.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"stonehall", 3.3f, 50.0f, 26.0f, 55.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 55.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"jazzclub", 1.1f, 28.0f, 9.0f, 42.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 42.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"recitalhall", 2.6f, 38.0f, 21.0f, 51.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 51.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"theater", 1.7f, 33.0f, 14.0f, 45.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 45.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"operahall", 3.6f, 45.0f, 29.0f, 57.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 57.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"royalhall", 4.2f, 48.0f, 34.0f, 60.0f, 9000.0f, 1000.0f, 0.0f, 1.0f, 0.0f, 35.0f, 100.0f, 60.0f, 0.0f, 0.0f, 100.0f, 40.0f, 35.0f, 80.0f, 100.0f, 0.0f},
  {"hauntedcavernv3", 5.5f, 15.0f, 28.0f, 90.0f, 3600.0f, 1000.0f, 0.0f, 2.7f, 0.0f, 280.0f, 667.0f, 90.0f, 30.0f, 12.0f, 150.0f, 50.0f, 25.0f, 85.0f, 100.0f, 0.0f},
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
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(12,12,12,12);
    mainLayout->addWidget(buildBassColumn(), 1);
    mainLayout->addWidget(buildReverbColumn(), 1);
    mainLayout->addWidget(buildOutputColumn(), 1);
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
    lay->addWidget(makeLabel("Volume", 12, true));
    sliderVolume = new DarkSlider(Qt::Horizontal);
    sliderVolume->setRangeF(0, 200, 1);
    lblVolVal    = makeLabel("100 %", 11, false, "#d0d0d0");
    lay->addWidget(sliderVolume);
    lay->addWidget(lblVolVal);

    lay->addStretch();
    return col;
}

QWidget* LiveTab::buildReverbColumn() {
    auto* col = new DarkCard();
    auto* lay = new QVBoxLayout(col);
    lay->setSpacing(12);
    lay->setContentsMargins(16, 16, 16, 16);

    // Convenience factory, mirrors AdvancedTab's makeRow: new slider + value
    // label wired to the passed-in pointers, with a consistent control height.
    auto makeRow = [&](double lo, double hi, double step,
                       DarkSlider*& sl, QLabel*& lbl, const QString& initial) {
        sl  = new DarkSlider(Qt::Horizontal);
        sl->setRangeF(lo, hi, step);
        sl->setFixedHeight(22);
        lbl = new QLabel(initial);
    };

    // ── 1. Reverb Enable ─────────────────────────────────────────────────────
    // Section renamed "Main" — this column now only carries the everyday
    // controls; the deep-dive parameters live in "Advanced Reverb Engine"
    // on the Advanced tab exclusively.
    auto* header = new QHBoxLayout();
    auto* title  = makeLabel("Main", 14, true, "#ffffff");
    toggleReverb = new ToggleSwitch();
    header->addWidget(title);
    header->addStretch();
    header->addWidget(toggleReverb);
    lay->addLayout(header);

    // ── 2. Effect Amount — master intensity for the whole reverb. 0% = fully
    //    dry, 100% = full preset intensity. Scales the wet bus only; it never
    //    touches the preset's own internal parameters (room size, decay,
    //    etc.), and updates in real time via the audio thread's smoothed gain
    //    ramp (see AudioProcessor::processStereo), so there is no zipper
    //    noise while dragging. Backed by the existing AppSettings::reverbAmount.
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

    // ── 3. Preset — larger, legible, dark-theme dropdown. ───────────────────
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
    // Let the popup grow to fit the longest preset name (e.g.
    // "hauntedcavernv2") in full, even though the closed control stays at
    // the column's width — names are never truncated once the list is open.
    comboPreset->view()->setMinimumWidth(comboPreset->minimumSizeHint().width() + 60);
    comboPreset->setMaxVisibleItems(14);
    lay->addWidget(comboPreset);

    // ── 4. Mix — popup.html: reverbMix min=0 max=1000 step=1 (wide
    //    "overdrive" headroom past 100%; ReverbEngine/outer bus clamp to
    //    0-100 internally).
    lay->addWidget(makeDimLabel("Mix (%)"));
    makeRow(0, 1000, 1, sliderReverbMix, lblReverbMixVal, "74 %");
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

    // VU Meter
    lay->addWidget(makeDimLabel("Level"));
    vuMeter = new VUMeter();
    lay->addWidget(vuMeter);

    // Spectrum
    auto* specHeader = new QHBoxLayout();
    specHeader->addWidget(makeDimLabel("Spectrum"));
    specHeader->addStretch();
    toggleSpectrum = new ToggleSwitch();
    specHeader->addWidget(toggleSpectrum);
    lay->addLayout(specHeader);
    spectrumW = new SpectrumWidget();
    spectrumW->setVisible(false);
    lay->addWidget(spectrumW);

    // Bypass
    auto* bypassRow = new QHBoxLayout();
    bypassRow->addWidget(makeDimLabel("A/B Bypass"));
    bypassRow->addStretch();
    toggleBypass = new ToggleSwitch();
    bypassRow->addWidget(toggleBypass);
    lay->addLayout(bypassRow);

    // Record
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

void LiveTab::connectSignals() {
    // Bass
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

    // ── Main reverb controls ─────────────────────────────────────────────────
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

    // Output
    connect(toggleSpectrum, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.spectrumOn = on;
        spectrumW->setVisible(on);
        emitSettings();
    });
    connect(toggleBypass, &ToggleSwitch::toggled, this, &LiveTab::onBypassToggled);
    connect(btnRecord, &QPushButton::clicked, this, &LiveTab::onRecordClicked);
}

void LiveTab::onPresetChanged(const QString& name) {
    for (int i = 0; EAX_PRESETS[i].name; ++i) {
        if (name == EAX_PRESETS[i].name) {
            // Matches popup.js's reverbPreset change handler: selecting a
            // preset overwrites every one of its fields (including the
            // Advanced Reverb Engine ones, so the Advanced tab reflects the
            // preset even though only a subset has controls on this tab).
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
            // Effect Amount is a separate master-intensity control, not part
            // of the preset itself — selecting a preset must not reset it.
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

void LiveTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;
    // Block signals to avoid feedback loops
    toggleBass->setChecked(s.bassOn);
    sliderFreq->setValueF(s.frequency);
    sliderGain->setValueF(s.gain);
    sliderVolume->setValueF(s.volume);

    toggleReverb->setChecked(s.reverbOn);
    sliderReverbAmount->setValueF(s.reverbAmount);
    int pIdx = comboPreset->findText(s.reverbPreset);
    if (pIdx >= 0) comboPreset->setCurrentIndex(pIdx);
    comboPreset->setToolTip(s.reverbPreset);
    sliderReverbMix->setValueF(s.reverbMix);

    toggleSpectrum->setChecked(s.spectrumOn);
    spectrumW->setVisible(s.spectrumOn);
    toggleBypass->setChecked(s.bypass);

    updateLabels();
}

void LiveTab::updateLabels() {
    lblFreqVal->setText(QString::number((int)m_settings.frequency) + " Hz");
    lblGainVal->setText(QString::number(m_settings.gain,'f',1) + " dB");
    lblVolVal->setText(QString::number((int)m_settings.volume) + " %");

    lblReverbAmountVal->setText(QString::number((int)m_settings.reverbAmount) + " %");
    lblReverbMixVal->setText(QString::number((int)m_settings.reverbMix) + " %");
}

void LiveTab::emitSettings() {
    m_proc->applySettings(m_settings);
    m_settings.save();
    emit settingsChanged(m_settings);
}

void LiveTab::startAudioDevice(int inputIdx, int outputIdx, double sr, int bufSize) {
    (void)inputIdx; (void)outputIdx; (void)sr; (void)bufSize;
    // AudioCapture is managed by MainWindow; this just updates status.
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
