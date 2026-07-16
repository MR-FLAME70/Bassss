#pragma once
#include <QMainWindow>
#include "ui/PillTabBar.h"
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
class QGraphicsOpacityEffect;
class QParallelAnimationGroup;

// ──────────────────────────────────────────────────────────────────────────────
// MainWindow — 860×600 fixed-size frameless dark window.
//
// Audio routing:
//   The user explicitly chooses what gets processed via the "Audio Source"
//   selector:
//     • Microphone        — direct mic capture only
//     • Playback Device   — WASAPI loopback (captures what's playing)
//     • Mic + Speaker     — both simultaneously, mixed before DSP
//   Nothing is captured until the user picks a source and presses Start.
// ──────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void playIntroAnimation();

protected:
    void closeEvent(QCloseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void paintEvent(QPaintEvent*) override;

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

    static constexpr int kCornerRadius = 12;
    static constexpr int kWindowWidth  = 860;
    static constexpr int kWindowHeight = 600;

    // Tab-switch crossfade
    QLabel*                  m_tabFadeOverlay    = nullptr;
    QGraphicsOpacityEffect*  m_tabOverlayEffect  = nullptr;
    QParallelAnimationGroup* m_tabAnim           = nullptr;

    // UI
    QWidget*        m_titleBar;
    PillTabBar*     m_tabBar;
    QWidget*        m_stack;
    QPushButton*    m_btnStart;

    // Audio Source selector + per-mode device groups.
    // In "Mic + Speaker" (both) mode both mic and playback groups are shown.
    QComboBox*  m_comboSource;

    // Mic device group (shown in mic-only and both modes)
    QWidget*    m_micGroup;
    QLabel*     m_micGroupLabel;
    QComboBox*  m_comboMic;

    // Playback device group (shown in playback-only and both modes)
    QWidget*    m_playbackGroup;
    QLabel*     m_playbackGroupLabel;
    QComboBox*  m_comboPlayback;

    QComboBox*  m_comboOutput;

    LiveTab*          m_liveTab;
    AdvancedTab*      m_advTab;
    AdvancedAudioTab* m_advAudioTab;

    void buildUI();
    QWidget* buildTitleBar();
    QWidget* buildTabBar();
    void     applyDarkTheme();
    void     populateDeviceDropdowns();
    void     persistDeviceSelection();
    void     startCapture();
    void     stopCapture();
    void     showTab(int idx);
    void     updateDeviceGroupVisibility();

    // Helpers to read current device selection
    // audioSourceMode() returns "microphone", "playback", or "both"
    QString         audioSourceMode() const;
    std::string     selectedInputId()   const;   // primary: loopback or mic
    AudioDeviceType selectedInputType() const;
    std::string     selectedOutputId()  const;
    std::string     selectedMicId()     const;   // mic ID for "both" mode (empty otherwise)
};
