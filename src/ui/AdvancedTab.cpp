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
        lbl->setStyleSheet("color:#8b5cf6; font-size:11px;");
        lbl->setFixedWidth(72);
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
        vlay->addWidget(card);
    }

    // ── Advanced Reverb Engine ────────────────────────────────────────────────
    // All 14 parameters wired end-to-end:
    //   UI slider → AppSettings field → AudioProcessor → ReverbEngine / FDNReverb DSP
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);

        // ── Header ────────────────────────────────────────────────────────────
        {
            auto* hdr = new QHBoxLayout();
            toggleReverbEngine = new ToggleSwitch();
            auto* title = makeLabel("Advanced Reverb Engine", 12, true);
            auto* note  = makeDimLabel("(overrides basic reverb when on)");
            note->setStyleSheet("color:#555; font-size:10px;");
            hdr->addWidget(title);
            hdr->addWidget(note);
            hdr->addStretch();
            hdr->addWidget(toggleReverbEngine);
            cl->addLayout(hdr);
        }

        // ── Section: Room character ───────────────────────────────────────────
        cl->addWidget(makeDimLabel("Room Character"));
        makeRow(0.25, 3.0,  0.05, slRoomSize, lblRoomSize);
        makeRow(0,    100,  1,    slDensity,  lblDensity);
        makeRow(0,    100,  1,    slModDepth, lblModDepth);
        makeRow(0,    100,  1,    slModRate,  lblModRate);
        addRow(cl, "Room Size",     slRoomSize, lblRoomSize);
        addRow(cl, "Density %",     slDensity,  lblDensity);
        addRow(cl, "Mod Depth %",   slModDepth, lblModDepth);
        addRow(cl, "Mod Rate %",    slModRate,  lblModRate);

        // ── Section: Early Reflections ────────────────────────────────────────
        cl->addSpacing(6);
        cl->addWidget(makeDimLabel("Early Reflections"));
        makeRow(0,    500,  5,    slERDelay,  lblERDelay);   // 0–500 ms
        makeRow(0,    100,  1,    slERLevel,  lblERLevel);
        addRow(cl, "ER Delay ms",   slERDelay,  lblERDelay);
        addRow(cl, "ER Level %",    slERLevel,  lblERLevel);

        // ── Section: Late Tail ────────────────────────────────────────────────
        cl->addSpacing(6);
        cl->addWidget(makeDimLabel("Late Tail"));
        makeRow(0, 100, 1, slLateLevel, lblLateLevel);
        addRow(cl, "Late Reverb %", slLateLevel, lblLateLevel);

        // ── Section: Spectral Shaping ─────────────────────────────────────────
        cl->addSpacing(6);
        cl->addWidget(makeDimLabel("Spectral Shaping"));
        makeRow(0,    100,    1,   slHfDamp,  lblHfDamp);
        makeRow(0,    100,    1,   slLfDamp,  lblLfDamp);
        makeRow(20,   500,    5,   slLowCut,  lblLowCut);
        makeRow(1000, 20000, 100,  slHighCut, lblHighCut);
        addRow(cl, "HF Damping %",  slHfDamp,  lblHfDamp);
        addRow(cl, "LF Damping %",  slLfDamp,  lblLfDamp);
        addRow(cl, "Low Cut Hz",    slLowCut,  lblLowCut);
        addRow(cl, "High Cut Hz",   slHighCut, lblHighCut);

        // ── Section: Stereo & Mix ─────────────────────────────────────────────
        cl->addSpacing(6);
        cl->addWidget(makeDimLabel("Stereo & Mix"));
        makeRow(0, 200, 1,  slRevWidth, lblRevWidth);
        makeRow(0, 100, 1,  slWetLevel, lblWetLevel);
        makeRow(0, 100, 1,  slDryLevel, lblDryLevel);
        addRow(cl, "Stereo Width %", slRevWidth, lblRevWidth);
        addRow(cl, "Wet Level %",    slWetLevel, lblWetLevel);
        addRow(cl, "Dry Level %",    slDryLevel, lblDryLevel);

        vlay->addWidget(card);
    }

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

    // ── Advanced Reverb Engine ────────────────────────────────────────────────
    connect(toggleReverbEngine, &ToggleSwitch::toggled, this, [this](bool on){
        m_settings.reverbEngineOn = on; emitSettings();
    });
    // Room character
    bindSlider(slRoomSize, lblRoomSize, "",     2, [this](float v){ m_settings.reverbRoomSize           = v; });
    bindSlider(slDensity,  lblDensity,  " %",   0, [this](float v){ m_settings.reverbDensity             = v; });
    bindSlider(slModDepth, lblModDepth, " %",   0, [this](float v){ m_settings.reverbModulationDepth     = v; });
    bindSlider(slModRate,  lblModRate,  " %",   0, [this](float v){ m_settings.reverbModulationRate      = v; });
    // Early reflections
    bindSlider(slERDelay,  lblERDelay,  " ms",  0, [this](float v){ m_settings.reverbEarlyReflectionDelay= v; });
    bindSlider(slERLevel,  lblERLevel,  " %",   0, [this](float v){ m_settings.reverbEarlyReflectionLevel= v; });
    // Late tail
    bindSlider(slLateLevel,lblLateLevel," %",   0, [this](float v){ m_settings.reverbLateReverbLevel     = v; });
    // Spectral shaping
    bindSlider(slHfDamp,   lblHfDamp,   " %",   0, [this](float v){ m_settings.reverbHfDamping           = v; });
    bindSlider(slLfDamp,   lblLfDamp,   " %",   0, [this](float v){ m_settings.reverbLfDamping           = v; });
    bindSlider(slLowCut,   lblLowCut,   " Hz",  0, [this](float v){ m_settings.reverbLowCut              = v; });
    bindSlider(slHighCut,  lblHighCut,  " Hz",  0, [this](float v){ m_settings.reverbHighCut             = v; });
    // Stereo & mix
    bindSlider(slRevWidth, lblRevWidth, " %",   0, [this](float v){ m_settings.reverbStereoWidth         = v; });
    bindSlider(slWetLevel, lblWetLevel, " %",   0, [this](float v){ m_settings.reverbWetLevel            = v; });
    bindSlider(slDryLevel, lblDryLevel, " %",   0, [this](float v){ m_settings.reverbDryLevel            = v; });
}

void AdvancedTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;

    // Acoustic engine
    toggleAcoustic->setChecked(s.acousticEngineOn);
    slSurround ->setValueF(s.fxSurround);    lblSurround ->setText(QString::number((int)s.fxSurround)    +" %");
    slCrystal  ->setValueF(s.fxCrystalizer); lblCrystal  ->setText(QString::number((int)s.fxCrystalizer) +" %");
    slBass     ->setValueF(s.fxBass);        lblBass     ->setText(QString::number((int)s.fxBass)         +" %");
    slCrossover->setValueF(s.fxCrossover);   lblCrossover->setText(QString::number((int)s.fxCrossover)    +" Hz");
    slSmartVol ->setValueF(s.fxSmartVolume); lblSmartVol ->setText(QString::number((int)s.fxSmartVolume)  +" %");
    slDialog   ->setValueF(s.fxDialogPlus);  lblDialog   ->setText(QString::number((int)s.fxDialogPlus)   +" %");

    // Speaker config
    toggleSpeaker->setChecked(s.speakerConfigOn);
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

    // Advanced reverb engine
    toggleReverbEngine->setChecked(s.reverbEngineOn);
    slRoomSize ->setValueF(s.reverbRoomSize);                lblRoomSize ->setText(QString::number(s.reverbRoomSize,'f',2));
    slDensity  ->setValueF(s.reverbDensity);                 lblDensity  ->setText(QString::number((int)s.reverbDensity)              +" %");
    slModDepth ->setValueF(s.reverbModulationDepth);         lblModDepth ->setText(QString::number((int)s.reverbModulationDepth)      +" %");
    slModRate  ->setValueF(s.reverbModulationRate);          lblModRate  ->setText(QString::number((int)s.reverbModulationRate)       +" %");
    slERDelay  ->setValueF(s.reverbEarlyReflectionDelay);    lblERDelay  ->setText(QString::number((int)s.reverbEarlyReflectionDelay) +" ms");
    slERLevel  ->setValueF(s.reverbEarlyReflectionLevel);    lblERLevel  ->setText(QString::number((int)s.reverbEarlyReflectionLevel) +" %");
    slLateLevel->setValueF(s.reverbLateReverbLevel);         lblLateLevel->setText(QString::number((int)s.reverbLateReverbLevel)      +" %");
    slHfDamp   ->setValueF(s.reverbHfDamping);               lblHfDamp   ->setText(QString::number((int)s.reverbHfDamping)            +" %");
    slLfDamp   ->setValueF(s.reverbLfDamping);               lblLfDamp   ->setText(QString::number((int)s.reverbLfDamping)            +" %");
    slLowCut   ->setValueF(s.reverbLowCut);                  lblLowCut   ->setText(QString::number((int)s.reverbLowCut)               +" Hz");
    slHighCut  ->setValueF(s.reverbHighCut);                 lblHighCut  ->setText(QString::number((int)s.reverbHighCut)              +" Hz");
    slRevWidth ->setValueF(s.reverbStereoWidth);             lblRevWidth ->setText(QString::number((int)s.reverbStereoWidth)          +" %");
    slWetLevel ->setValueF(s.reverbWetLevel);                lblWetLevel ->setText(QString::number((int)s.reverbWetLevel)             +" %");
    slDryLevel ->setValueF(s.reverbDryLevel);                lblDryLevel ->setText(QString::number((int)s.reverbDryLevel)             +" %");
}

void AdvancedTab::emitSettings() {
    m_settings.save();
    emit settingsChanged(m_settings);
}
