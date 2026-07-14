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
    nm->setFixedWidth(120);
    vl->setFixedWidth(70);
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

    auto makeSliderLabel = [](double lo, double hi, double step, DarkSlider*& sl, QLabel*& lbl) {
        sl  = new DarkSlider(Qt::Horizontal);
        sl->setRangeF(lo, hi, step);
        lbl = new QLabel();
        lbl->setStyleSheet("color:#8b5cf6; font-size:11px;");
        lbl->setFixedWidth(70);
    };

    // ── Acoustic Engine ───────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleAcoustic = new ToggleSwitch();
        hdr->addWidget(makeLabel("Acoustic Engine (SBX Pro Studio)", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleAcoustic);
        cl->addLayout(hdr);

        makeSliderLabel(0,100,1, slSurround, lblSurround);
        makeSliderLabel(0,100,1, slCrystal,  lblCrystal);
        makeSliderLabel(0,100,1, slBass,      lblBass);
        makeSliderLabel(20,500,5,slCrossover, lblCrossover);
        makeSliderLabel(0,100,1, slSmartVol,  lblSmartVol);
        makeSliderLabel(0,100,1, slDialog,    lblDialog);
        addRow(cl, "Surround %",    slSurround,  lblSurround);
        addRow(cl, "Crystalizer %", slCrystal,   lblCrystal);
        addRow(cl, "Bass %",        slBass,      lblBass);
        addRow(cl, "Crossover Hz",  slCrossover, lblCrossover);
        addRow(cl, "Smart Volume%", slSmartVol,  lblSmartVol);
        addRow(cl, "Dialog Plus%",  slDialog,    lblDialog);
        vlay->addWidget(card);
    }

    // ── Speaker Config ────────────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleSpeaker = new ToggleSwitch();
        hdr->addWidget(makeLabel("Speaker Configuration", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleSpeaker);
        cl->addLayout(hdr);

        comboSpeakerMode = new QComboBox();
        comboSpeakerMode->setStyleSheet(
            "QComboBox { background:#1a1a1a; color:#fff; border:1px solid #333;"
            "border-radius:6px; padding:4px 8px; font-size:11px; }"
            "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; "
            "border:1px solid #333; selection-background-color:#8b5cf6; }");
        for (auto* m : {"headphones","stereo","2.1","4.0","4.1","5.1","7.1"})
            comboSpeakerMode->addItem(m);
        cl->addWidget(comboSpeakerMode);

        // Virtual speaker shifter
        cl->addWidget(makeLabel("Virtual Speaker Shifter", 11, false, "#aaa"));
        makeSliderLabel(0,200,1,  slFrontW,     lblFrontW);
        makeSliderLabel(0,200,1,  slRearW,      lblRearW);
        makeSliderLabel(0,200,1,  slCenterDist, lblCenterDist);
        makeSliderLabel(0,200,1,  slRearDist,   lblRearDist);
        makeSliderLabel(0,30,0.5, slSubDist,    lblSubDist);
        addRow(cl, "Front Width %",    slFrontW,     lblFrontW);
        addRow(cl, "Rear Width %",     slRearW,      lblRearW);
        addRow(cl, "Center Dist %",    slCenterDist, lblCenterDist);
        addRow(cl, "Rear Dist %",      slRearDist,   lblRearDist);
        addRow(cl, "Sub Dist ft",      slSubDist,    lblSubDist);

        // Channel levels
        cl->addWidget(makeLabel("Channel Levels", 11, false, "#aaa"));
        makeSliderLabel(0,200,1, slLvFL,  lblLvFL);
        makeSliderLabel(0,200,1, slLvFR,  lblLvFR);
        makeSliderLabel(0,200,1, slLvC,   lblLvC);
        makeSliderLabel(0,200,1, slLvSub, lblLvSub);
        makeSliderLabel(0,200,1, slLvRL,  lblLvRL);
        makeSliderLabel(0,200,1, slLvRR,  lblLvRR);
        addRow(cl, "FL %",  slLvFL,  lblLvFL);
        addRow(cl, "FR %",  slLvFR,  lblLvFR);
        addRow(cl, "C %",   slLvC,   lblLvC);
        addRow(cl, "Sub %", slLvSub, lblLvSub);
        addRow(cl, "RL %",  slLvRL,  lblLvRL);
        addRow(cl, "RR %",  slLvRR,  lblLvRR);
        vlay->addWidget(card);
    }

    // ── Advanced Reverb Engine ────────────────────────────────────────────────
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        auto* hdr  = new QHBoxLayout();
        toggleReverbEngine = new ToggleSwitch();
        hdr->addWidget(makeLabel("Advanced Reverb Engine", 12, true));
        hdr->addStretch();
        hdr->addWidget(toggleReverbEngine);
        cl->addLayout(hdr);

        makeSliderLabel(0.25,3,0.05, slRoomSize,  lblRoomSize);
        makeSliderLabel(0,100,1,     slERLevel,   lblERLevel);
        makeSliderLabel(0,100,1,     slLateLevel, lblLateLevel);
        makeSliderLabel(0,100,1,     slHfDamp,    lblHfDamp);
        makeSliderLabel(0,100,1,     slLfDamp,    lblLfDamp);
        makeSliderLabel(0,200,1,     slRevWidth,  lblRevWidth);
        makeSliderLabel(0,100,1,     slModDepth,  lblModDepth);
        makeSliderLabel(0,100,1,     slModRate,   lblModRate);
        makeSliderLabel(20,500,5,    slLowCut,    lblLowCut);
        makeSliderLabel(1000,20000,100,slHighCut, lblHighCut);
        makeSliderLabel(0,100,1,     slDensity,   lblDensity);
        makeSliderLabel(0,100,1,     slWetLevel,  lblWetLevel);
        makeSliderLabel(0,100,1,     slDryLevel,  lblDryLevel);
        addRow(cl, "Room Size",       slRoomSize,  lblRoomSize);
        addRow(cl, "Early Refl %",   slERLevel,   lblERLevel);
        addRow(cl, "Late Reverb %",  slLateLevel, lblLateLevel);
        addRow(cl, "HF Damping %",   slHfDamp,    lblHfDamp);
        addRow(cl, "LF Damping %",   slLfDamp,    lblLfDamp);
        addRow(cl, "Stereo Width %", slRevWidth,  lblRevWidth);
        addRow(cl, "Mod Depth %",    slModDepth,  lblModDepth);
        addRow(cl, "Mod Rate %",     slModRate,   lblModRate);
        addRow(cl, "Low Cut Hz",     slLowCut,    lblLowCut);
        addRow(cl, "High Cut Hz",    slHighCut,   lblHighCut);
        addRow(cl, "Density %",      slDensity,   lblDensity);
        addRow(cl, "Wet Level %",    slWetLevel,  lblWetLevel);
        addRow(cl, "Dry Level %",    slDryLevel,  lblDryLevel);
        vlay->addWidget(card);
    }

    vlay->addStretch();
    scroll->setWidget(container);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0,0,0,0);
    outer->addWidget(scroll);
}

void AdvancedTab::connectAll() {
    auto bindSlider = [&](DarkSlider* sl, QLabel* lbl,
                          const QString& suffix, auto getter, auto setter) {
        connect(sl, &QSlider::valueChanged, this, [=]{
            float v = (float)sl->valueF();
            setter(v);
            lbl->setText(QString::number(v,'f',
                         (suffix.contains(".")||suffix.isEmpty())? 2:0)+suffix);
            emitSettings();
        });
        (void)getter;
    };

    // Acoustic engine
    connect(toggleAcoustic, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.acousticEngineOn=on; emitSettings();
    });
    bindSlider(slSurround,  lblSurround,  " %",   [&]{return m_settings.fxSurround;},    [&](float v){ m_settings.fxSurround=v; });
    bindSlider(slCrystal,   lblCrystal,   " %",   [&]{return m_settings.fxCrystalizer;}, [&](float v){ m_settings.fxCrystalizer=v; });
    bindSlider(slBass,      lblBass,      " %",   [&]{return m_settings.fxBass;},        [&](float v){ m_settings.fxBass=v; });
    bindSlider(slCrossover, lblCrossover, " Hz",  [&]{return m_settings.fxCrossover;},   [&](float v){ m_settings.fxCrossover=v; });
    bindSlider(slSmartVol,  lblSmartVol,  " %",   [&]{return m_settings.fxSmartVolume;}, [&](float v){ m_settings.fxSmartVolume=v; });
    bindSlider(slDialog,    lblDialog,    " %",   [&]{return m_settings.fxDialogPlus;},  [&](float v){ m_settings.fxDialogPlus=v; });

    // Speaker config
    connect(toggleSpeaker, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.speakerConfigOn=on; emitSettings();
    });
    connect(comboSpeakerMode, &QComboBox::currentTextChanged, this, [this](const QString& m){
        m_settings.speakerMode=m; emitSettings();
    });
    bindSlider(slFrontW,     lblFrontW,     " %",   []{return 0.f;}, [&](float v){ m_settings.speakerFrontWidth=v; });
    bindSlider(slRearW,      lblRearW,      " %",   []{return 0.f;}, [&](float v){ m_settings.speakerRearWidth=v; });
    bindSlider(slCenterDist, lblCenterDist, " %",   []{return 0.f;}, [&](float v){ m_settings.speakerCenterDistance=v; });
    bindSlider(slRearDist,   lblRearDist,   " %",   []{return 0.f;}, [&](float v){ m_settings.speakerRearDistance=v; });
    bindSlider(slSubDist,    lblSubDist,    " ft",  []{return 0.f;}, [&](float v){ m_settings.speakerSubDistance=v; });
    bindSlider(slLvFL,  lblLvFL,  " %", []{return 0.f;}, [&](float v){ m_settings.speakerLevelFL=v;  });
    bindSlider(slLvFR,  lblLvFR,  " %", []{return 0.f;}, [&](float v){ m_settings.speakerLevelFR=v;  });
    bindSlider(slLvC,   lblLvC,   " %", []{return 0.f;}, [&](float v){ m_settings.speakerLevelC=v;   });
    bindSlider(slLvSub, lblLvSub, " %", []{return 0.f;}, [&](float v){ m_settings.speakerLevelSub=v; });
    bindSlider(slLvRL,  lblLvRL,  " %", []{return 0.f;}, [&](float v){ m_settings.speakerLevelRL=v;  });
    bindSlider(slLvRR,  lblLvRR,  " %", []{return 0.f;}, [&](float v){ m_settings.speakerLevelRR=v;  });

    // Reverb engine
    connect(toggleReverbEngine, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.reverbEngineOn=on; emitSettings();
    });
    bindSlider(slRoomSize,  lblRoomSize,  "",     []{return 0.f;}, [&](float v){ m_settings.reverbRoomSize=v; });
    bindSlider(slERLevel,   lblERLevel,   " %",   []{return 0.f;}, [&](float v){ m_settings.reverbEarlyReflectionLevel=v; });
    bindSlider(slLateLevel, lblLateLevel, " %",   []{return 0.f;}, [&](float v){ m_settings.reverbLateReverbLevel=v; });
    bindSlider(slHfDamp,    lblHfDamp,    " %",   []{return 0.f;}, [&](float v){ m_settings.reverbHfDamping=v; });
    bindSlider(slLfDamp,    lblLfDamp,    " %",   []{return 0.f;}, [&](float v){ m_settings.reverbLfDamping=v; });
    bindSlider(slRevWidth,  lblRevWidth,  " %",   []{return 0.f;}, [&](float v){ m_settings.reverbStereoWidth=v; });
    bindSlider(slModDepth,  lblModDepth,  " %",   []{return 0.f;}, [&](float v){ m_settings.reverbModulationDepth=v; });
    bindSlider(slModRate,   lblModRate,   " %",   []{return 0.f;}, [&](float v){ m_settings.reverbModulationRate=v; });
    bindSlider(slLowCut,    lblLowCut,    " Hz",  []{return 0.f;}, [&](float v){ m_settings.reverbLowCut=v; });
    bindSlider(slHighCut,   lblHighCut,   " Hz",  []{return 0.f;}, [&](float v){ m_settings.reverbHighCut=v; });
    bindSlider(slDensity,   lblDensity,   " %",   []{return 0.f;}, [&](float v){ m_settings.reverbDensity=v; });
    bindSlider(slWetLevel,  lblWetLevel,  " %",   []{return 0.f;}, [&](float v){ m_settings.reverbWetLevel=v; });
    bindSlider(slDryLevel,  lblDryLevel,  " %",   []{return 0.f;}, [&](float v){ m_settings.reverbDryLevel=v; });
}

void AdvancedTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;

    toggleAcoustic->setChecked(s.acousticEngineOn);
    slSurround->setValueF(s.fxSurround);   lblSurround->setText(QString::number((int)s.fxSurround)+" %");
    slCrystal->setValueF(s.fxCrystalizer); lblCrystal->setText(QString::number((int)s.fxCrystalizer)+" %");
    slBass->setValueF(s.fxBass);           lblBass->setText(QString::number((int)s.fxBass)+" %");
    slCrossover->setValueF(s.fxCrossover); lblCrossover->setText(QString::number((int)s.fxCrossover)+" Hz");
    slSmartVol->setValueF(s.fxSmartVolume);lblSmartVol->setText(QString::number((int)s.fxSmartVolume)+" %");
    slDialog->setValueF(s.fxDialogPlus);   lblDialog->setText(QString::number((int)s.fxDialogPlus)+" %");

    toggleSpeaker->setChecked(s.speakerConfigOn);
    int idx = comboSpeakerMode->findText(s.speakerMode);
    if (idx>=0) comboSpeakerMode->setCurrentIndex(idx);
    slFrontW->setValueF(s.speakerFrontWidth);    lblFrontW->setText(QString::number((int)s.speakerFrontWidth)+" %");
    slRearW->setValueF(s.speakerRearWidth);      lblRearW->setText(QString::number((int)s.speakerRearWidth)+" %");
    slCenterDist->setValueF(s.speakerCenterDistance); lblCenterDist->setText(QString::number((int)s.speakerCenterDistance)+" %");
    slRearDist->setValueF(s.speakerRearDistance);     lblRearDist->setText(QString::number((int)s.speakerRearDistance)+" %");
    slSubDist->setValueF(s.speakerSubDistance);   lblSubDist->setText(QString::number(s.speakerSubDistance,'f',1)+" ft");
    slLvFL->setValueF(s.speakerLevelFL);   lblLvFL->setText(QString::number((int)s.speakerLevelFL)+" %");
    slLvFR->setValueF(s.speakerLevelFR);   lblLvFR->setText(QString::number((int)s.speakerLevelFR)+" %");
    slLvC->setValueF(s.speakerLevelC);     lblLvC->setText(QString::number((int)s.speakerLevelC)+" %");
    slLvSub->setValueF(s.speakerLevelSub); lblLvSub->setText(QString::number((int)s.speakerLevelSub)+" %");
    slLvRL->setValueF(s.speakerLevelRL);   lblLvRL->setText(QString::number((int)s.speakerLevelRL)+" %");
    slLvRR->setValueF(s.speakerLevelRR);   lblLvRR->setText(QString::number((int)s.speakerLevelRR)+" %");

    toggleReverbEngine->setChecked(s.reverbEngineOn);
    slRoomSize->setValueF(s.reverbRoomSize);            lblRoomSize->setText(QString::number(s.reverbRoomSize,'f',2));
    slERLevel->setValueF(s.reverbEarlyReflectionLevel); lblERLevel->setText(QString::number((int)s.reverbEarlyReflectionLevel)+" %");
    slLateLevel->setValueF(s.reverbLateReverbLevel);    lblLateLevel->setText(QString::number((int)s.reverbLateReverbLevel)+" %");
    slHfDamp->setValueF(s.reverbHfDamping);             lblHfDamp->setText(QString::number((int)s.reverbHfDamping)+" %");
    slLfDamp->setValueF(s.reverbLfDamping);             lblLfDamp->setText(QString::number((int)s.reverbLfDamping)+" %");
    slRevWidth->setValueF(s.reverbStereoWidth);         lblRevWidth->setText(QString::number((int)s.reverbStereoWidth)+" %");
    slModDepth->setValueF(s.reverbModulationDepth);     lblModDepth->setText(QString::number((int)s.reverbModulationDepth)+" %");
    slModRate->setValueF(s.reverbModulationRate);       lblModRate->setText(QString::number((int)s.reverbModulationRate)+" %");
    slLowCut->setValueF(s.reverbLowCut);                lblLowCut->setText(QString::number((int)s.reverbLowCut)+" Hz");
    slHighCut->setValueF(s.reverbHighCut);              lblHighCut->setText(QString::number((int)s.reverbHighCut)+" Hz");
    slDensity->setValueF(s.reverbDensity);              lblDensity->setText(QString::number((int)s.reverbDensity)+" %");
    slWetLevel->setValueF(s.reverbWetLevel);            lblWetLevel->setText(QString::number((int)s.reverbWetLevel)+" %");
    slDryLevel->setValueF(s.reverbDryLevel);            lblDryLevel->setText(QString::number((int)s.reverbDryLevel)+" %");
}

void AdvancedTab::emitSettings() {
    m_settings.save();
    emit settingsChanged(m_settings);
}
