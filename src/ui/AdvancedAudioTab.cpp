#include "AdvancedAudioTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QComboBox>
#include <QVariant>
#include <cmath>
#include <utility>

static const char* EQ_FREQS[10] = {"31","62","125","250","500","1k","2k","4k","8k","16k"};

// ── Echo Engine presets ──────────────────────────────────────────────────────
// name → { delayMs, feedback%, mix%, tone%, pingPong% }
struct EchoPreset { const char* name; float delay, fb, mix, tone, pingPong; };
static const EchoPreset ECHO_PRESETS[] = {
    { "slapback",     110.f,  8.f, 25.f, 20.f,   0.f },
    { "vocaldoubler",  35.f,  5.f, 20.f, 10.f,   0.f },
    { "pingpong",     280.f, 42.f, 40.f, 25.f, 100.f },
    { "tapeecho",     350.f, 55.f, 38.f, 60.f,  15.f },
    { "rhythmic8th",  250.f, 48.f, 42.f, 30.f,  60.f },
    { "dubdelay",     430.f, 72.f, 50.f, 70.f,  20.f },
    { "ambientwash",  620.f, 68.f, 45.f, 45.f,  35.f },
    { "stadium",      500.f, 60.f, 55.f, 50.f,  80.f },
};
static const EchoPreset* findEchoPreset(const QString& name) {
    for (auto& p : ECHO_PRESETS) if (name == p.name) return &p;
    return &ECHO_PRESETS[3]; // tapeecho fallback
}

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
            "QComboBox { background:#171717; color:#f2f2f2; border:1px solid #2a2a2a;"
            "border-radius:6px; padding:5px 9px; font-size:11px; }"
            "QComboBox:hover { border:1px solid #3a3a3a; }"
            "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; "
            "border:1px solid #333; selection-background-color:#333333; outline:none; }");
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
            auto* lbl = makeLabel("0", 10, false, "#d0d0d0");
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
        lblDynSens = makeLabel("50 %", 11, false, "#d0d0d0");
        sliderDynStr  = new DarkSlider(Qt::Horizontal);
        sliderDynStr->setRangeF(0, 100, 1);
        lblDynStr  = makeLabel("50 %", 11, false, "#d0d0d0");
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
            vallbl = makeLabel("", 11, false, "#d0d0d0");
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
        lblLimThr = makeLabel("-3 dB", 11, false, "#d0d0d0");
        sliderLimRel = new DarkSlider(Qt::Horizontal);
        sliderLimRel->setRangeF(5, 500, 5);
        lblLimRel = makeLabel("50 ms", 11, false, "#d0d0d0");

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
        lblWidth = makeLabel("100 %", 11, false, "#d0d0d0");
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
        lblPitch = makeLabel("0 st", 11, false, "#d0d0d0");
        auto* row = new QHBoxLayout();
        row->addWidget(sliderPitch, 4); row->addWidget(lblPitch, 1);
        cl->addLayout(row);
        lay->addWidget(card);
    }

    // ── Echo Engine ───────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        cl->setSpacing(10);

        auto* hdr = new QHBoxLayout();
        hdr->addWidget(makeLabel("Echo Engine", 12, true));

        lblEchoStatus = new QLabel("OFF");
        lblEchoStatus->setStyleSheet(
            "QLabel { color:#808080; background:#232323; border-radius:8px;"
            "padding:2px 9px; font-size:9px; font-weight:600; letter-spacing:0.5px; }");
        hdr->addSpacing(8);
        hdr->addWidget(lblEchoStatus);
        hdr->addStretch();

        hdr->addWidget(makeDimLabel("Bypass"));
        toggleEchoBypass = new ToggleSwitch();
        hdr->addWidget(toggleEchoBypass);
        hdr->addSpacing(10);
        toggleEchoEnable = new ToggleSwitch();
        hdr->addWidget(toggleEchoEnable);
        cl->addLayout(hdr);

        comboEchoPreset = new QComboBox();
        comboEchoPreset->setStyleSheet(
            "QComboBox { background:#171717; color:#f2f2f2; border:1px solid #2a2a2a;"
            "border-radius:6px; padding:5px 9px; font-size:11px; }"
            "QComboBox:hover { border:1px solid #3a3a3a; }"
            "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; "
            "border:1px solid #333; selection-background-color:#333333; outline:none; }");
        static const std::pair<const char*, const char*> ECHO_PRESET_LABELS[] = {
            {"slapback","Slapback"}, {"vocaldoubler","Vocal Doubler"},
            {"pingpong","Ping-Pong"}, {"tapeecho","Tape Echo"},
            {"rhythmic8th","Rhythmic 8th"}, {"dubdelay","Dub Delay"},
            {"ambientwash","Ambient Wash"}, {"stadium","Stadium"},
        };
        for (auto& pl : ECHO_PRESET_LABELS)
            comboEchoPreset->addItem(pl.second, QVariant(QString(pl.first)));
        cl->addWidget(comboEchoPreset);

        echoBody = new QWidget();
        auto* grid = new QGridLayout(echoBody);
        grid->setContentsMargins(0,0,0,0);
        grid->setSpacing(8);

        auto makeEchoRow = [&](int row, const char* name, DarkSlider*& sl, QLabel*& vl,
                                double lo, double hi, double step) {
            sl = new DarkSlider(Qt::Horizontal);
            sl->setRangeF(lo, hi, step);
            vl = makeLabel("", 11, false, "#d0d0d0");
            vl->setFixedWidth(64);
            grid->addWidget(makeDimLabel(name), row, 0);
            grid->addWidget(sl,                 row, 1);
            grid->addWidget(vl,                 row, 2);
        };
        makeEchoRow(0, "Delay Time",  sliderEchoDelay,    lblEchoDelay,     10, 2000, 1);
        makeEchoRow(1, "Feedback",    sliderEchoFeedback, lblEchoFeedback,   0,   95, 1);
        makeEchoRow(2, "Tone",        sliderEchoTone,      lblEchoTone,      0,  100, 1);
        makeEchoRow(3, "Mix",         sliderEchoMix,       lblEchoMix,       0,  100, 1);
        makeEchoRow(4, "Ping-Pong",   sliderEchoPingPong,  lblEchoPingPong,  0,  100, 1);
        cl->addWidget(echoBody);

        lay->addWidget(card);
    }

    buildAdvancedEchoSection(lay);

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

    // Echo Engine
    connect(toggleEchoEnable, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.echoOn = on;
        updateEchoStatus();
        emitSettings();
    });
    connect(toggleEchoBypass, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.echoBypass = on;
        updateEchoStatus();
        emitSettings();
    });
    connect(comboEchoPreset, &QComboBox::currentTextChanged, this,
            [this](const QString&){ onEchoPresetChanged(comboEchoPreset->currentData().toString()); });
    connect(sliderEchoDelay, &QSlider::valueChanged, this, [this]{
        m_settings.echoDelayMs=(float)sliderEchoDelay->valueF();
        lblEchoDelay->setText(QString::number((int)m_settings.echoDelayMs)+" ms");
        emitSettings();
    });
    connect(sliderEchoFeedback, &QSlider::valueChanged, this, [this]{
        m_settings.echoFeedback=(float)sliderEchoFeedback->valueF();
        lblEchoFeedback->setText(QString::number((int)m_settings.echoFeedback)+" %");
        emitSettings();
    });
    connect(sliderEchoTone, &QSlider::valueChanged, this, [this]{
        m_settings.echoTone=(float)sliderEchoTone->valueF();
        lblEchoTone->setText(QString::number((int)m_settings.echoTone)+" %");
        emitSettings();
    });
    connect(sliderEchoMix, &QSlider::valueChanged, this, [this]{
        m_settings.echoMix=(float)sliderEchoMix->valueF();
        lblEchoMix->setText(QString::number((int)m_settings.echoMix)+" %");
        emitSettings();
    });
    connect(sliderEchoPingPong, &QSlider::valueChanged, this, [this]{
        m_settings.echoPingPong=(float)sliderEchoPingPong->valueF();
        lblEchoPingPong->setText(QString::number((int)m_settings.echoPingPong)+" %");
        emitSettings();
    });

    connectAdvancedEcho();
}

void AdvancedAudioTab::onEchoPresetChanged(const QString& id) {
    if (id.isEmpty()) return;
    const EchoPreset* p = findEchoPreset(id);
    m_settings.echoPreset     = id;
    m_settings.echoDelayMs    = p->delay;
    m_settings.echoFeedback   = p->fb;
    m_settings.echoMix        = p->mix;
    m_settings.echoTone       = p->tone;
    m_settings.echoPingPong   = p->pingPong;

    sliderEchoDelay->setValueF(p->delay);
    sliderEchoFeedback->setValueF(p->fb);
    sliderEchoTone->setValueF(p->tone);
    sliderEchoMix->setValueF(p->mix);
    sliderEchoPingPong->setValueF(p->pingPong);
    lblEchoDelay->setText(QString::number((int)p->delay)+" ms");
    lblEchoFeedback->setText(QString::number((int)p->fb)+" %");
    lblEchoTone->setText(QString::number((int)p->tone)+" %");
    lblEchoMix->setText(QString::number((int)p->mix)+" %");
    lblEchoPingPong->setText(QString::number((int)p->pingPong)+" %");
    emitSettings();
}

void AdvancedAudioTab::updateEchoStatus() {
    if (!m_settings.echoOn) {
        lblEchoStatus->setText("OFF");
        lblEchoStatus->setStyleSheet(
            "QLabel { color:#808080; background:#232323; border-radius:8px;"
            "padding:2px 9px; font-size:9px; font-weight:600; letter-spacing:0.5px; }");
    } else if (m_settings.echoBypass) {
        lblEchoStatus->setText("BYPASSED");
        lblEchoStatus->setStyleSheet(
            "QLabel { color:#f5a623; background:#332a12; border-radius:8px;"
            "padding:2px 9px; font-size:9px; font-weight:600; letter-spacing:0.5px; }");
    } else {
        lblEchoStatus->setText("ACTIVE");
        lblEchoStatus->setStyleSheet(
            "QLabel { color:#22c55e; background:#123320; border-radius:8px;"
            "padding:2px 9px; font-size:9px; font-weight:600; letter-spacing:0.5px; }");
    }
    if (echoBody) echoBody->setEnabled(m_settings.echoOn && !m_settings.echoBypass);
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

    toggleEchoEnable->setChecked(s.echoOn);
    toggleEchoBypass->setChecked(s.echoBypass);
    int echoIdx = comboEchoPreset->findData(s.echoPreset);
    if (echoIdx >= 0) comboEchoPreset->setCurrentIndex(echoIdx);
    sliderEchoDelay->setValueF(s.echoDelayMs);
    sliderEchoFeedback->setValueF(s.echoFeedback);
    sliderEchoTone->setValueF(s.echoTone);
    sliderEchoMix->setValueF(s.echoMix);
    sliderEchoPingPong->setValueF(s.echoPingPong);
    lblEchoDelay->setText(QString::number((int)s.echoDelayMs)+" ms");
    lblEchoFeedback->setText(QString::number((int)s.echoFeedback)+" %");
    lblEchoTone->setText(QString::number((int)s.echoTone)+" %");
    lblEchoMix->setText(QString::number((int)s.echoMix)+" %");
    lblEchoPingPong->setText(QString::number((int)s.echoPingPong)+" %");
    updateEchoStatus();
    refreshAdvancedEchoFromSettings(s);
}

void AdvancedAudioTab::emitSettings() {
    m_settings.save();
    emit settingsChanged(m_settings);
}

// ════════════════════════════════════════════════════════════════════════════
// Advanced Echo Engine — collapsible section (9 parameter groups)
// ════════════════════════════════════════════════════════════════════════════

void AdvancedAudioTab::buildAdvancedEchoSection(QVBoxLayout* outerLay) {
    aeSection = new CollapsibleSection("Advanced Echo Engine");
    aeSection->headerLayout()->insertWidget(1,
        makeDimLabel("deep signal-chain control"));

    auto* aeLay = new QVBoxLayout(aeSection->content());
    aeLay->setSpacing(5);
    aeLay->setContentsMargins(0, 6, 0, 2);

    // ── Shared helpers ───────────────────────────────────────────────────────
    // addRow: horizontal slider row in a QGridLayout
    auto addRow = [](QGridLayout* gl, int r, const char* n,
                     DarkSlider*& sl, QLabel*& vl,
                     double lo, double hi, double step) {
        sl = new DarkSlider(Qt::Horizontal);
        sl->setRangeF(lo, hi, step);
        vl = makeLabel("", 11, false, "#d0d0d0");
        vl->setFixedWidth(80);
        gl->addWidget(makeDimLabel(n), r, 0);
        gl->addWidget(sl,             r, 1);
        gl->addWidget(vl,             r, 2);
    };
    // addTog: toggle row (full-width in grid)
    auto addTog = [](QGridLayout* gl, int r, const char* n, ToggleSwitch*& ts) {
        ts = new ToggleSwitch();
        auto* w = new QWidget();
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 2, 0, 2);
        h->addWidget(makeDimLabel(n));
        h->addStretch();
        h->addWidget(ts);
        gl->addWidget(w, r, 0, 1, 3);
    };
    // makeGrid: standard grid with spacing for a sub-group body
    auto makeGrid = [](QWidget* parent) -> QGridLayout* {
        auto* gl = new QGridLayout(parent);
        gl->setSpacing(8);
        gl->setContentsMargins(4, 4, 4, 6);
        gl->setColumnStretch(1, 4);
        return gl;
    };

    // ── 1. Delay ─────────────────────────────────────────────────────────────
    {
        aeDelayGroup = new CollapsibleSection("Delay");
        auto* gl = makeGrid(aeDelayGroup->content());
        int r = 0;
        addRow(gl, r++, "Delay Time",    sliderAeDelayTime,    lblAeDelayTime,    1, 2000, 1);
        addRow(gl, r++, "Left Delay",    sliderAeLeftDelay,    lblAeLeftDelay,    1, 2000, 1);
        addRow(gl, r++, "Right Delay",   sliderAeRightDelay,   lblAeRightDelay,   1, 2000, 1);
        addRow(gl, r++, "Stereo Offset", sliderAeStereoOffset, lblAeStereoOffset, -200, 200, 1);
        addRow(gl, r++, "Stereo Width",  sliderAeStereoWidthD, lblAeStereoWidthD, 0, 100, 1);
        addTog(gl, r++, "Tempo Sync",       toggleAeTempoSync);
        addTog(gl, r++, "Millisecond Mode", toggleAeMillisecMode);
        aeLay->addWidget(aeDelayGroup);
    }

    // ── 2. Feedback (8 controls) ──────────────────────────────────────────────
    {
        aeFeedbackGroup = new CollapsibleSection("Feedback");
        auto* gl = makeGrid(aeFeedbackGroup->content());
        int r = 0;
        addRow(gl, r++, "Feedback",            sliderAeCrossFb,     lblAeCrossFb,     0,  95,    1);
        addRow(gl, r++, "Cross Feedback",      sliderAeFbSat,       lblAeFbSat,       0, 100,    1);
        addRow(gl, r++, "Feedback Tone",       sliderAeFbTone,      lblAeFbTone,      0, 100,    1);
        addRow(gl, r++, "Feedback Saturation", sliderAeFbDamp,      lblAeFbDamp,      0, 100,    1);
        addRow(gl, r++, "Feedback Damping",    sliderAeFbLowCut,    lblAeFbLowCut,    0, 100,    1);
        addRow(gl, r++, "Feedback Low Cut",    sliderAeFbHighCut,   lblAeFbHighCut,   20, 2000,  1);
        addRow(gl, r++, "Feedback High Cut",   sliderAeFbDiff,      lblAeFbDiff,      200, 20000, 100);
        addRow(gl, r++, "Feedback Diffusion",  sliderAeFbDiffusion, lblAeFbDiffusion, 0, 100,    1);
        aeLay->addWidget(aeFeedbackGroup);
    }

    // ── 3. Stereo (5 sliders + 2 toggles) ────────────────────────────────────
    {
        aeStereoGroup = new CollapsibleSection("Stereo");
        auto* gl = makeGrid(aeStereoGroup->content());
        int r = 0;
        addRow(gl, r++, "Stereo Width", sliderAeStereoWidthSt, lblAeStereoWidthSt, 0, 200,  1);
        addRow(gl, r++, "Balance",      sliderAeBalance,       lblAeBalance,     -100, 100,  1);
        addRow(gl, r++, "Left Level",   sliderAeLeftLevel,     lblAeLeftLevel,     0, 200,  1);
        addRow(gl, r++, "Right Level",  sliderAeRightLevel,    lblAeRightLevel,    0, 200,  1);
        addRow(gl, r++, "Mid/Side Mix", sliderAeMidSide,       lblAeMidSide,       0, 100,  1);
        addTog(gl, r++, "Ping Pong Mode", toggleAePingPongMode);
        addTog(gl, r++, "Swap Channels",  toggleAeSwapCh);
        aeLay->addWidget(aeStereoGroup);
    }

    // ── 4. Tone ──────────────────────────────────────────────────────────────
    {
        aeToneGroup = new CollapsibleSection("Tone");
        auto* gl = makeGrid(aeToneGroup->content());
        int r = 0;
        addRow(gl, r++, "Low Cut",    sliderAeToneLowCut,  lblAeToneLowCut,   20, 2000,  1);
        addRow(gl, r++, "High Cut",   sliderAeToneHighCut, lblAeToneHighCut,  1000, 20000, 100);
        addRow(gl, r++, "Bass",       sliderAeToneBass,    lblAeToneBass,    -18, 18, 0.5);
        addRow(gl, r++, "Mid",        sliderAeToneMid,     lblAeToneMid,     -18, 18, 0.5);
        addRow(gl, r++, "Treble",     sliderAeToneTreble,  lblAeToneTreble,  -18, 18, 0.5);
        addRow(gl, r++, "Presence",   sliderAeTonePresence,lblAeTonePresence,-18, 18, 0.5);
        addRow(gl, r++, "Brightness", sliderAeToneBright,  lblAeToneBright,  -18, 18, 0.5);
        aeLay->addWidget(aeToneGroup);
    }

    // ── 5. Saturation ────────────────────────────────────────────────────────
    {
        aeSatGroup = new CollapsibleSection("Saturation");
        auto* gl = makeGrid(aeSatGroup->content());
        int r = 0;
        addRow(gl, r++, "Tape Saturation",   sliderAeTapeSat,   lblAeTapeSat,   0, 100, 1);
        addRow(gl, r++, "Analog Saturation", sliderAeAnalogSat, lblAeAnalogSat, 0, 100, 1);
        addRow(gl, r++, "Drive",             sliderAeDrive,     lblAeDrive,     0, 100, 1);
        addRow(gl, r++, "Warmth",            sliderAeWarmth,    lblAeWarmth,    0, 100, 1);
        addTog(gl, r++, "Soft Clip", toggleAeSoftClip);
        aeLay->addWidget(aeSatGroup);
    }

    // ── 6. Dynamics ──────────────────────────────────────────────────────────
    {
        aeDynGroup = new CollapsibleSection("Dynamics");
        auto* gl = makeGrid(aeDynGroup->content());
        int r = 0;
        addRow(gl, r++, "Input Gain",  sliderAeInGain,  lblAeInGain,  -24, 24, 0.5);
        addRow(gl, r++, "Output Gain", sliderAeOutGain, lblAeOutGain, -24, 24, 0.5);
        addRow(gl, r++, "Wet Gain",    sliderAeWetGain, lblAeWetGain, -24, 24, 0.5);
        addRow(gl, r++, "Dry Gain",    sliderAeDryGain, lblAeDryGain, -24, 24, 0.5);
        addTog(gl, r++, "Internal Limiter", toggleAeIntLimiter);
        addTog(gl, r++, "Soft Limiter",     toggleAeSoftLimiter);
        aeLay->addWidget(aeDynGroup);
    }

    // ── 7. Mix ───────────────────────────────────────────────────────────────
    {
        aeMixGroup = new CollapsibleSection("Mix");
        auto* gl = makeGrid(aeMixGroup->content());
        int r = 0;
        addRow(gl, r++, "Wet Level", sliderAeWetLvl2, lblAeWetLvl2, 0, 100, 1);
        addRow(gl, r++, "Dry Level", sliderAeDryLvl2, lblAeDryLvl2, 0, 100, 1);
        addRow(gl, r++, "Blend",     sliderAeBlend,   lblAeBlend,   0, 100, 1);
        addRow(gl, r++, "Mix",       sliderAeMixOvr,  lblAeMixOvr,  0, 100, 1);
        aeLay->addWidget(aeMixGroup);
    }

    // ── 8. Modulation ────────────────────────────────────────────────────────
    {
        aeModGroup = new CollapsibleSection("Modulation");
        auto* gl = makeGrid(aeModGroup->content());
        int r = 0;
        addRow(gl, r++, "Wow",              sliderAeWow,       lblAeWow,       0, 100, 1);
        addRow(gl, r++, "Flutter",          sliderAeFlutter,   lblAeFlutter,   0, 100, 1);
        addRow(gl, r++, "Modulation Depth", sliderAeModDepth,  lblAeModDepth,  0, 100, 1);
        addRow(gl, r++, "Modulation Rate",  sliderAeModRate,   lblAeModRate,   0.01, 20, 0.01);
        addRow(gl, r++, "Random Drift",     sliderAeRandDrift, lblAeRandDrift, 0, 100, 1);
        aeLay->addWidget(aeModGroup);
    }

    // ── 9. Spatial ───────────────────────────────────────────────────────────
    {
        aeSpatialGroup = new CollapsibleSection("Spatial");
        auto* gl = makeGrid(aeSpatialGroup->content());
        int r = 0;
        addRow(gl, r++, "Haas Width",       sliderAeHaasW,    lblAeHaasW,    0,   40,  0.1);
        addRow(gl, r++, "Stereo Spread",    sliderAeStSpread, lblAeStSpread, 0,  100,   1);
        addRow(gl, r++, "Early Reflections",sliderAeEarlyRefl,lblAeEarlyRefl,0,  100,   1);
        addRow(gl, r++, "Reflection Level", sliderAeReflLvl,  lblAeReflLvl,  0,  100,   1);
        addRow(gl, r++, "Reflection Delay", sliderAeReflDelay,lblAeReflDelay,1,  100,   1);
        aeLay->addWidget(aeSpatialGroup);
    }

    outerLay->addWidget(aeSection);
}

// ─────────────────────────────────────────────────────────────────────────────

void AdvancedAudioTab::connectAdvancedEcho() {
    if (!aeSection) return;

    // Main section toggle = aeOn (Advanced Engine enable)
    connect(aeSection->toggle(), &ToggleSwitch::toggled, this, [this](bool on) {
        m_settings.aeOn = on;
        emitSettings();
    });

    // ── Helpers ──────────────────────────────────────────────────────────────
    // Slider that writes a float % field and formats "xx %"
    auto connPct = [this](DarkSlider* sl, QLabel* vl, float AppSettings::* field) {
        connect(sl, &QSlider::valueChanged, this, [this, sl, vl, field] {
            float v = (float)sl->valueF();
            m_settings.*field = v;
            vl->setText(QString::number((int)v) + " %");
            emitSettings();
        });
    };
    auto connMs = [this](DarkSlider* sl, QLabel* vl, float AppSettings::* field) {
        connect(sl, &QSlider::valueChanged, this, [this, sl, vl, field] {
            float v = (float)sl->valueF();
            m_settings.*field = v;
            vl->setText(QString::number((int)v) + " ms");
            emitSettings();
        });
    };
    auto connHz = [this](DarkSlider* sl, QLabel* vl, float AppSettings::* field) {
        connect(sl, &QSlider::valueChanged, this, [this, sl, vl, field] {
            float v = (float)sl->valueF();
            m_settings.*field = v;
            vl->setText(v >= 1000.f
                ? QString::number(v/1000.f,'f',1) + " kHz"
                : QString::number((int)v) + " Hz");
            emitSettings();
        });
    };
    auto connDb = [this](DarkSlider* sl, QLabel* vl, float AppSettings::* field) {
        connect(sl, &QSlider::valueChanged, this, [this, sl, vl, field] {
            float v = (float)sl->valueF();
            m_settings.*field = v;
            vl->setText(QString::number(v,'f',1) + " dB");
            emitSettings();
        });
    };
    auto connBool = [this](ToggleSwitch* ts, bool AppSettings::* field) {
        connect(ts, &ToggleSwitch::toggled, this, [this, field](bool on) {
            m_settings.*field = on;
            emitSettings();
        });
    };

    // ── Delay ────────────────────────────────────────────────────────────────
    // "Delay Time" master: sets both L and R simultaneously
    connect(sliderAeDelayTime, &QSlider::valueChanged, this, [this] {
        float v = (float)sliderAeDelayTime->valueF();
        m_settings.aeLeftDelayMs  = v;
        m_settings.aeRightDelayMs = v;
        lblAeDelayTime->setText(QString::number((int)v) + " ms");
        // Keep L/R sliders in sync visually (block their signals to avoid re-entry)
        sliderAeLeftDelay ->blockSignals(true);  sliderAeLeftDelay ->setValueF(v);
        sliderAeLeftDelay ->blockSignals(false);
        sliderAeRightDelay->blockSignals(true);  sliderAeRightDelay->setValueF(v);
        sliderAeRightDelay->blockSignals(false);
        lblAeLeftDelay ->setText(QString::number((int)v) + " ms");
        lblAeRightDelay->setText(QString::number((int)v) + " ms");
        emitSettings();
    });
    connMs(sliderAeLeftDelay,    lblAeLeftDelay,    &AppSettings::aeLeftDelayMs);
    connMs(sliderAeRightDelay,   lblAeRightDelay,   &AppSettings::aeRightDelayMs);
    // Stereo Offset: signed ms
    connect(sliderAeStereoOffset, &QSlider::valueChanged, this, [this] {
        float v = (float)sliderAeStereoOffset->valueF();
        m_settings.aeStereoOffset = v;
        lblAeStereoOffset->setText((v >= 0 ? "+" : "") + QString::number((int)v) + " ms");
        emitSettings();
    });
    connPct(sliderAeStereoWidthD, lblAeStereoWidthD, &AppSettings::aeStereoWidthD);
    connBool(toggleAeTempoSync,   &AppSettings::aeTempoSync);
    connBool(toggleAeMillisecMode,&AppSettings::aeMillisecondMode);

    // ── Feedback ─────────────────────────────────────────────────────────────
    // Row 0 "Feedback" → echoFeedback (keeps basic echo slider in sync)
    connect(sliderAeCrossFb, &QSlider::valueChanged, this, [this] {
        m_settings.echoFeedback = (float)sliderAeCrossFb->valueF();
        lblAeCrossFb->setText(QString::number((int)m_settings.echoFeedback) + " %");
        sliderEchoFeedback->blockSignals(true);
        sliderEchoFeedback->setValueF(m_settings.echoFeedback);
        sliderEchoFeedback->blockSignals(false);
        lblEchoFeedback->setText(QString::number((int)m_settings.echoFeedback) + " %");
        emitSettings();
    });
    connPct(sliderAeFbSat,       lblAeFbSat,       &AppSettings::aeCrossFeedback); // Cross Feedback
    // Row 2 "Feedback Tone" → echoTone (keeps basic echo tone slider in sync)
    connect(sliderAeFbTone, &QSlider::valueChanged, this, [this] {
        m_settings.echoTone = (float)sliderAeFbTone->valueF();
        lblAeFbTone->setText(QString::number((int)m_settings.echoTone) + " %");
        sliderEchoTone->blockSignals(true);
        sliderEchoTone->setValueF(m_settings.echoTone);
        sliderEchoTone->blockSignals(false);
        lblEchoTone->setText(QString::number((int)m_settings.echoTone) + " %");
        emitSettings();
    });
    connPct(sliderAeFbDamp,      lblAeFbDamp,      &AppSettings::aeFbSaturation); // Feedback Saturation
    connPct(sliderAeFbLowCut,    lblAeFbLowCut,    &AppSettings::aeFbDamping);    // Feedback Damping
    connHz(sliderAeFbHighCut,    lblAeFbHighCut,   &AppSettings::aeFbLowCut);     // Feedback Low Cut (Hz)
    connHz(sliderAeFbDiff,       lblAeFbDiff,      &AppSettings::aeFbHighCut);    // Feedback High Cut (Hz)
    connPct(sliderAeFbDiffusion, lblAeFbDiffusion, &AppSettings::aeFbDiffusion);  // Feedback Diffusion

    // ── Stereo ───────────────────────────────────────────────────────────────
    // Row 0 "Stereo Width" → stereoWidth (shared with main Stereo Width card)
    connect(sliderAeStereoWidthSt, &QSlider::valueChanged, this, [this] {
        float v = (float)sliderAeStereoWidthSt->valueF();
        m_settings.stereoWidth = v;
        lblAeStereoWidthSt->setText(QString::number((int)v) + " %");
        emitSettings();
    });
    // Row 1 "Balance" → aeBalance (−100..+100, displayed as signed int)
    connect(sliderAeBalance, &QSlider::valueChanged, this, [this] {
        float v = (float)sliderAeBalance->valueF();
        m_settings.aeBalance = v;
        lblAeBalance->setText((v >= 0 ? "+" : "") + QString::number((int)v));
        emitSettings();
    });
    connPct(sliderAeLeftLevel,  lblAeLeftLevel,  &AppSettings::aeLeftLevel);   // Left Level
    connPct(sliderAeRightLevel, lblAeRightLevel, &AppSettings::aeRightLevel);  // Right Level
    connPct(sliderAeMidSide,    lblAeMidSide,    &AppSettings::aeMidSideMix);  // Mid/Side Mix
    connBool(toggleAePingPongMode, &AppSettings::aePingPongMode);
    connBool(toggleAeSwapCh,       &AppSettings::aeSwapChannels);

    // ── Tone ─────────────────────────────────────────────────────────────────
    connHz(sliderAeToneLowCut,   lblAeToneLowCut,   &AppSettings::aeToneLowCut);
    connHz(sliderAeToneHighCut,  lblAeToneHighCut,  &AppSettings::aeToneHighCut);
    connDb(sliderAeToneBass,     lblAeToneBass,     &AppSettings::aeToneBass);
    connDb(sliderAeToneMid,      lblAeToneMid,      &AppSettings::aeToneMid);
    connDb(sliderAeToneTreble,   lblAeToneTreble,   &AppSettings::aeToneTreble);
    connDb(sliderAeTonePresence, lblAeTonePresence, &AppSettings::aeTonePresence);
    connDb(sliderAeToneBright,   lblAeToneBright,   &AppSettings::aeToneBrightness);

    // ── Saturation ───────────────────────────────────────────────────────────
    connPct(sliderAeTapeSat,   lblAeTapeSat,   &AppSettings::aeTapeSat);
    connPct(sliderAeAnalogSat, lblAeAnalogSat, &AppSettings::aeAnalogSat);
    connPct(sliderAeDrive,     lblAeDrive,     &AppSettings::aeDrive);
    connPct(sliderAeWarmth,    lblAeWarmth,    &AppSettings::aeWarmth);
    connBool(toggleAeSoftClip, &AppSettings::aeSoftClip);

    // ── Dynamics ─────────────────────────────────────────────────────────────
    connDb(sliderAeInGain,   lblAeInGain,   &AppSettings::aeInputGainDb);
    connDb(sliderAeOutGain,  lblAeOutGain,  &AppSettings::aeOutputGainDb);
    connDb(sliderAeWetGain,  lblAeWetGain,  &AppSettings::aeWetGainDb);
    connDb(sliderAeDryGain,  lblAeDryGain,  &AppSettings::aeDryGainDb);
    connBool(toggleAeIntLimiter,  &AppSettings::aeIntLimiter);
    connBool(toggleAeSoftLimiter, &AppSettings::aeSoftLimiter);

    // ── Mix ──────────────────────────────────────────────────────────────────
    connPct(sliderAeWetLvl2, lblAeWetLvl2, &AppSettings::aeWetLevel2);
    connPct(sliderAeDryLvl2, lblAeDryLvl2, &AppSettings::aeDryLevel2);
    connPct(sliderAeBlend,   lblAeBlend,   &AppSettings::aeBlend);
    connect(sliderAeMixOvr, &QSlider::valueChanged, this, [this] {
        m_settings.aeMixOverride = (float)sliderAeMixOvr->valueF();
        lblAeMixOvr->setText(QString::number((int)m_settings.aeMixOverride) + " %");
        emitSettings();
    });

    // ── Modulation ───────────────────────────────────────────────────────────
    connPct(sliderAeWow,       lblAeWow,       &AppSettings::aeWow);
    connPct(sliderAeFlutter,   lblAeFlutter,   &AppSettings::aeFlutter);
    connPct(sliderAeModDepth,  lblAeModDepth,  &AppSettings::aeModDepth);
    connect(sliderAeModRate, &QSlider::valueChanged, this, [this] {
        float v = (float)sliderAeModRate->valueF();
        m_settings.aeModRate = v;
        lblAeModRate->setText(QString::number(v,'f',2) + " Hz");
        emitSettings();
    });
    connPct(sliderAeRandDrift, lblAeRandDrift, &AppSettings::aeRandomDrift);

    // ── Spatial ──────────────────────────────────────────────────────────────
    connect(sliderAeHaasW, &QSlider::valueChanged, this, [this] {
        float v = (float)sliderAeHaasW->valueF();
        m_settings.aeHaasWidth = v;
        lblAeHaasW->setText(QString::number(v,'f',1) + " ms");
        emitSettings();
    });
    connPct(sliderAeStSpread,  lblAeStSpread,  &AppSettings::aeStereoSpread);
    connPct(sliderAeEarlyRefl, lblAeEarlyRefl, &AppSettings::aeEarlyReflections);
    connPct(sliderAeReflLvl,   lblAeReflLvl,   &AppSettings::aeReflLevel);
    connMs(sliderAeReflDelay,  lblAeReflDelay, &AppSettings::aeReflDelay);
}

// ─────────────────────────────────────────────────────────────────────────────

void AdvancedAudioTab::refreshAdvancedEchoFromSettings(const AppSettings& s) {
    if (!aeSection) return;

    // Expand/collapse without animation (called from refreshFromSettings)
    aeSection->toggle()->blockSignals(true);
    aeSection->toggle()->setChecked(s.aeOn);
    aeSection->toggle()->blockSignals(false);
    aeSection->setExpanded(s.aeOn, false);

    // ── Delay ────────────────────────────────────────────────────────────────
    float avgDelay = (s.aeLeftDelayMs + s.aeRightDelayMs) * 0.5f;
    sliderAeDelayTime->setValueF(avgDelay);
    lblAeDelayTime->setText(QString::number((int)avgDelay) + " ms");
    sliderAeLeftDelay->setValueF(s.aeLeftDelayMs);
    lblAeLeftDelay->setText(QString::number((int)s.aeLeftDelayMs) + " ms");
    sliderAeRightDelay->setValueF(s.aeRightDelayMs);
    lblAeRightDelay->setText(QString::number((int)s.aeRightDelayMs) + " ms");
    sliderAeStereoOffset->setValueF(s.aeStereoOffset);
    lblAeStereoOffset->setText((s.aeStereoOffset>=0?"+":"")+QString::number((int)s.aeStereoOffset)+" ms");
    sliderAeStereoWidthD->setValueF(s.aeStereoWidthD);
    lblAeStereoWidthD->setText(QString::number((int)s.aeStereoWidthD) + " %");
    toggleAeTempoSync->setChecked(s.aeTempoSync);
    toggleAeMillisecMode->setChecked(s.aeMillisecondMode);

    // ── Feedback ─────────────────────────────────────────────────────────────
    auto setHzFb = [](DarkSlider* sl, QLabel* vl, float v) {
        sl->setValueF(v);
        vl->setText(v>=1000.f ? QString::number(v/1000.f,'f',1)+" kHz"
                               : QString::number((int)v)+" Hz");
    };
    sliderAeCrossFb->setValueF(s.echoFeedback);
    lblAeCrossFb->setText(QString::number((int)s.echoFeedback) + " %");
    sliderAeFbSat->setValueF(s.aeCrossFeedback);
    lblAeFbSat->setText(QString::number((int)s.aeCrossFeedback) + " %");
    sliderAeFbTone->setValueF(s.echoTone);
    lblAeFbTone->setText(QString::number((int)s.echoTone) + " %");
    sliderAeFbDamp->setValueF(s.aeFbSaturation);
    lblAeFbDamp->setText(QString::number((int)s.aeFbSaturation) + " %");
    sliderAeFbLowCut->setValueF(s.aeFbDamping);
    lblAeFbLowCut->setText(QString::number((int)s.aeFbDamping) + " %");
    setHzFb(sliderAeFbHighCut,   lblAeFbHighCut,   s.aeFbLowCut);
    setHzFb(sliderAeFbDiff,      lblAeFbDiff,      s.aeFbHighCut);
    sliderAeFbDiffusion->setValueF(s.aeFbDiffusion);
    lblAeFbDiffusion->setText(QString::number((int)s.aeFbDiffusion) + " %");

    // ── Stereo ───────────────────────────────────────────────────────────────
    sliderAeStereoWidthSt->setValueF(s.stereoWidth);
    lblAeStereoWidthSt->setText(QString::number((int)s.stereoWidth) + " %");
    sliderAeBalance->setValueF(s.aeBalance);
    lblAeBalance->setText((s.aeBalance>=0?"+":"")+QString::number((int)s.aeBalance));
    sliderAeLeftLevel->setValueF(s.aeLeftLevel);
    lblAeLeftLevel->setText(QString::number((int)s.aeLeftLevel) + " %");
    sliderAeRightLevel->setValueF(s.aeRightLevel);
    lblAeRightLevel->setText(QString::number((int)s.aeRightLevel) + " %");
    sliderAeMidSide->setValueF(s.aeMidSideMix);
    lblAeMidSide->setText(QString::number((int)s.aeMidSideMix) + " %");
    toggleAePingPongMode->setChecked(s.aePingPongMode);
    toggleAeSwapCh->setChecked(s.aeSwapChannels);

    // ── Tone ─────────────────────────────────────────────────────────────────
    auto setHz = [](DarkSlider* sl, QLabel* vl, float v) {
        sl->setValueF(v);
        vl->setText(v>=1000.f?QString::number(v/1000.f,'f',1)+" kHz":QString::number((int)v)+" Hz");
    };
    auto setDb = [](DarkSlider* sl, QLabel* vl, float v) {
        sl->setValueF(v);
        vl->setText(QString::number(v,'f',1)+" dB");
    };
    setHz(sliderAeToneLowCut,   lblAeToneLowCut,   s.aeToneLowCut);
    setHz(sliderAeToneHighCut,  lblAeToneHighCut,  s.aeToneHighCut);
    setDb(sliderAeToneBass,     lblAeToneBass,     s.aeToneBass);
    setDb(sliderAeToneMid,      lblAeToneMid,      s.aeToneMid);
    setDb(sliderAeToneTreble,   lblAeToneTreble,   s.aeToneTreble);
    setDb(sliderAeTonePresence, lblAeTonePresence, s.aeTonePresence);
    setDb(sliderAeToneBright,   lblAeToneBright,   s.aeToneBrightness);

    // ── Saturation ───────────────────────────────────────────────────────────
    auto setPct = [](DarkSlider* sl, QLabel* vl, float v) {
        sl->setValueF(v);
        vl->setText(QString::number((int)v) + " %");
    };
    setPct(sliderAeTapeSat,   lblAeTapeSat,   s.aeTapeSat);
    setPct(sliderAeAnalogSat, lblAeAnalogSat, s.aeAnalogSat);
    setPct(sliderAeDrive,     lblAeDrive,     s.aeDrive);
    setPct(sliderAeWarmth,    lblAeWarmth,    s.aeWarmth);
    toggleAeSoftClip->setChecked(s.aeSoftClip);

    // ── Dynamics ─────────────────────────────────────────────────────────────
    setDb(sliderAeInGain,  lblAeInGain,  s.aeInputGainDb);
    setDb(sliderAeOutGain, lblAeOutGain, s.aeOutputGainDb);
    setDb(sliderAeWetGain, lblAeWetGain, s.aeWetGainDb);
    setDb(sliderAeDryGain, lblAeDryGain, s.aeDryGainDb);
    toggleAeIntLimiter->setChecked(s.aeIntLimiter);
    toggleAeSoftLimiter->setChecked(s.aeSoftLimiter);

    // ── Mix ──────────────────────────────────────────────────────────────────
    setPct(sliderAeWetLvl2, lblAeWetLvl2, s.aeWetLevel2);
    setPct(sliderAeDryLvl2, lblAeDryLvl2, s.aeDryLevel2);
    setPct(sliderAeBlend,   lblAeBlend,   s.aeBlend);
    sliderAeMixOvr->setValueF(std::max(0.f, s.aeMixOverride)); // clamp -1 default to 0 for display
    lblAeMixOvr->setText(s.aeMixOverride < 0.f
        ? "auto"
        : QString::number((int)s.aeMixOverride) + " %");

    // ── Modulation ───────────────────────────────────────────────────────────
    setPct(sliderAeWow,       lblAeWow,       s.aeWow);
    setPct(sliderAeFlutter,   lblAeFlutter,   s.aeFlutter);
    setPct(sliderAeModDepth,  lblAeModDepth,  s.aeModDepth);
    sliderAeModRate->setValueF(s.aeModRate);
    lblAeModRate->setText(QString::number(s.aeModRate,'f',2) + " Hz");
    setPct(sliderAeRandDrift, lblAeRandDrift, s.aeRandomDrift);

    // ── Spatial ──────────────────────────────────────────────────────────────
    sliderAeHaasW->setValueF(s.aeHaasWidth);
    lblAeHaasW->setText(QString::number(s.aeHaasWidth,'f',1) + " ms");
    setPct(sliderAeStSpread,  lblAeStSpread,  s.aeStereoSpread);
    setPct(sliderAeEarlyRefl, lblAeEarlyRefl, s.aeEarlyReflections);
    setPct(sliderAeReflLvl,   lblAeReflLvl,   s.aeReflLevel);
    sliderAeReflDelay->setValueF(s.aeReflDelay);
    lblAeReflDelay->setText(QString::number((int)s.aeReflDelay) + " ms");
}
