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
  {"generic", 1.49f, 35f, 7f, 90f, 9000f, 1000f, 0f, 0.8f, 0f, 5f, 100f, 90f, 10f, 0f, 100f, 35f, 35f, 80f, 100f, 0f},
  {"paddedcell", 0.17f, 20f, 1f, 90f, 1500f, 1000f, 0f, 0.35f, 0f, 25f, 100f, 90f, 54f, 15f, 80f, 15f, 20f, 80f, 100f, 0f},
  {"room", 0.4f, 25f, 2f, 90f, 8200f, 1000f, 0f, 0.4f, 0f, 15f, 100f, 90f, 10f, 0f, 90f, 20f, 25f, 80f, 100f, 0f},
  {"bathroom", 1.49f, 55f, 7f, 90f, 6000f, 1000f, 0f, 0.8f, 0f, 65f, 100f, 60f, 28f, 0f, 85f, 30f, 30f, 80f, 100f, 0f},
  {"livingroom", 0.5f, 20f, 3f, 90f, 1500f, 1000f, 0f, 0.44f, 0f, 21f, 100f, 90f, 54f, 10f, 85f, 20f, 20f, 80f, 100f, 0f},
  {"stoneroom", 2.31f, 40f, 12f, 90f, 8600f, 1000f, 0f, 1.1f, 0f, 44f, 100f, 90f, 22f, 0f, 105f, 40f, 35f, 80f, 100f, 0f},
  {"auditorium", 4.32f, 40f, 20f, 90f, 8100f, 1000f, 0f, 1.85f, 0f, 40f, 100f, 90f, 25f, 0f, 120f, 45f, 30f, 80f, 100f, 0f},
  {"concerthall", 3.92f, 45f, 20f, 90f, 8000f, 1000f, 0f, 1.7f, 0f, 24f, 100f, 90f, 18f, 0f, 130f, 45f, 30f, 80f, 100f, 0f},
  {"concerthallv2", 5.6f, 58f, 32f, 88f, 6200f, 1000f, 0f, 2.15f, 4f, 30f, 100f, 92f, 32f, 8f, 155f, 40f, 28f, 90f, 100f, 0f},
  {"cave", 2.91f, 45f, 15f, 90f, 9000f, 1000f, 0f, 1.35f, 0f, 50f, 100f, 90f, 0f, 0f, 110f, 45f, 30f, 80f, 100f, 0f},
  {"arena", 7.24f, 45f, 20f, 90f, 7200f, 1000f, 0f, 2.9f, 0f, 26f, 100f, 90f, 40f, 0f, 145f, 50f, 30f, 80f, 100f, 0f},
  {"hangar", 10.05f, 45f, 20f, 90f, 6400f, 1000f, 0f, 3f, 0f, 50f, 100f, 90f, 46f, 10f, 160f, 55f, 30f, 80f, 100f, 0f},
  {"carpetedhallway", 0.3f, 15f, 2f, 90f, 2500f, 1000f, 0f, 0.36f, 0f, 12f, 100f, 90f, 54f, 15f, 80f, 15f, 20f, 80f, 100f, 0f},
  {"hallway", 1.49f, 25f, 7f, 90f, 8600f, 1000f, 0f, 0.8f, 0f, 25f, 100f, 90f, 25f, 0f, 95f, 35f, 35f, 80f, 100f, 0f},
  {"stonecorridor", 2.7f, 30f, 13f, 90f, 8800f, 1000f, 0f, 1.25f, 0f, 25f, 100f, 90f, 13f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"alley", 1.49f, 25f, 7f, 90f, 8700f, 1000f, 0f, 0.8f, 0f, 25f, 100f, 90f, 8f, 0f, 105f, 35f, 35f, 80f, 100f, 0f},
  {"forest", 1.49f, 20f, 45f, 79f, 3000f, 1000f, 0f, 0.8f, 0f, 5f, 80f, 79f, 28f, 0f, 130f, 50f, 20f, 60f, 100f, 0f},
  {"city", 1.49f, 15f, 7f, 50f, 6900f, 1000f, 0f, 0.8f, 0f, 7f, 70f, 50f, 20f, 0f, 120f, 35f, 25f, 75f, 100f, 0f},
  {"mountains", 1.49f, 12f, 45f, 27f, 4000f, 1000f, 0f, 0.8f, 0f, 4f, 60f, 27f, 47f, 0f, 160f, 55f, 15f, 50f, 100f, 0f},
  {"quarry", 1.49f, 30f, 45f, 90f, 6400f, 1000f, 0f, 0.8f, 0f, 0f, 100f, 90f, 10f, 0f, 120f, 40f, 30f, 70f, 100f, 0f},
  {"plain", 1.49f, 10f, 45f, 21f, 4800f, 1000f, 0f, 0.8f, 0f, 6f, 55f, 21f, 30f, 0f, 150f, 50f, 15f, 50f, 100f, 0f},
  {"parkinglot", 1.65f, 20f, 8f, 90f, 9000f, 1000f, 0f, 0.86f, 0f, 21f, 85f, 90f, 0f, 0f, 115f, 35f, 30f, 80f, 100f, 0f},
  {"sewerpipe", 2.81f, 60f, 14f, 80f, 6400f, 1000f, 0f, 1.29f, 0f, 100f, 100f, 60f, 51f, 5f, 75f, 30f, 40f, 100f, 100f, 0f},
  {"underwater", 1.49f, 65f, 7f, 90f, 2500f, 1000f, 0f, 0.8f, 0f, 60f, 100f, 90f, 54f, 20f, 85f, 60f, 20f, 100f, 100f, 0f},
  {"drugged", 8.39f, 50f, 2f, 90f, 9000f, 1000f, 0f, 3f, 0f, 88f, 100f, 90f, 0f, 0f, 130f, 75f, 15f, 80f, 100f, 0f},
  {"dizzy", 17.23f, 50f, 20f, 90f, 8400f, 1000f, 0f, 3f, 0f, 14f, 100f, 90f, 26f, 0f, 145f, 70f, 20f, 80f, 100f, 0f},
  {"psychotic", 7.56f, 55f, 20f, 90f, 9000f, 1000f, 0f, 3f, 0f, 49f, 100f, 90f, 5f, 0f, 140f, 65f, 40f, 80f, 100f, 0f},
  {"hauntedcavernv1", 5.6f, 68f, 22f, 88f, 2600f, 750f, 5f, 1f, 0f, 35f, 100f, 88f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"hauntedcavernv2", 9f, 74f, 38f, 78f, 3600f, 1000f, 0f, 2.6f, 0f, 38f, 100f, 78f, 35f, 15f, 165f, 55f, 30f, 90f, 100f, 0f},
  {"amphitheater", 4.6f, 42f, 37f, 63f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 63f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"orchestrapit", 1.8f, 34f, 14f, 46f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 46f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"stonehall", 3.3f, 50f, 26f, 55f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 55f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"jazzclub", 1.1f, 28f, 9f, 42f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 42f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"recitalhall", 2.6f, 38f, 21f, 51f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 51f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"theater", 1.7f, 33f, 14f, 45f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 45f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"operahall", 3.6f, 45f, 29f, 57f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 57f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"royalhall", 4.2f, 48f, 34f, 60f, 9000f, 1000f, 0f, 1f, 0f, 35f, 100f, 60f, 0f, 0f, 100f, 40f, 35f, 80f, 100f, 0f},
  {"hauntedcavernv3", 5.5f, 15f, 28f, 90f, 3600f, 1000f, 0f, 2.7f, 0f, 280f, 667f, 90f, 30f, 12f, 150f, 50f, 25f, 85f, 100f, 0f},
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
    lay->setSpacing(8);

    // Header
    auto* header = new QHBoxLayout();
    auto* title  = makeLabel("Reverb", 13, true, "#ffffff");
    toggleReverb = new ToggleSwitch();
    header->addWidget(title);
    header->addStretch();
    header->addWidget(toggleReverb);
    lay->addLayout(header);

    // Preset
    lay->addWidget(makeDimLabel("Preset"));
    comboPreset = new QComboBox();
    comboPreset->setStyleSheet(R"(
        QComboBox {
            background: #171717; color: #f2f2f2; border: 1px solid #2a2a2a;
            border-radius: 6px; padding: 5px 9px; font-size: 12px;
        }
        QComboBox:hover { border: 1px solid #3a3a3a; }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background: #1a1a1a; color: #fff; border: 1px solid #333;
            selection-background-color: #333333; outline: none;
        }
    )");
    for (int i = 0; EAX_PRESETS[i].name; ++i)
        comboPreset->addItem(EAX_PRESETS[i].name);
    lay->addWidget(comboPreset);

    // Mix — popup.html: reverbMix min=0 max=1000 step=1 (wide "overdrive"
    // headroom past 100%; ReverbEngine/outer bus clamp to 0-100 internally).
    lay->addWidget(makeDimLabel("Mix (%)"));
    sliderReverbMix = new DarkSlider(Qt::Horizontal);
    sliderReverbMix->setRangeF(0, 1000, 1);
    lblReverbMixVal = makeLabel("74 %", 11, false, "#d0d0d0");
    lay->addWidget(sliderReverbMix);
    lay->addWidget(lblReverbMixVal);

    // Decay — popup.html: reverbDecay min=0.1 max=250 step=0.01
    lay->addWidget(makeDimLabel("Decay (s)"));
    sliderDecay = new DarkSlider(Qt::Horizontal);
    sliderDecay->setRangeF(0.1f, 250.f, 0.01f);
    lblDecayVal = makeLabel("9.0 s", 11, false, "#d0d0d0");
    lay->addWidget(sliderDecay);
    lay->addWidget(lblDecayVal);

    // Pre-delay — popup.html: reverbPredelay min=0 max=1500 step=1
    lay->addWidget(makeDimLabel("Pre-delay (ms)"));
    sliderPredelay = new DarkSlider(Qt::Horizontal);
    sliderPredelay->setRangeF(0, 1500, 1);
    lblPredelayVal = makeLabel("38 ms", 11, false, "#d0d0d0");
    lay->addWidget(sliderPredelay);
    lay->addWidget(lblPredelayVal);

    // Diffusion — popup.html: reverbDiffuse min=0 max=900 step=1
    lay->addWidget(makeDimLabel("Diffusion (%)"));
    sliderDiffuse = new DarkSlider(Qt::Horizontal);
    sliderDiffuse->setRangeF(0, 900, 1);
    lblDiffuseVal = makeLabel("78 %", 11, false, "#d0d0d0");
    lay->addWidget(sliderDiffuse);
    lay->addWidget(lblDiffuseVal);

    // Tone Hz — popup.html: reverbFrequency min=500 max=12000 step=50.
    // Same underlying field as the Advanced tab's "High Cut" slider
    // (reverbToneHz) — the two are alternate views of one control.
    lay->addWidget(makeDimLabel("Tone (Hz)"));
    sliderTone = new DarkSlider(Qt::Horizontal);
    sliderTone->setRangeF(500, 12000, 50);
    lblToneVal = makeLabel("3600 Hz", 11, false, "#d0d0d0");
    lay->addWidget(sliderTone);
    lay->addWidget(lblToneVal);

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

    // Reverb
    connect(toggleReverb, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.reverbOn = on; emitSettings();
    });
    connect(comboPreset, &QComboBox::currentTextChanged,
            this, &LiveTab::onPresetChanged);
    connect(sliderReverbMix, &QSlider::valueChanged, this, [this]{
        m_settings.reverbMix = (float)sliderReverbMix->valueF();
        lblReverbMixVal->setText(QString::number((int)m_settings.reverbMix)+" %");
        emitSettings();
    });
    connect(sliderDecay, &QSlider::valueChanged, this, [this]{
        m_settings.reverbDecay = (float)sliderDecay->valueF();
        lblDecayVal->setText(QString::number(m_settings.reverbDecay,'f',1)+" s");
        emitSettings();
    });
    connect(sliderPredelay, &QSlider::valueChanged, this, [this]{
        m_settings.reverbPredelay = (float)sliderPredelay->valueF();
        lblPredelayVal->setText(QString::number((int)m_settings.reverbPredelay)+" ms");
        emitSettings();
    });
    connect(sliderDiffuse, &QSlider::valueChanged, this, [this]{
        m_settings.reverbDiffuse = (float)sliderDiffuse->valueF();
        lblDiffuseVal->setText(QString::number((int)m_settings.reverbDiffuse)+" %");
        emitSettings();
    });
    connect(sliderTone, &QSlider::valueChanged, this, [this]{
        m_settings.reverbToneHz = (float)sliderTone->valueF();
        lblToneVal->setText(QString::number((int)m_settings.reverbToneHz)+" Hz");
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
    int pIdx = comboPreset->findText(s.reverbPreset);
    if (pIdx >= 0) comboPreset->setCurrentIndex(pIdx);
    sliderReverbMix->setValueF(s.reverbMix);
    sliderDecay->setValueF(s.reverbDecay);
    sliderPredelay->setValueF(s.reverbPredelay);
    sliderDiffuse->setValueF(s.reverbDiffuse);
    sliderTone->setValueF(s.reverbToneHz);

    toggleSpectrum->setChecked(s.spectrumOn);
    spectrumW->setVisible(s.spectrumOn);
    toggleBypass->setChecked(s.bypass);

    updateLabels();
}

void LiveTab::updateLabels() {
    lblFreqVal->setText(QString::number((int)m_settings.frequency) + " Hz");
    lblGainVal->setText(QString::number(m_settings.gain,'f',1) + " dB");
    lblVolVal->setText(QString::number((int)m_settings.volume) + " %");
    lblReverbMixVal->setText(QString::number((int)m_settings.reverbMix) + " %");
    lblDecayVal->setText(QString::number(m_settings.reverbDecay,'f',1) + " s");
    lblPredelayVal->setText(QString::number((int)m_settings.reverbPredelay) + " ms");
    lblDiffuseVal->setText(QString::number((int)m_settings.reverbDiffuse) + " %");
    lblToneVal->setText(QString::number((int)m_settings.reverbToneHz) + " Hz");
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
