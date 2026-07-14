#include "AdvancedAudioTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QComboBox>
#include <cmath>

static const char* EQ_FREQS[10] = {"31","62","125","250","500","1k","2k","4k","8k","16k"};

AdvancedAudioTab::AdvancedAudioTab(QWidget* parent) : QWidget(parent) {
    buildUI();
    connectAll();
}

void AdvancedAudioTab::buildUI() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    auto* container = new QWidget();
    auto* lay = new QVBoxLayout(container);
    lay->setSpacing(14);
    lay->setContentsMargins(12,12,12,12);

    // ── EQ ────────────────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleEq   = new ToggleSwitch();
        comboEqPreset = new QComboBox();
        comboEqPreset->setStyleSheet(
            "QComboBox { background:#1a1a1a; color:#fff; border:1px solid #333;"
            "border-radius:6px; padding:4px 8px; font-size:11px; }"
            "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; "
            "border:1px solid #333; selection-background-color:#8b5cf6; }");
        for (auto* preset : {"flat","bassboost","vocal","rock","pop","edm","cinema","gaming"})
            comboEqPreset->addItem(preset);
        hdr->addWidget(makeLabel("10-Band EQ", 12, true));
        hdr->addStretch();
        hdr->addWidget(comboEqPreset);
        hdr->addSpacing(8);
        hdr->addWidget(toggleEq);
        cl->addLayout(hdr);

        auto* grid = new QGridLayout();
        grid->setSpacing(6);
        for (int i = 0; i < 10; ++i) {
            auto* s = new DarkSlider(Qt::Vertical);
            s->setRangeF(-12, 12, 0.5);
            s->setFixedHeight(80);
            auto* lbl = makeLabel("0", 10, false, "#8b5cf6");
            lbl->setAlignment(Qt::AlignHCenter);
            auto* freq = makeDimLabel(EQ_FREQS[i]);
            freq->setAlignment(Qt::AlignHCenter);
            eqSliders[i] = s;
            eqLabels[i]  = lbl;
            grid->addWidget(freq, 0, i, Qt::AlignHCenter);
            grid->addWidget(s,    1, i, Qt::AlignHCenter);
            grid->addWidget(lbl,  2, i, Qt::AlignHCenter);
        }
        cl->addLayout(grid);
        lay->addWidget(card);
    }

    // ── Dynamic Bass ─────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleDynBass = new ToggleSwitch();
        hdr->addWidget(makeLabel("Dynamic Bass", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleDynBass);
        cl->addLayout(hdr);

        auto* grid = new QGridLayout();
        grid->setSpacing(8);
        sliderDynSens = new DarkSlider(Qt::Horizontal);
        sliderDynSens->setRangeF(0, 100, 1);
        lblDynSens = makeLabel("50 %", 11, false, "#8b5cf6");
        sliderDynStr  = new DarkSlider(Qt::Horizontal);
        sliderDynStr->setRangeF(0, 100, 1);
        lblDynStr  = makeLabel("50 %", 11, false, "#8b5cf6");
        grid->addWidget(makeDimLabel("Sensitivity"), 0,0);
        grid->addWidget(sliderDynSens,                0,1);
        grid->addWidget(lblDynSens,                   0,2);
        grid->addWidget(makeDimLabel("Strength"),     1,0);
        grid->addWidget(sliderDynStr,                 1,1);
        grid->addWidget(lblDynStr,                    1,2);
        cl->addLayout(grid);
        lay->addWidget(card);
    }

    // ── Compressor ────────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleComp = new ToggleSwitch();
        hdr->addWidget(makeLabel("Compressor", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleComp);
        cl->addLayout(hdr);

        auto makeRow = [&](const char* lbl, DarkSlider*& sl, QLabel*& vallbl,
                           double lo, double hi, double step, const QString& unit) {
            auto* row = new QHBoxLayout();
            row->addWidget(makeDimLabel(lbl), 1);
            sl = new DarkSlider(Qt::Horizontal);
            sl->setRangeF(lo, hi, step);
            vallbl = makeLabel("", 11, false, "#8b5cf6");
            vallbl->setFixedWidth(70);
            row->addWidget(sl, 3);
            row->addWidget(vallbl, 1);
            cl->addLayout(row);
            (void)unit;
        };

        makeRow("Threshold",  sliderCompThr,    lblCompThr,    -60, 0,   0.5, "dB");
        makeRow("Ratio",      sliderCompRatio,   lblCompRatio,    1, 20,  0.1, ":1");
        makeRow("Attack ms",  sliderCompAtk,     lblCompAtk,      0, 200, 1,   "ms");
        makeRow("Release ms", sliderCompRel,     lblCompRel,     10, 1000,10,  "ms");
        makeRow("Makeup dB",  sliderCompMakeup,  lblCompMakeup,   0, 24,  0.5, "dB");
        lay->addWidget(card);
    }

    // ── Limiter ───────────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleLim = new ToggleSwitch();
        hdr->addWidget(makeLabel("Limiter", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleLim);
        cl->addLayout(hdr);

        sliderLimThr = new DarkSlider(Qt::Horizontal);
        sliderLimThr->setRangeF(-30, 0, 0.5);
        lblLimThr = makeLabel("-3 dB", 11, false, "#8b5cf6");
        sliderLimRel = new DarkSlider(Qt::Horizontal);
        sliderLimRel->setRangeF(5, 500, 5);
        lblLimRel = makeLabel("50 ms", 11, false, "#8b5cf6");

        auto add = [&](const char* name, QSlider* sl, QLabel* vl) {
            auto* row = new QHBoxLayout();
            row->addWidget(makeDimLabel(name), 1);
            row->addWidget(sl, 3);
            row->addWidget(vl, 1);
            cl->addLayout(row);
        };
        add("Threshold", sliderLimThr, lblLimThr);
        add("Release ms",sliderLimRel, lblLimRel);
        lay->addWidget(card);
    }

    // ── Stereo Width ──────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleWidth = new ToggleSwitch();
        hdr->addWidget(makeLabel("Stereo Width", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleWidth);
        cl->addLayout(hdr);
        sliderWidth = new DarkSlider(Qt::Horizontal);
        sliderWidth->setRangeF(0, 200, 1);
        lblWidth = makeLabel("100 %", 11, false, "#8b5cf6");
        auto* row = new QHBoxLayout();
        row->addWidget(sliderWidth, 4); row->addWidget(lblWidth, 1);
        cl->addLayout(row);
        lay->addWidget(card);
    }

    // ── Pitch ─────────────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        togglePitch = new ToggleSwitch();
        hdr->addWidget(makeLabel("Pitch Shift", 12, true));
        hdr->addStretch();
        hdr->addWidget(togglePitch);
        cl->addLayout(hdr);
        sliderPitch = new DarkSlider(Qt::Horizontal);
        sliderPitch->setRangeF(-12, 12, 0.5);
        lblPitch = makeLabel("0 st", 11, false, "#8b5cf6");
        auto* row = new QHBoxLayout();
        row->addWidget(sliderPitch, 4); row->addWidget(lblPitch, 1);
        cl->addLayout(row);
        lay->addWidget(card);
    }

    lay->addStretch();
    scroll->setWidget(container);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0,0,0,0);
    outer->addWidget(scroll);
}

void AdvancedAudioTab::connectAll() {
    // EQ preset
    connect(comboEqPreset, &QComboBox::currentTextChanged, this, [this](const QString& n){
        m_settings.applyEqPreset(n);
        for (int i=0;i<10;++i) eqSliders[i]->setValueF(m_settings.eqBands[i]);
        emitSettings();
    });
    connect(toggleEq, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.eqOn=on; emitSettings();
    });
    for (int i=0;i<10;++i) {
        connect(eqSliders[i], &QSlider::valueChanged, this, [this,i]{
            m_settings.eqBands[i] = (float)eqSliders[i]->valueF();
            eqLabels[i]->setText(QString::number(m_settings.eqBands[i],'f',1));
            emitSettings();
        });
    }

    // Dyn Bass
    connect(toggleDynBass, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.dynBassOn=on; emitSettings();
    });
    connect(sliderDynSens, &QSlider::valueChanged, this, [this]{
        m_settings.dynBassSensitivity=(float)sliderDynSens->valueF();
        lblDynSens->setText(QString::number((int)m_settings.dynBassSensitivity)+" %");
        emitSettings();
    });
    connect(sliderDynStr, &QSlider::valueChanged, this, [this]{
        m_settings.dynBassStrength=(float)sliderDynStr->valueF();
        lblDynStr->setText(QString::number((int)m_settings.dynBassStrength)+" %");
        emitSettings();
    });

    // Compressor
    connect(toggleComp, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.compOn=on; emitSettings();
    });
    connect(sliderCompThr,   &QSlider::valueChanged, this, [this]{
        m_settings.compThreshold=(float)sliderCompThr->valueF();
        lblCompThr->setText(QString::number(m_settings.compThreshold,'f',1)+" dB");
        emitSettings();
    });
    connect(sliderCompRatio, &QSlider::valueChanged, this, [this]{
        m_settings.compRatio=(float)sliderCompRatio->valueF();
        lblCompRatio->setText(QString::number(m_settings.compRatio,'f',1)+":1");
        emitSettings();
    });
    connect(sliderCompAtk,   &QSlider::valueChanged, this, [this]{
        m_settings.compAttack=(float)sliderCompAtk->valueF();
        lblCompAtk->setText(QString::number((int)m_settings.compAttack)+" ms");
        emitSettings();
    });
    connect(sliderCompRel,   &QSlider::valueChanged, this, [this]{
        m_settings.compRelease=(float)sliderCompRel->valueF();
        lblCompRel->setText(QString::number((int)m_settings.compRelease)+" ms");
        emitSettings();
    });
    connect(sliderCompMakeup,&QSlider::valueChanged, this, [this]{
        m_settings.compMakeup=(float)sliderCompMakeup->valueF();
        lblCompMakeup->setText(QString::number(m_settings.compMakeup,'f',1)+" dB");
        emitSettings();
    });

    // Limiter
    connect(toggleLim, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.limOn=on; emitSettings();
    });
    connect(sliderLimThr, &QSlider::valueChanged, this, [this]{
        m_settings.limThreshold=(float)sliderLimThr->valueF();
        lblLimThr->setText(QString::number(m_settings.limThreshold,'f',1)+" dB");
        emitSettings();
    });
    connect(sliderLimRel, &QSlider::valueChanged, this, [this]{
        m_settings.limRelease=(float)sliderLimRel->valueF();
        lblLimRel->setText(QString::number((int)m_settings.limRelease)+" ms");
        emitSettings();
    });

    // Stereo Width
    connect(toggleWidth, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.stereoWidthOn=on; emitSettings();
    });
    connect(sliderWidth, &QSlider::valueChanged, this, [this]{
        m_settings.stereoWidth=(float)sliderWidth->valueF();
        lblWidth->setText(QString::number((int)m_settings.stereoWidth)+" %");
        emitSettings();
    });

    // Pitch
    connect(togglePitch, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.pitchOn=on; emitSettings();
    });
    connect(sliderPitch, &QSlider::valueChanged, this, [this]{
        m_settings.pitch=(float)sliderPitch->valueF();
        lblPitch->setText(QString::number(m_settings.pitch,'f',1)+" st");
        emitSettings();
    });
}

void AdvancedAudioTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;

    toggleEq->setChecked(s.eqOn);
    int pIdx = comboEqPreset->findText(s.eqPreset);
    if (pIdx>=0) comboEqPreset->setCurrentIndex(pIdx);
    for (int i=0;i<10;++i) {
        eqSliders[i]->setValueF(s.eqBands[i]);
        eqLabels[i]->setText(QString::number(s.eqBands[i],'f',1));
    }

    toggleDynBass->setChecked(s.dynBassOn);
    sliderDynSens->setValueF(s.dynBassSensitivity);
    sliderDynStr->setValueF(s.dynBassStrength);
    lblDynSens->setText(QString::number((int)s.dynBassSensitivity)+" %");
    lblDynStr->setText(QString::number((int)s.dynBassStrength)+" %");

    toggleComp->setChecked(s.compOn);
    sliderCompThr->setValueF(s.compThreshold);
    sliderCompRatio->setValueF(s.compRatio);
    sliderCompAtk->setValueF(s.compAttack);
    sliderCompRel->setValueF(s.compRelease);
    sliderCompMakeup->setValueF(s.compMakeup);
    lblCompThr->setText(QString::number(s.compThreshold,'f',1)+" dB");
    lblCompRatio->setText(QString::number(s.compRatio,'f',1)+":1");
    lblCompAtk->setText(QString::number((int)s.compAttack)+" ms");
    lblCompRel->setText(QString::number((int)s.compRelease)+" ms");
    lblCompMakeup->setText(QString::number(s.compMakeup,'f',1)+" dB");

    toggleLim->setChecked(s.limOn);
    sliderLimThr->setValueF(s.limThreshold);
    sliderLimRel->setValueF(s.limRelease);
    lblLimThr->setText(QString::number(s.limThreshold,'f',1)+" dB");
    lblLimRel->setText(QString::number((int)s.limRelease)+" ms");

    toggleWidth->setChecked(s.stereoWidthOn);
    sliderWidth->setValueF(s.stereoWidth);
    lblWidth->setText(QString::number((int)s.stereoWidth)+" %");

    togglePitch->setChecked(s.pitchOn);
    sliderPitch->setValueF(s.pitch);
    lblPitch->setText(QString::number(s.pitch,'f',1)+" st");
}

void AdvancedAudioTab::emitSettings() {
    m_settings.save();
    emit settingsChanged(m_settings);
}
