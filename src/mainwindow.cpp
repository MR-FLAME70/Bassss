#include "mainwindow.h"
#include "ui/LiveTab.h"
#include "ui/FileTab.h"
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

// ── Data roles for device combo boxes ─────────────────────────────────────────
// UserRole+0 = device ID string (QString)
// UserRole+1 = device type string: "loopback" | "microphone"  (input only)
static const int kRoleDeviceId   = Qt::UserRole + 0;
static const int kRoleDeviceType = Qt::UserRole + 1;

// ──────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    globalSettings().load();

    setFixedSize(860, 600);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);

    m_proc    = new AudioProcessor();
    m_capture = new AudioCapture(m_proc, this);

    connect(m_capture, &AudioCapture::errorOccurred,
            this,      &MainWindow::onAudioError);

    applyDarkTheme();
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
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    // 1. Custom title bar
    root->addWidget(buildTitleBar());

    // 2. Tab bar
    m_tabBar = new QTabBar();
    m_tabBar->addTab("🎵 Live Tab Audio");
    m_tabBar->addTab("📁 File Export");
    m_tabBar->addTab("⚙ Advanced");
    m_tabBar->addTab("🎚 Advanced Audio");
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
    m_fileTab      = new FileTab(m_proc);
    m_advTab       = new AdvancedTab();
    m_advAudioTab  = new AdvancedAudioTab();
    stack->addWidget(m_liveTab);
    stack->addWidget(m_fileTab);
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
    m_fileTab->refreshFromSettings(s);
    m_advTab->refreshFromSettings(s);
    m_advAudioTab->refreshFromSettings(s);
}

// ──────────────────────────────────────────────────────────────────────────────
// Title bar
// ──────────────────────────────────────────────────────────────────────────────
QWidget* MainWindow::buildTitleBar() {
    m_titleBar = new QWidget();
    m_titleBar->setFixedHeight(46);
    m_titleBar->setStyleSheet("background: #0a0a0a; border-bottom: 1px solid #1a1a1a;");

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

    // ── Input Device ─────────────────────────────────────────────────────────
    // Shows both render (playback) endpoints for loopback capture and
    // capture (microphone) endpoints for direct mic input.
    auto* inGroup = new QWidget();
    inGroup->setStyleSheet("background:transparent;");
    auto* inLay = new QVBoxLayout(inGroup);
    inLay->setContentsMargins(0,0,0,0);
    inLay->setSpacing(1);

    auto* inLabel = new QLabel("Input Source");
    inLabel->setStyleSheet("color:#555; font-size:9px; letter-spacing:0.5px;");
    inLay->addWidget(inLabel);

    m_comboInput = new QComboBox();
    m_comboInput->setFixedWidth(185);
    m_comboInput->setFixedHeight(26);
    m_comboInput->setStyleSheet(
        "QComboBox { background:#131313; color:#ccc; border:1px solid #262626; border-radius:5px;"
        "padding:2px 8px; font-size:11px; }"
        "QComboBox:hover { border:1px solid #3a3a3a; }"
        "QComboBox::drop-down { border:none; width:18px; }"
        "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; border:1px solid #333;"
        "selection-background-color:#333333; font-size:11px; outline:none; }");
    inLay->addWidget(m_comboInput);
    lay->addWidget(inGroup);

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
    m_comboOutput->setFixedWidth(185);
    m_comboOutput->setFixedHeight(26);
    m_comboOutput->setStyleSheet(m_comboInput->styleSheet());
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
    // These were previously unconnected; now properly wired so changing the
    // combo while running restarts capture with the new device immediately.
    connect(m_comboInput,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInputDeviceChanged);
    connect(m_comboOutput, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOutputDeviceChanged);

    return m_titleBar;
}

// ──────────────────────────────────────────────────────────────────────────────
// Status bar
// ──────────────────────────────────────────────────────────────────────────────
QWidget* MainWindow::buildStatusBar() {
    auto* bar = new QWidget();
    bar->setFixedHeight(22);
    bar->setStyleSheet("background: #0a0a0a; border-top: 1px solid #1a1a1a;");
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
    // Block signals while we populate so we don't trigger onDeviceChanged
    m_comboInput ->blockSignals(true);
    m_comboOutput->blockSignals(true);

    m_comboInput ->clear();
    m_comboOutput->clear();

    const auto& s = globalSettings();

    // ── Input: enumerate all WASAPI endpoints (render loopback + mics) ────────
    m_inputSources = AudioCapture::enumerateInputSources();

    // "Default" entries first
    m_comboInput->addItem("🔊 Default Playback (Loopback)", QVariant());
    m_comboInput->setItemData(0, QString(""), kRoleDeviceId);
    m_comboInput->setItemData(0, QString("loopback"), kRoleDeviceType);

    int savedInputIdx = 0; // will point to the "default" if nothing matches

    for (int i = 0; i < (int)m_inputSources.size(); ++i) {
        const auto& d = m_inputSources[i];
        QString prefix;
        if (d.type == AudioDeviceType::Loopback)
            prefix = "🔊 "; // playback / loopback
        else
            prefix = "🎤 "; // microphone
        QString label = prefix + QString::fromStdString(d.name);
        m_comboInput->addItem(label, QVariant());

        int comboIdx = i + 1; // +1 for the "Default" entry
        QString qid  = QString::fromStdString(d.id);
        QString qtype = (d.type == AudioDeviceType::Loopback) ? "loopback" : "microphone";
        m_comboInput->setItemData(comboIdx, qid,   kRoleDeviceId);
        m_comboInput->setItemData(comboIdx, qtype, kRoleDeviceType);

        // Restore selection by saved device ID
        if (!s.inputDeviceId.isEmpty() && qid == s.inputDeviceId)
            savedInputIdx = comboIdx;
    }
    m_comboInput->setCurrentIndex(savedInputIdx);

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

    m_comboInput ->blockSignals(false);
    m_comboOutput->blockSignals(false);
}

// ──────────────────────────────────────────────────────────────────────────────
// Helpers to read current combo selection
// ──────────────────────────────────────────────────────────────────────────────
std::string MainWindow::selectedInputId() const {
    return m_comboInput->currentData(kRoleDeviceId).toString().toStdString();
}

AudioDeviceType MainWindow::selectedInputType() const {
    QString t = m_comboInput->currentData(kRoleDeviceType).toString();
    return (t == "microphone") ? AudioDeviceType::Microphone
                               : AudioDeviceType::Loopback;
}

std::string MainWindow::selectedOutputId() const {
    return m_comboOutput->currentData(kRoleDeviceId).toString().toStdString();
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
    s.inputDeviceId   = QString::fromStdString(selectedInputId());
    s.inputDeviceType = (selectedInputType() == AudioDeviceType::Loopback)
                            ? "loopback" : "microphone";
    s.outputDeviceId  = QString::fromStdString(selectedOutputId());
    s.save();

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
// Device changed (combo box)
// ──────────────────────────────────────────────────────────────────────────────
void MainWindow::onInputDeviceChanged(int /*idx*/) {
    if (m_running) {
        stopCapture();
        startCapture();
    }
}

void MainWindow::onOutputDeviceChanged(int /*idx*/) {
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
    m_fileTab->refreshFromSettings(s);
    m_advTab->refreshFromSettings(s);
    m_advAudioTab->refreshFromSettings(s);
}

void MainWindow::onAudioError(const QString& msg) {
    QMessageBox::warning(this, "Audio Error", msg);
    stopCapture();
}

void MainWindow::onTabChanged(int idx) {
    static_cast<QStackedWidget*>(m_stack)->setCurrentIndex(idx);
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

void MainWindow::closeEvent(QCloseEvent* e) {
    stopCapture();
    // Persist current device selection before closing
    auto& s = globalSettings();
    s.inputDeviceId   = QString::fromStdString(selectedInputId());
    s.inputDeviceType = (selectedInputType() == AudioDeviceType::Loopback)
                            ? "loopback" : "microphone";
    s.outputDeviceId  = QString::fromStdString(selectedOutputId());
    s.save();
    e->accept();
}
