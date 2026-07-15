#include "AdvancedTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>

AdvancedTab::AdvancedTab(QWidget* parent) : QWidget(parent) {
    buildUI();
    connectAll();
}

void AdvancedTab::addRow(QLayout* lay, const char* name, DarkSlider* sl, QLabel* vl) {
    auto* row = new QHBoxLayout();
    auto* nm  = new QLabel(name);
    nm->setStyleSheet("color:#aaa; font-size:11px;");
    nm->setFixedWidth(130);
    vl->setFixedWidth(72);
    row->addWidget(nm);
    row->addWidget(sl, 1);
    row->addWidget(vl);
    static_cast<QVBoxLayout*>(lay)->addLayout(row);
}

void AdvancedTab::buildUI() {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border:none; background:transparent; }");
    auto* container = new QWidget();
    auto* vlay = new QVBoxLayout(container);
    vlay->setSpacing(14);
    vlay->setContentsMargins(12,12,12,12);

    // Convenience factory: new slider + label wired to the passed-in pointers
    auto makeRow = [&](double lo, double hi, double step,
                       DarkSlider*& sl, QLabel*& lbl) {
        sl  = new DarkSlider(Qt::Horizontal);
        sl->setRangeF(lo, hi, step);
        lbl = new QLabel();
        lbl->setStyleSheet("color:#d0d0d0; font-size:11px; font-weight:500;");
        lbl->setFixedWidth(72);
    };

    // ── Acoustic Engine ───────────────────────────────────────────────────────
    {
        sectionAcoustic = new CollapsibleSection("Acoustic Engine (SBX Pro Studio)");
        toggleAcoustic  = sectionAcoustic->toggle();
        auto* cl = new QVBoxLayout(sectionAcoustic->content());
        cl->setContentsMargins(0,0,0,0);
        cl->setSpacing(8);

        makeRow(0,100,1, slSurround, lblSurround);
        makeRow(0,100,1, slCrystal,  lblCrystal);
        makeRow(0,100,1, slBass,     lblBass);
        makeRow(20,500,5,slCrossover,lblCrossover);
        makeRow(0,100,1, slSmartVol, lblSmartVol);
        makeRow(0,100,1, slDialog,   lblDialog);
        addRow(cl, "Surround %",    slSurround,  lblSurround);
        addRow(cl, "Crystalizer %", slCrystal,   lblCrystal);
        addRow(cl, "Bass %",        slBass,      lblBass);
        addRow(cl, "Crossover Hz",  slCrossover, lblCrossover);
        addRow(cl, "Smart Volume%", slSmartVol,  lblSmartVol);
        addRow(cl, "Dialog Plus%",  slDialog,    lblDialog);
        vlay->addWidget(sectionAcoustic);
    }

    // ── Speaker Config ────────────────────────────────────────────────────────
    {
        sectionSpeaker = new CollapsibleSection("Speaker Configuration");
        toggleSpeaker  = sectionSpeaker->toggle();
        auto* cl = new QVBoxLayout(sectionSpeaker->content());
        cl->setContentsMargins(0,0,0,0);
        cl->setSpacing(8);

        comboSpeakerMode = new QComboBox();
        comboSpeakerMode->setStyleSheet(
            "QComboBox { background:#171717; color:#f2f2f2; border:1px solid #2a2a2a;"
            "border-radius:6px; padding:5px 9px; font-size:11px; }"
            "QComboBox:hover { border:1px solid #3a3a3a; }"
            "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; "
            "border:1px solid #333; selection-background-color:#333333; outline:none; }");
        for (auto* m : {"headphones","stereo","2.1","4.0","4.1","5.1","7.1"})
            comboSpeakerMode->addItem(m);
        cl->addWidget(comboSpeakerMode);

        cl->addWidget(makeLabel("Virtual Speaker Shifter", 11, false, "#aaa"));
        makeRow(0,200,1,  slFrontW,     lblFrontW);
        makeRow(0,200,1,  slRearW,      lblRearW);
        makeRow(0,200,1,  slCenterDist, lblCenterDist);
        makeRow(0,200,1,  slRearDist,   lblRearDist);
        makeRow(0,30,0.5, slSubDist,    lblSubDist);
        addRow(cl, "Front Width %",  slFrontW,     lblFrontW);
        addRow(cl, "Rear Width %",   slRearW,      lblRearW);
        addRow(cl, "Center Dist %",  slCenterDist, lblCenterDist);
        addRow(cl, "Rear Dist %",    slRearDist,   lblRearDist);
        addRow(cl, "Sub Dist ft",    slSubDist,    lblSubDist);

        cl->addWidget(makeLabel("Channel Levels", 11, false, "#aaa"));
        makeRow(0,200,1, slLvFL,  lblLvFL);
        makeRow(0,200,1, slLvFR,  lblLvFR);
        makeRow(0,200,1, slLvC,   lblLvC);
        makeRow(0,200,1, slLvSub, lblLvSub);
        makeRow(0,200,1, slLvRL,  lblLvRL);
        makeRow(0,200,1, slLvRR,  lblLvRR);
        addRow(cl, "FL %",  slLvFL,  lblLvFL);
        addRow(cl, "FR %",  slLvFR,  lblLvFR);
        addRow(cl, "C %",   slLvC,   lblLvC);
        addRow(cl, "Sub %", slLvSub, lblLvSub);
        addRow(cl, "RL %",  slLvRL,  lblLvRL);
        addRow(cl, "RR %",  slLvRR,  lblLvRR);
        vlay->addWidget(sectionSpeaker);
    }

    // Advanced Reverb Engine now lives inside the redesigned Reverb section
    // on the Live tab (see LiveTab::buildReverbColumn) — it is no longer a
    // separate section here, so the whole Reverb feature reads as one unit.

    vlay->addStretch();
    scroll->setWidget(container);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0,0,0,0);
    outer->addWidget(scroll);
}

void AdvancedTab::connectAll() {
    // Generic slider binder: captures setter by value; setter captures [&] → this
    auto bindSlider = [&](DarkSlider* sl, QLabel* lbl,
                          const QString& suffix, int decimals, auto setter) {
        connect(sl, &QSlider::valueChanged, this, [=]{
            float v = (float)sl->valueF();
            setter(v);
            lbl->setText(QString::number((double)v, 'f', decimals) + suffix);
            emitSettings();
        });
    };

    // ── Acoustic Engine ───────────────────────────────────────────────────────
    connect(toggleAcoustic, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.acousticEngineOn = on; emitSettings();
    });
    bindSlider(slSurround,  lblSurround,  " %",  0, [this](float v){ m_settings.fxSurround    = v; });
    bindSlider(slCrystal,   lblCrystal,   " %",  0, [this](float v){ m_settings.fxCrystalizer = v; });
    bindSlider(slBass,      lblBass,      " %",  0, [this](float v){ m_settings.fxBass         = v; });
    bindSlider(slCrossover, lblCrossover, " Hz", 0, [this](float v){ m_settings.fxCrossover    = v; });
    bindSlider(slSmartVol,  lblSmartVol,  " %",  0, [this](float v){ m_settings.fxSmartVolume  = v; });
    bindSlider(slDialog,    lblDialog,    " %",  0, [this](float v){ m_settings.fxDialogPlus   = v; });

    // ── Speaker Config ────────────────────────────────────────────────────────
    connect(toggleSpeaker, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.speakerConfigOn = on; emitSettings();
    });
    connect(comboSpeakerMode, &QComboBox::currentTextChanged, this, [this](const QString& m){
        m_settings.speakerMode = m; emitSettings();
    });
    bindSlider(slFrontW,     lblFrontW,     " %",  0, [this](float v){ m_settings.speakerFrontWidth     = v; });
    bindSlider(slRearW,      lblRearW,      " %",  0, [this](float v){ m_settings.speakerRearWidth      = v; });
    bindSlider(slCenterDist, lblCenterDist, " %",  0, [this](float v){ m_settings.speakerCenterDistance = v; });
    bindSlider(slRearDist,   lblRearDist,   " %",  0, [this](float v){ m_settings.speakerRearDistance   = v; });
    bindSlider(slSubDist,    lblSubDist,    " ft", 1, [this](float v){ m_settings.speakerSubDistance    = v; });
    bindSlider(slLvFL,  lblLvFL,  " %", 0, [this](float v){ m_settings.speakerLevelFL  = v; });
    bindSlider(slLvFR,  lblLvFR,  " %", 0, [this](float v){ m_settings.speakerLevelFR  = v; });
    bindSlider(slLvC,   lblLvC,   " %", 0, [this](float v){ m_settings.speakerLevelC   = v; });
    bindSlider(slLvSub, lblLvSub, " %", 0, [this](float v){ m_settings.speakerLevelSub = v; });
    bindSlider(slLvRL,  lblLvRL,  " %", 0, [this](float v){ m_settings.speakerLevelRL  = v; });
    bindSlider(slLvRR,  lblLvRR,  " %", 0, [this](float v){ m_settings.speakerLevelRR  = v; });

    // Advanced Reverb Engine wiring now lives in LiveTab (see connectSignals
    // there) alongside the rest of the redesigned Reverb section.
}

void AdvancedTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;

    // Acoustic engine
    // Block signals so this programmatic sync doesn't trigger the animated
    // expand/collapse — setExpanded(..., false) below does an instant sync.
    toggleAcoustic->blockSignals(true);
    toggleAcoustic->setChecked(s.acousticEngineOn);
    toggleAcoustic->blockSignals(false);
    sectionAcoustic->setExpanded(s.acousticEngineOn, false);
    slSurround ->setValueF(s.fxSurround);    lblSurround ->setText(QString::number((int)s.fxSurround)    +" %");
    slCrystal  ->setValueF(s.fxCrystalizer); lblCrystal  ->setText(QString::number((int)s.fxCrystalizer) +" %");
    slBass     ->setValueF(s.fxBass);        lblBass     ->setText(QString::number((int)s.fxBass)         +" %");
    slCrossover->setValueF(s.fxCrossover);   lblCrossover->setText(QString::number((int)s.fxCrossover)    +" Hz");
    slSmartVol ->setValueF(s.fxSmartVolume); lblSmartVol ->setText(QString::number((int)s.fxSmartVolume)  +" %");
    slDialog   ->setValueF(s.fxDialogPlus);  lblDialog   ->setText(QString::number((int)s.fxDialogPlus)   +" %");

    // Speaker config
    toggleSpeaker->blockSignals(true);
    toggleSpeaker->setChecked(s.speakerConfigOn);
    toggleSpeaker->blockSignals(false);
    sectionSpeaker->setExpanded(s.speakerConfigOn, false);
    {
        int idx = comboSpeakerMode->findText(s.speakerMode);
        if (idx >= 0) comboSpeakerMode->setCurrentIndex(idx);
    }
    slFrontW    ->setValueF(s.speakerFrontWidth);     lblFrontW    ->setText(QString::number((int)s.speakerFrontWidth)     +" %");
    slRearW     ->setValueF(s.speakerRearWidth);      lblRearW     ->setText(QString::number((int)s.speakerRearWidth)      +" %");
    slCenterDist->setValueF(s.speakerCenterDistance); lblCenterDist->setText(QString::number((int)s.speakerCenterDistance) +" %");
    slRearDist  ->setValueF(s.speakerRearDistance);   lblRearDist  ->setText(QString::number((int)s.speakerRearDistance)   +" %");
    slSubDist   ->setValueF(s.speakerSubDistance);    lblSubDist   ->setText(QString::number(s.speakerSubDistance,'f',1)   +" ft");
    slLvFL ->setValueF(s.speakerLevelFL);  lblLvFL ->setText(QString::number((int)s.speakerLevelFL)  +" %");
    slLvFR ->setValueF(s.speakerLevelFR);  lblLvFR ->setText(QString::number((int)s.speakerLevelFR)  +" %");
    slLvC  ->setValueF(s.speakerLevelC);   lblLvC  ->setText(QString::number((int)s.speakerLevelC)   +" %");
    slLvSub->setValueF(s.speakerLevelSub); lblLvSub->setText(QString::number((int)s.speakerLevelSub) +" %");
    slLvRL ->setValueF(s.speakerLevelRL);  lblLvRL ->setText(QString::number((int)s.speakerLevelRL)  +" %");
    slLvRR ->setValueF(s.speakerLevelRR);  lblLvRR ->setText(QString::number((int)s.speakerLevelRR)  +" %");

    // Advanced Reverb Engine sync now happens in LiveTab::refreshFromSettings.
}

void AdvancedTab::emitSettings() {
    m_settings.save();
    emit settingsChanged(m_settings);
}
