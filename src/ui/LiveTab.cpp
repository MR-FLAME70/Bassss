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

// EAX Preset definitions (from popup.js) — maps name → {decay, mix, predelay, diffuse, tone, roomSize, hfDamp, lfDamp, modDepth, modRate, lowCut}
struct EaxPreset {
    const char* name;
    float decay, mix, predelay, diffuse, toneHz, roomSize;
    float hfDamp, lfDamp, modDepth, modRate, lowCut;
};
static const EaxPreset EAX_PRESETS[] = {
  {"hauntedcavernv2",9.0f,74,38,78,3600,2.6f,35,15,55,30,90},
  {"cathedral",     6.5f,65,30,70,5000,2.2f,25,10,40,20,80},
  {"concerthall",   4.0f,55,20,65,6500,1.8f,20, 8,30,15,70},
  {"sewer",         3.2f,60,15,82,2800,1.5f,40,20,45,35,100},
  {"cave",          5.5f,62,25,75,4200,2.0f,30,12,50,25,85},
  {"drumsroom",     1.8f,45,10,60,7000,1.2f,15, 5,20,10,60},
  {"studioroom",    1.2f,35, 5,55,8000,0.9f,12, 4,15, 8,50},
  {"warehouse",     4.5f,58,22,68,4500,1.9f,28,10,35,18,75},
  {"outdoors",      2.0f,40,12,50,9000,1.3f,18, 6,25,12,55},
  {"submarine",     3.8f,52,18,72,3200,1.6f,35,15,42,28,95},
  {"flat",          1.0f,20, 0,30,12000,0.5f,5, 2, 5, 3,40},
  {nullptr,0,0,0,0,0,0,0,0,0,0,0}
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
    lblFreqVal  = makeLabel("150 Hz", 11, false, "#8b5cf6");
    lay->addWidget(sliderFreq);
    lay->addWidget(lblFreqVal);

    lay->addWidget(makeDimLabel("Gain (dB)"));
    sliderGain  = new DarkSlider(Qt::Horizontal);
    sliderGain->setRangeF(-12, 24, 0.5);
    lblGainVal  = makeLabel("12 dB", 11, false, "#8b5cf6");
    lay->addWidget(sliderGain);
    lay->addWidget(lblGainVal);

    lay->addSpacing(8);
    lay->addWidget(makeLabel("Volume", 12, true));
    sliderVolume = new DarkSlider(Qt::Horizontal);
    sliderVolume->setRangeF(0, 200, 1);
    lblVolVal    = makeLabel("100 %", 11, false, "#8b5cf6");
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
            background: #1a1a1a; color: #fff; border: 1px solid #333;
            border-radius: 6px; padding: 4px 8px; font-size: 12px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView {
            background: #1a1a1a; color: #fff; border: 1px solid #333;
            selection-background-color: #8b5cf6;
        }
    )");
    for (int i = 0; EAX_PRESETS[i].name; ++i)
        comboPreset->addItem(EAX_PRESETS[i].name);
    lay->addWidget(comboPreset);

    // Mix
    lay->addWidget(makeDimLabel("Mix (%)"));
    sliderReverbMix = new DarkSlider(Qt::Horizontal);
    sliderReverbMix->setRangeF(0, 100, 1);
    lblReverbMixVal = makeLabel("74 %", 11, false, "#8b5cf6");
    lay->addWidget(sliderReverbMix);
    lay->addWidget(lblReverbMixVal);

    // Decay
    lay->addWidget(makeDimLabel("Decay (s)"));
    sliderDecay = new DarkSlider(Qt::Horizontal);
    sliderDecay->setRangeF(0.1f, 25.f, 0.1f);
    lblDecayVal = makeLabel("9.0 s", 11, false, "#8b5cf6");
    lay->addWidget(sliderDecay);
    lay->addWidget(lblDecayVal);

    // Pre-delay
    lay->addWidget(makeDimLabel("Pre-delay (ms)"));
    sliderPredelay = new DarkSlider(Qt::Horizontal);
    sliderPredelay->setRangeF(0, 200, 1);
    lblPredelayVal = makeLabel("38 ms", 11, false, "#8b5cf6");
    lay->addWidget(sliderPredelay);
    lay->addWidget(lblPredelayVal);

    // Diffusion
    lay->addWidget(makeDimLabel("Diffusion (%)"));
    sliderDiffuse = new DarkSlider(Qt::Horizontal);
    sliderDiffuse->setRangeF(0, 100, 1);
    lblDiffuseVal = makeLabel("78 %", 11, false, "#8b5cf6");
    lay->addWidget(sliderDiffuse);
    lay->addWidget(lblDiffuseVal);

    // Tone Hz
    lay->addWidget(makeDimLabel("Tone (Hz)"));
    sliderTone = new DarkSlider(Qt::Horizontal);
    sliderTone->setRangeF(200, 20000, 50);
    lblToneVal = makeLabel("3600 Hz", 11, false, "#8b5cf6");
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
            background: #1a1a1a; color: #ef4444; border: 1px solid #333;
            border-radius: 6px; padding: 6px 12px; font-size: 12px;
        }
        QPushButton:hover { background: #222; }
        QPushButton:checked { background: #450a0a; border-color: #ef4444; }
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
            const auto& pr = EAX_PRESETS[i];
            m_settings.reverbPreset         = name;
            m_settings.reverbDecay          = pr.decay;
            m_settings.reverbMix            = pr.mix;
            m_settings.reverbPredelay       = pr.predelay;
            m_settings.reverbDiffuse        = pr.diffuse;
            m_settings.reverbToneHz         = pr.toneHz;
            m_settings.reverbRoomSize       = pr.roomSize;
            m_settings.reverbHfDamping      = pr.hfDamp;
            m_settings.reverbLfDamping      = pr.lfDamp;
            m_settings.reverbModulationDepth= pr.modDepth;
            m_settings.reverbModulationRate = pr.modRate;
            m_settings.reverbLowCut         = pr.lowCut;
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
