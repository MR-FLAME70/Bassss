#include "mainwindow.h"
#include "ui/LiveTab.h"
#include "ui/AdvancedTab.h"
#include "ui/AdvancedAudioTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QMessageBox>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QPixmap>

// ── Data roles for device combo boxes ─────────────────────────────────────────
// UserRole+0 = device ID string (QString)
// UserRole+1 = audio source mode string: "playback" | "microphone" (source combo only)
static const int kRoleDeviceId    = Qt::UserRole + 0;
static const int kRoleSourceMode  = Qt::UserRole + 1;

// Stack page indices in m_deviceStack
static const int kStackPageMic      = 0;
static const int kStackPagePlayback = 1;

// ──────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    globalSettings().load();

    setFixedSize(860, 600);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    // Frameless windows are square by default; a translucent surface lets us
    // paint our own anti-aliased rounded-rect background in paintEvent() and
    // leave the true corners fully transparent instead of hard-edged.
    setAttribute(Qt::WA_TranslucentBackground, true);

    m_proc    = new AudioProcessor();
    m_capture = new AudioCapture(m_proc, this);

    connect(m_capture, &AudioCapture::errorOccurred,
            this,      &MainWindow::onAudioError);

    applyDarkTheme();
    // Override the global QWidget{background-color:#000000} rule for just
    // this top-level window so translucency actually shows; the rounded
    // fill itself is painted in paintEvent().
    setStyleSheet("background: transparent;");
    buildUI();

    m_proc->applySettings(globalSettings());
}

MainWindow::~MainWindow() {
    stopCapture();
}

// ──────────────────────────────────────────────────────────────────────────────
// Dark stylesheet
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::applyDarkTheme() {
    qApp->setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #000000;
            color: #f2f2f2;
            font-family: "Segoe UI", "Inter", "Helvetica Neue", Arial, sans-serif;
            font-size: 12px;
        }
        QToolTip {
            background: #1a1a1a; color: #f2f2f2; border: 1px solid #333;
            border-radius: 5px; padding: 5px 9px;
        }
        QLabel { background: transparent; }
        QCheckBox { spacing: 8px; }
        QCheckBox::indicator {
            width: 15px; height: 15px; border-radius: 3px;
            border: 1px solid #3a3a3a; background: #161616;
        }
        QCheckBox::indicator:checked { background: #e8e8e8; border-color: #e8e8e8; }
        QLineEdit {
            background: #131313; color: #f2f2f2; border: 1px solid #2a2a2a;
            border-radius: 6px; padding: 4px 8px; selection-background-color: #3a3a3a;
        }
        QLineEdit:focus { border: 1px solid #565656; }
        QScrollBar:vertical {
            background: transparent; width: 8px; margin: 2px; border-radius: 4px;
        }
        QScrollBar::handle:vertical {
            background: #333; border-radius: 4px; min-height: 24px;
        }
        QScrollBar::handle:vertical:hover { background: #4a4a4a; }
        QScrollBar:horizontal {
            background: transparent; height: 8px; margin: 2px; border-radius: 4px;
        }
        QScrollBar::handle:horizontal {
            background: #333; border-radius: 4px; min-width: 24px;
        }
        QScrollBar::handle:horizontal:hover { background: #4a4a4a; }
        QScrollBar::add-line, QScrollBar::sub-line { background: none; height: 0; width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
    )");
}

// ──────────────────────────────────────────────────────────────────────────────
// Build UI
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::buildUI() {
    auto* central = new QWidget();
    // Transparent so MainWindow's own rounded-rect paint (behind this widget)
    // shows through at the corners instead of a flat black rectangle.
    central->setStyleSheet("background: transparent;");
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    // 1. Custom title bar
    root->addWidget(buildTitleBar());

    // 2. Tab bar
    // Now that File Export is gone, only 3 tabs remain. Expand them to share
    // the full title-bar width evenly instead of clumping left with a dead
    // gap on the right, and center each tab's contents for a balanced look.
    m_tabBar = new QTabBar();
    m_tabBar->addTab("🎵 Live Tab Audio");
    m_tabBar->addTab("⚙ Advanced");
    m_tabBar->addTab("🎚 Advanced Audio");
    m_tabBar->setExpanding(true);
    m_tabBar->setElideMode(Qt::ElideNone);
    m_tabBar->setStyleSheet(R"(
        QTabBar {
            background: #0a0a0a;
            border-bottom: 1px solid #232323;
        }
        QTabBar::tab {
            background: transparent;
            color: #7a7a7a;
            padding: 10px 20px;
            font-size: 12px;
            letter-spacing: 0.3px;
            border: none;
            border-bottom: 2px solid transparent;
            margin-right: 2px;
            alignment: center;
        }
        QTabBar::tab:selected {
            color: #ffffff;
            border-bottom: 2px solid #e8e8e8;
            font-weight: 600;
        }
        QTabBar::tab:hover:!selected { color: #d0d0d0; }
    )");
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    root->addWidget(m_tabBar);

    // 3. Tab content (stacked)
    auto* stack = new QStackedWidget();
    m_liveTab      = new LiveTab(m_proc);
    m_advTab       = new AdvancedTab();
    m_advAudioTab  = new AdvancedAudioTab();
    stack->addWidget(m_liveTab);
    stack->addWidget(m_advTab);
    stack->addWidget(m_advAudioTab);
    m_stack = stack;
    root->addWidget(stack, 1);

    // 4. Status bar
    root->addWidget(buildStatusBar());

    // Wire settings-changed signals
    connect(m_liveTab,     &LiveTab::settingsChanged,         this, &MainWindow::onSettingsChanged);
    connect(m_advTab,      &AdvancedTab::settingsChanged,     this, &MainWindow::onSettingsChanged);
    connect(m_advAudioTab, &AdvancedAudioTab::settingsChanged,this, &MainWindow::onSettingsChanged);

    // Populate device dropdowns and restore saved selection
    populateDeviceDropdowns();

    const auto& s = globalSettings();
    m_liveTab->refreshFromSettings(s);
    m_advTab->refreshFromSettings(s);
    m_advAudioTab->refreshFromSettings(s);
}

// ──────────────────────────────────────────────────────────────────────────────
// Title bar
// ──────────────────────────────────────────────────────────────────────────────
QWidget* MainWindow::buildTitleBar() {
    m_titleBar = new QWidget();
    m_titleBar->setFixedHeight(46);
    // Round only the top two corners so they line up exactly with the
    // window's own rounded corners painted behind it.
    m_titleBar->setStyleSheet(QString(
        "background: #0a0a0a; border-bottom: 1px solid #1a1a1a;"
        "border-top-left-radius: %1px; border-top-right-radius: %1px;"
    ).arg(kCornerRadius));

    auto* lay = new QHBoxLayout(m_titleBar);
    lay->setContentsMargins(12,0,8,0);
    lay->setSpacing(6);

    // Logo
    QLabel* logo = new QLabel();
    logo->setPixmap(QIcon(":/icons/icon16.png").pixmap(20,20));
    lay->addWidget(logo);

    // App title
    QLabel* title = new QLabel("Bass Nuker");
    title->setStyleSheet("color:#ffffff; font-size:14px; font-weight:600; letter-spacing:1.5px;");
    lay->addWidget(title);

    lay->addSpacing(10);

    // Shared combo-box chrome, reused for source, device, and output combos.
    const QString comboStyle =
        "QComboBox { background:#131313; color:#ccc; border:1px solid #262626; border-radius:5px;"
        "padding:2px 8px; font-size:11px; }"
        "QComboBox:hover { border:1px solid #3a3a3a; }"
        "QComboBox::drop-down { border:none; width:18px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; border:1px solid #333;"
        "selection-background-color:#333333; font-size:11px; outline:none; }";

    // ── Audio Source (Microphone / Playback Device) ────────────────────────────
    // This is the master routing choice: it decides WHAT gets processed.
    // Nothing is captured until the user picks one — the mic is never opened
    // implicitly.
    auto* srcGroup = new QWidget();
    srcGroup->setStyleSheet("background:transparent;");
    auto* srcLay = new QVBoxLayout(srcGroup);
    srcLay->setContentsMargins(0,0,0,0);
    srcLay->setSpacing(1);

    auto* srcLabel = new QLabel("Audio Source");
    srcLabel->setStyleSheet("color:#555; font-size:9px; letter-spacing:0.5px;");
    srcLay->addWidget(srcLabel);

    m_comboSource = new QComboBox();
    m_comboSource->setFixedWidth(130);
    m_comboSource->setFixedHeight(26);
    m_comboSource->setStyleSheet(comboStyle);
    m_comboSource->addItem("🎤 Microphone",      QVariant());
    m_comboSource->setItemData(0, QString("microphone"), kRoleSourceMode);
    m_comboSource->addItem("🔊 Playback Device", QVariant());
    m_comboSource->setItemData(1, QString("playback"), kRoleSourceMode);
    srcLay->addWidget(m_comboSource);
    lay->addWidget(srcGroup);

    // ── Device selector (swaps between Mic list and Playback list) ────────────
    auto* devGroup = new QWidget();
    devGroup->setStyleSheet("background:transparent;");
    auto* devLay = new QVBoxLayout(devGroup);
    devLay->setContentsMargins(0,0,0,0);
    devLay->setSpacing(1);

    m_deviceGroupLabel = new QLabel("Playback Device");
    m_deviceGroupLabel->setStyleSheet("color:#555; font-size:9px; letter-spacing:0.5px;");
    devLay->addWidget(m_deviceGroupLabel);

    m_deviceStack = new QStackedWidget();
    m_deviceStack->setFixedWidth(190);
    m_deviceStack->setFixedHeight(26);

    m_comboMic = new QComboBox();
    m_comboMic->setFixedHeight(26);
    m_comboMic->setStyleSheet(comboStyle);
    m_deviceStack->insertWidget(kStackPageMic, m_comboMic);

    m_comboPlayback = new QComboBox();
    m_comboPlayback->setFixedHeight(26);
    m_comboPlayback->setStyleSheet(comboStyle);
    m_deviceStack->insertWidget(kStackPagePlayback, m_comboPlayback);

    devLay->addWidget(m_deviceStack);
    lay->addWidget(devGroup);

    // ── Output Device ─────────────────────────────────────────────────────────
    auto* outGroup = new QWidget();
    outGroup->setStyleSheet("background:transparent;");
    auto* outLay = new QVBoxLayout(outGroup);
    outLay->setContentsMargins(0,0,0,0);
    outLay->setSpacing(1);

    auto* outLabel = new QLabel("Output Device");
    outLabel->setStyleSheet("color:#555; font-size:9px; letter-spacing:0.5px;");
    outLay->addWidget(outLabel);

    m_comboOutput = new QComboBox();
    m_comboOutput->setFixedWidth(150);
    m_comboOutput->setFixedHeight(26);
    m_comboOutput->setStyleSheet(comboStyle);
    outLay->addWidget(m_comboOutput);
    lay->addWidget(outGroup);

    lay->addStretch();

    // ── Start/Stop ────────────────────────────────────────────────────────────
    m_btnStart = new QPushButton("▶ Start");
    m_btnStart->setFixedSize(80, 28);
    m_btnStart->setStyleSheet(R"(
        QPushButton {
            background: #ececec; color: #0a0a0a; border: 1px solid #ececec;
            border-radius: 6px; font-size: 12px; font-weight: 600;
        }
        QPushButton:hover    { background: #ffffff; border-color: #ffffff; }
        QPushButton:pressed  { background: #c9c9c9; border-color: #c9c9c9; }
        QPushButton:checked  { background: #dc2626; color: #fff; border-color: #dc2626; }
        QPushButton:checked:hover { background: #ef4444; border-color: #ef4444; }
    )");
    m_btnStart->setCheckable(true);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartStop);
    lay->addWidget(m_btnStart);

    lay->addSpacing(8);

    // ── Window controls ───────────────────────────────────────────────────────
    auto makeBtn = [](const QString& text, const QString& hoverColor) {
        auto* b = new QPushButton(text);
        b->setFixedSize(28, 28);
        b->setFlat(true);
        b->setStyleSheet(QString(
            "QPushButton { background:transparent; color:#666; border:none; font-size:14px; }"
            "QPushButton:hover { background:%1; color:#fff; border-radius:4px; }"
        ).arg(hoverColor));
        return b;
    };

    auto* btnMin   = makeBtn("−", "#333");
    auto* btnClose = makeBtn("×", "#dc2626");
    connect(btnMin,   &QPushButton::clicked, this, &QMainWindow::showMinimized);
    connect(btnClose, &QPushButton::clicked, this, &QMainWindow::close);
    lay->addWidget(btnMin);
    lay->addWidget(btnClose);

    // ── Wire device-change signals ─────────────────────────────────────────────
    // Changing any of these while running restarts capture with the new
    // routing immediately; changing them while stopped just remembers the
    // choice for the next Start.
    connect(m_comboSource,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAudioSourceChanged);
    connect(m_comboMic,      QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onMicDeviceChanged);
    connect(m_comboPlayback, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPlaybackDeviceChanged);
    connect(m_comboOutput,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);

    return m_titleBar;
}

// ──────────────────────────────────────────────────────────────────────────────
// Status bar
// ──────────────────────────────────────────────────────────────────────────────
QWidget* MainWindow::buildStatusBar() {
    auto* bar = new QWidget();
    bar->setFixedHeight(22);
    // Round only the bottom two corners, mirroring the title bar's top ones.
    bar->setStyleSheet(QString(
        "background: #0a0a0a; border-top: 1px solid #1a1a1a;"
        "border-bottom-left-radius: %1px; border-bottom-right-radius: %1px;"
    ).arg(kCornerRadius));
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12,0,12,0);

    m_statusLabel = new QLabel("Stopped");
    m_statusLabel->setStyleSheet("color: #666; font-size: 10px;");
    m_deviceLabel = new QLabel("48000 Hz · stereo");
    m_deviceLabel->setStyleSheet("color: #444; font-size: 10px;");
    auto* ver = new QLabel("Bass Nuker v6.9.0");
    ver->setStyleSheet("color: #333; font-size: 10px;");

    lay->addWidget(m_statusLabel);
    lay->addStretch();
    lay->addWidget(m_deviceLabel);
    lay->addStretch();
    lay->addWidget(ver);
    return bar;
}

// ──────────────────────────────────────────────────────────────────────────────
// Device enumeration & dropdown population
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::populateDeviceDropdowns() {
    // Block signals while we populate so we don't trigger the change handlers
    // (and, critically, so we never trip a spurious capture start).
    m_comboSource  ->blockSignals(true);
    m_comboMic     ->blockSignals(true);
    m_comboPlayback->blockSignals(true);
    m_comboOutput  ->blockSignals(true);

    m_comboMic     ->clear();
    m_comboPlayback->clear();
    m_comboOutput  ->clear();

    const auto& s = globalSettings();

    // ── Enumerate all WASAPI endpoints (render loopback + capture mics) ───────
    m_inputSources = AudioCapture::enumerateInputSources();

    // "Default microphone" first, then every capture (recording) endpoint —
    // i.e. "Any recording device" on the system.
    m_comboMic->addItem("🎤 Default Microphone", QVariant());
    m_comboMic->setItemData(0, QString(""), kRoleDeviceId);
    int savedMicIdx = 0;

    // "Default playback device" first, then every render endpoint — Speakers,
    // Headphones, VoiceMeeter Input/AUX, VB-Cable, or any other Windows
    // playback device shows up here automatically since it comes straight
    // from the OS's render-endpoint list.
    m_comboPlayback->addItem("🔊 Default Playback Device", QVariant());
    m_comboPlayback->setItemData(0, QString(""), kRoleDeviceId);
    int savedPlaybackIdx = 0;

    for (const auto& d : m_inputSources) {
        QString qid   = QString::fromStdString(d.id);
        QString label = QString::fromStdString(d.name);

        if (d.type == AudioDeviceType::Microphone) {
            int idx = m_comboMic->count();
            m_comboMic->addItem(label, QVariant());
            m_comboMic->setItemData(idx, qid, kRoleDeviceId);
            if (!s.micDeviceId.isEmpty() && qid == s.micDeviceId)
                savedMicIdx = idx;
        } else {
            int idx = m_comboPlayback->count();
            m_comboPlayback->addItem(label, QVariant());
            m_comboPlayback->setItemData(idx, qid, kRoleDeviceId);
            if (!s.playbackDeviceId.isEmpty() && qid == s.playbackDeviceId)
                savedPlaybackIdx = idx;
        }
    }
    m_comboMic     ->setCurrentIndex(savedMicIdx);
    m_comboPlayback->setCurrentIndex(savedPlaybackIdx);

    // ── Audio Source: restore last mode (defaults to "playback") ──────────────
    bool wantMic = (s.audioSourceMode == "microphone");
    m_comboSource->setCurrentIndex(wantMic ? 0 : 1);
    m_deviceStack->setCurrentIndex(wantMic ? kStackPageMic : kStackPagePlayback);
    m_deviceGroupLabel->setText(wantMic ? "Microphone Device" : "Playback Device");

    // ── Output: enumerate PortAudio render devices ─────────────────────────────
    m_outputDevices = AudioCapture::enumerateOutputDevices();

    m_comboOutput->addItem("Default Output", QVariant());
    m_comboOutput->setItemData(0, QString(""), kRoleDeviceId);

    int savedOutputIdx = 0;

    for (int i = 0; i < (int)m_outputDevices.size(); ++i) {
        const auto& d = m_outputDevices[i];
        QString qid   = QString::fromStdString(d.id);
        m_comboOutput->addItem(QString::fromStdString(d.name), QVariant());
        int comboIdx = i + 1;
        m_comboOutput->setItemData(comboIdx, qid, kRoleDeviceId);

        if (!s.outputDeviceId.isEmpty() && qid == s.outputDeviceId)
            savedOutputIdx = comboIdx;
    }
    m_comboOutput->setCurrentIndex(savedOutputIdx);

    m_comboSource  ->blockSignals(false);
    m_comboMic     ->blockSignals(false);
    m_comboPlayback->blockSignals(false);
    m_comboOutput  ->blockSignals(false);
}

// ──────────────────────────────────────────────────────────────────────────────
// Helpers to read current combo selection
// ──────────────────────────────────────────────────────────────────────────────
bool MainWindow::sourceIsMicrophone() const {
    return m_comboSource->currentData(kRoleSourceMode).toString() == "microphone";
}

std::string MainWindow::selectedInputId() const {
    return sourceIsMicrophone()
               ? m_comboMic->currentData(kRoleDeviceId).toString().toStdString()
               : m_comboPlayback->currentData(kRoleDeviceId).toString().toStdString();
}

AudioDeviceType MainWindow::selectedInputType() const {
    return sourceIsMicrophone() ? AudioDeviceType::Microphone
                                : AudioDeviceType::Loopback;
}

std::string MainWindow::selectedOutputId() const {
    return m_comboOutput->currentData(kRoleDeviceId).toString().toStdString();
}

// ──────────────────────────────────────────────────────────────────────────────
// Persist the current Audio Source + device selections immediately, so a
// restart (even without ever pressing Start) remembers what the user picked.
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::persistDeviceSelection() {
    auto& s = globalSettings();
    s.audioSourceMode  = sourceIsMicrophone() ? "microphone" : "playback";
    s.micDeviceId      = QString::fromStdString(
                              m_comboMic->currentData(kRoleDeviceId).toString().toStdString());
    s.playbackDeviceId = QString::fromStdString(
                              m_comboPlayback->currentData(kRoleDeviceId).toString().toStdString());
    s.outputDeviceId   = QString::fromStdString(selectedOutputId());
    s.save();
}

// ──────────────────────────────────────────────────────────────────────────────
// Start / Stop
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onStartStop() {
    if (!m_running)
        startCapture();
    else
        stopCapture();
}

void MainWindow::startCapture() {
    auto& s = globalSettings();

    // Persist current selection
    persistDeviceSelection();

    // Errors are delivered via the errorOccurred signal → onAudioError,
    // which shows a dialog and calls stopCapture().  We just reset the button
    // here so the UI doesn't stay in the "running" state if open() fails.
    QString err;
    bool ok = m_capture->open(selectedInputId(),
                              selectedInputType(),
                              selectedOutputId(),
                              (double)s.sampleRate,
                              s.bufferSize,
                              err);
    if (!ok) {
        m_btnStart->setChecked(false);
        // errorOccurred was already emitted inside AudioCapture::open()
        return;
    }

    m_running = true;
    m_btnStart->setText("■ Stop");
    m_btnStart->setChecked(true);

    // Status message reflects capture mode
    bool isLoopback = (selectedInputType() == AudioDeviceType::Loopback);
    m_statusLabel->setText(isLoopback ? "Running · WASAPI Loopback"
                                      : "Running · Microphone");
    m_statusLabel->setStyleSheet("color: #22c55e; font-size: 10px;");
    m_deviceLabel->setText(QString("%1 Hz · stereo")
                           .arg((int)m_capture->actualSampleRate()));

    m_liveTab->startAudioDevice(0, 0, m_capture->actualSampleRate(), s.bufferSize);
    m_proc->applySettings(s);
}

void MainWindow::stopCapture() {
    m_capture->close();
    m_running = false;
    m_btnStart->setText("▶ Start");
    m_btnStart->setChecked(false);
    m_statusLabel->setText("Stopped");
    m_statusLabel->setStyleSheet("color: #666; font-size: 10px;");
    m_liveTab->stopAudioDevice();
}

// ──────────────────────────────────────────────────────────────────────────────
// Device / source changed (combo boxes)
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onAudioSourceChanged(int /*idx*/) {
    bool wantMic = sourceIsMicrophone();
    m_deviceStack->setCurrentIndex(wantMic ? kStackPageMic : kStackPagePlayback);
    m_deviceGroupLabel->setText(wantMic ? "Microphone Device" : "Playback Device");

    persistDeviceSelection();
    if (m_running) {
        stopCapture();
        startCapture();
    }
}

void MainWindow::onMicDeviceChanged(int /*idx*/) {
    persistDeviceSelection();
    // Only affects the live stream if the mic is the active source.
    if (m_running && sourceIsMicrophone()) {
        stopCapture();
        startCapture();
    }
}

void MainWindow::onPlaybackDeviceChanged(int /*idx*/) {
    persistDeviceSelection();
    // Only affects the live stream if playback loopback is the active source.
    if (m_running && !sourceIsMicrophone()) {
        stopCapture();
        startCapture();
    }
}

void MainWindow::onOutputDeviceChanged(int /*idx*/) {
    persistDeviceSelection();
    if (m_running) {
        stopCapture();
        startCapture();
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Settings changed (from any tab)
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onSettingsChanged(const AppSettings& s) {
    globalSettings() = s;
    m_proc->applySettings(s);
    m_liveTab->refreshFromSettings(s);
    m_advTab->refreshFromSettings(s);
    m_advAudioTab->refreshFromSettings(s);
}

void MainWindow::onAudioError(const QString& msg) {
    QMessageBox::warning(this, "Audio Error", msg);
    stopCapture();
}

// ──────────────────────────────────────────────────────────────────────────────
// Tab switch: crossfade instead of an instant cut.
//
// Approach: grab a snapshot of the outgoing page into a plain overlay label
// (cheap to fade — no live repaint cost, so Live Tab's VU meter/spectrum
// timers can't make it stutter), switch the stack to the new page right
// away, and run two opacity animations in parallel: the overlay 1→0 and the
// new page 0→1. Both effects are torn down the moment the animation ends so
// steady-state rendering pays zero extra compositing cost.
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onTabChanged(int idx) {
    auto* stack  = static_cast<QStackedWidget*>(m_stack);
    int   oldIdx = stack->currentIndex();
    if (oldIdx == idx) return;

    // A tab click mid-transition should feel instant, not queue up behind
    // the running animation — drop it and start clean.
    if (m_tabAnim) {
        m_tabAnim->stop();
        m_tabAnim->deleteLater();
        m_tabAnim = nullptr;
    }
    if (m_tabFadeOverlay) {
        m_tabFadeOverlay->deleteLater();
        m_tabFadeOverlay = nullptr;
    }
    QWidget* oldWidget = stack->widget(oldIdx);
    // Clears any leftover incoming-fade effect from an interrupted previous
    // transition so the snapshot below captures it at full opacity.
    if (oldWidget->graphicsEffect()) oldWidget->setGraphicsEffect(nullptr);

    QPixmap snapshot = oldWidget->grab();
    m_tabFadeOverlay = new QLabel(stack);
    m_tabFadeOverlay->setPixmap(snapshot);
    m_tabFadeOverlay->setGeometry(stack->rect());
    m_tabFadeOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tabOverlayEffect = new QGraphicsOpacityEffect(m_tabFadeOverlay);
    m_tabFadeOverlay->setGraphicsEffect(m_tabOverlayEffect);
    m_tabFadeOverlay->show();
    m_tabFadeOverlay->raise();

    stack->setCurrentIndex(idx);
    QWidget* newWidget = stack->widget(idx);
    auto* incomingEffect = new QGraphicsOpacityEffect(newWidget);
    incomingEffect->setOpacity(0.0);
    newWidget->setGraphicsEffect(incomingEffect);

    const int kDurationMs = 180; // within the requested 150–250ms window

    auto* fadeOut = new QPropertyAnimation(m_tabOverlayEffect, "opacity");
    fadeOut->setDuration(kDurationMs);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::OutCubic);

    auto* fadeIn = new QPropertyAnimation(incomingEffect, "opacity");
    fadeIn->setDuration(kDurationMs);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);

    m_tabAnim = new QParallelAnimationGroup(this);
    m_tabAnim->addAnimation(fadeOut);
    m_tabAnim->addAnimation(fadeIn);

    connect(m_tabAnim, &QParallelAnimationGroup::finished, this, [this, newWidget]{
        // Drop the opacity effects once settled — leaving them attached
        // would add a compositing pass to every future repaint for no
        // visual benefit at full opacity.
        if (newWidget) newWidget->setGraphicsEffect(nullptr);
        if (m_tabFadeOverlay) {
            m_tabFadeOverlay->deleteLater();
            m_tabFadeOverlay = nullptr;
        }
        if (m_tabAnim) {
            m_tabAnim->deleteLater();
            m_tabAnim = nullptr;
        }
    });
    m_tabAnim->start();
}

// ──────────────────────────────────────────────────────────────────────────────
// Frameless window drag
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && e->pos().y() <= 46) {
        m_dragging  = true;
        m_dragStart = e->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging && (e->buttons() & Qt::LeftButton))
        move(e->globalPosition().toPoint() - m_dragStart);
}

void MainWindow::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Rounded window background
//
// With WA_TranslucentBackground set, the OS surface is fully transparent by
// default, so we paint our own anti-aliased rounded-rect fill + hairline
// border here. The central widget and the title/status bars are all set to
// transparent/matching corner radii so this shows through seamlessly at the
// four corners instead of a hard rectangular edge.
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kCornerRadius, kCornerRadius);

    p.fillPath(path, QColor(0x00,0x00,0x00));
    p.setPen(QPen(QColor(0x2a,0x2a,0x2a), 1));
    p.drawPath(path);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    stopCapture();
    // Persist current device selection before closing
    persistDeviceSelection();
    e->accept();
}
