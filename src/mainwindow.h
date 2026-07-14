#pragma once
#include <QMainWindow>
#include <QTabBar>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QStackedWidget>
#include <QTimer>
#include <memory>
#include <vector>
#include "settings.h"
#include "audio/AudioProcessor.h"
#include "audio/AudioCapture.h"
#include "audio/WASAPICapture.h"  // AudioDeviceInfo, AudioDeviceType

class LiveTab;
class AdvancedTab;
class AdvancedAudioTab;

// ──────────────────────────────────────────────────────────────────────────────
// MainWindow — 800×600 fixed-size frameless dark window matching popup.html.
//
// Layout:
//   [custom title bar: logo | "Bass Nuker" | Audio Source | Playback/Mic Device
//                      (whichever is active) | Output Device | start/stop | × −]
//   [tab bar: Live Tab Audio | Advanced | Advanced Audio]
//   [tab content area — 4 tab panels]
//   [status bar: status | sample rate | version]
//
// Audio routing:
//   The user explicitly chooses what gets processed via the "Audio Source"
//   selector — Microphone or Playback Device. Nothing is captured until the
//   user picks a source and presses Start; the microphone is NEVER opened
//   automatically. Each source keeps its own remembered device selection
//   (playbackDeviceId / micDeviceId in AppSettings) so switching back and
//   forth restores the last device chosen for that source, across restarts.
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
    void onAudioSourceChanged(int idx);
    void onPlaybackDeviceChanged(int idx);
    void onMicDeviceChanged(int idx);
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
    QWidget*        m_titleBar;
    QTabBar*        m_tabBar;
    QWidget*        m_stack;
    QWidget*        m_statusBar;
    QLabel*         m_statusLabel;
    QLabel*         m_deviceLabel;
    QPushButton*    m_btnStart;

    // Audio Source (Microphone / Playback Device) + its two independent
    // device selectors. Only one of m_comboMic / m_comboPlayback is visible
    // at a time, swapped via m_deviceStack based on m_comboSource.
    QComboBox*       m_comboSource;
    QStackedWidget*  m_deviceStack;
    QComboBox*       m_comboMic;
    QComboBox*       m_comboPlayback;
    QLabel*          m_deviceGroupLabel;
    QComboBox*       m_comboOutput;

    LiveTab*          m_liveTab;
    AdvancedTab*      m_advTab;
    AdvancedAudioTab* m_advAudioTab;

    void buildUI();
    QWidget* buildTitleBar();
    QWidget* buildTabBar();
    QWidget* buildStatusBar();
    void     applyDarkTheme();
    void     populateDeviceDropdowns();
    void     persistDeviceSelection();
    void     startCapture();
    void     stopCapture();
    void     showTab(int idx);

    // Helpers to read current device selection
    bool            sourceIsMicrophone() const;
    std::string     selectedInputId()   const;
    AudioDeviceType selectedInputType() const;
    std::string     selectedOutputId()  const;
};
