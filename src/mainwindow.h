#pragma once
#include <QMainWindow>
#include <QTabBar>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <memory>
#include <vector>
#include "settings.h"
#include "audio/AudioProcessor.h"
#include "audio/AudioCapture.h"
#include "audio/WASAPICapture.h"  // AudioDeviceInfo, AudioDeviceType

class LiveTab;
class FileTab;
class AdvancedTab;
class AdvancedAudioTab;

// ──────────────────────────────────────────────────────────────────────────────
// MainWindow — 800×600 fixed-size frameless dark window matching popup.html.
//
// Layout:
//   [custom title bar: logo | "Bass Nuker" | Input Device | Output Device |
//                      start/stop | × −]
//   [tab bar: Live Tab Audio | File Export | Advanced | Advanced Audio]
//   [tab content area — 4 tab panels]
//   [status bar: status | sample rate | version]
// ──────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private slots:
    void onTabChanged(int idx);
    void onStartStop();
    void onInputDeviceChanged(int idx);
    void onOutputDeviceChanged(int idx);
    void onSettingsChanged(const AppSettings& s);
    void onAudioError(const QString& msg);

private:
    // DSP + capture
    AudioProcessor* m_proc;
    AudioCapture*   m_capture;
    bool            m_running = false;

    // Cached device lists (populated once at startup; refreshable)
    std::vector<AudioDeviceInfo>           m_inputSources;
    std::vector<AudioCapture::OutputDeviceInfo> m_outputDevices;

    // Title bar drag
    QPoint m_dragStart;
    bool   m_dragging = false;

    // UI
    QWidget*       m_titleBar;
    QTabBar*       m_tabBar;
    QWidget*       m_stack;
    QWidget*       m_statusBar;
    QLabel*        m_statusLabel;
    QLabel*        m_deviceLabel;
    QPushButton*   m_btnStart;
    QComboBox*     m_comboInput;
    QComboBox*     m_comboOutput;

    LiveTab*          m_liveTab;
    FileTab*          m_fileTab;
    AdvancedTab*      m_advTab;
    AdvancedAudioTab* m_advAudioTab;

    void buildUI();
    QWidget* buildTitleBar();
    QWidget* buildTabBar();
    QWidget* buildStatusBar();
    void     applyDarkTheme();
    void     populateDeviceDropdowns();
    void     startCapture();
    void     stopCapture();
    void     showTab(int idx);

    // Helpers to read current device selection
    std::string     selectedInputId()   const;
    AudioDeviceType selectedInputType() const;
    std::string     selectedOutputId()  const;
};
