#pragma once
#include <portaudio.h>
#include <QString>
#include <QObject>
#include <QTimer>
#include <string>
#include <vector>
#include <atomic>
#include "WASAPICapture.h"   // AudioDeviceInfo, AudioDeviceType, StereoRingBuffer

class AudioProcessor;

// ──────────────────────────────────────────────────────────────────────────────
// AudioCapture — manages the full capture → process → playback pipeline.
//
// On Windows the input side is always driven by WASAPICapture:
//   • Render (playback) device  → WASAPI loopback (AUDCLNT_STREAMFLAGS_LOOPBACK)
//   • Capture (mic) device      → WASAPI direct capture
// This completely avoids the broken pa_win_wasapi.h dependency.
//
// The output side uses a PortAudio output-only stream that reads from a
// StereoRingBuffer filled by the WASAPI capture thread.
//
// "Both" mode: opens a WASAPI loopback AND a WASAPI mic simultaneously,
// each into its own ring buffer.  The outCallback pops from both rings
// and sums the frames before passing them to the DSP chain.
//
// On non-Windows, both input and output fall back to a standard PortAudio
// full-duplex stream.
// ──────────────────────────────────────────────────────────────────────────────
class AudioCapture : public QObject {
    Q_OBJECT
public:
    // Output device descriptor (always a PortAudio render device)
    struct OutputDeviceInfo {
        int         paIndex;
        std::string id;    // "<paIndex>:<name>" used as stable key
        std::string name;
        double      defaultSampleRate;
    };

    explicit AudioCapture(AudioProcessor* proc, QObject* parent = nullptr);
    ~AudioCapture();

    // ── Enumeration ──────────────────────────────────────────────────────────
    // Input sources: on Windows returns WASAPI endpoints (render + capture).
    // On non-Windows returns PortAudio input devices.
    static std::vector<AudioDeviceInfo> enumerateInputSources();

    // Output devices: always PortAudio output devices.
    static std::vector<OutputDeviceInfo> enumerateOutputDevices();

    // ── Open / close ─────────────────────────────────────────────────────────
    // inputDeviceId  — stable device ID string for the primary source
    // inputType      — Loopback or Microphone (primary source type)
    // outputDeviceId — output device id string (empty = default output)
    // sampleRate     — advisory; actual rate reported after open
    // bufferSize     — PortAudio frames per buffer
    // micDeviceId    — non-empty only in "both" mode: secondary mic device ID.
    //                  When set, the loopback (inputDeviceId) is opened as
    //                  primary and this mic is opened simultaneously; frames
    //                  from both are summed before DSP.
    bool open(const std::string& inputDeviceId,
              AudioDeviceType   inputType,
              const std::string& outputDeviceId,
              double sampleRate,
              int    bufferSize,
              QString& errorOut,
              const std::string& micDeviceId = "");
    void close();

    bool   isOpen()           const { return m_open.load(); }
    double actualSampleRate() const { return m_sampleRate; }

    // Drain the diagnostic event ring to the log file (UI/timer thread only).
    // Called automatically every second via an internal QTimer; also callable
    // manually to get an immediate snapshot.
    void flushDiagnostics();

    // Absolute path to the diagnostic CSV log file.
    static std::string diagLogPath();

signals:
    void errorOccurred(const QString& msg);

private:
    AudioProcessor*   m_proc;
    std::atomic<bool> m_open{false};
    double            m_sampleRate = 48000.0;

#ifdef _WIN32
    // Windows path: WASAPI capture + PortAudio output-only
    // Primary source (loopback or mic-only).
    WASAPICapture*   m_wasapi    = nullptr;
    StereoRingBuffer m_ring;

    // Secondary mic capture — only allocated/active in "both" mode.
    WASAPICapture*   m_wasapiMic = nullptr;
    StereoRingBuffer m_ring2;
    std::atomic<bool> m_mixMode{false};  // true when both loopback+mic active

    PaStream*        m_outStream = nullptr;
    QTimer*          m_diagTimer = nullptr;  // flushes diagnostics every 1 s

    bool openOutputOnly(const std::string& outputDeviceId,
                        double sampleRate, int bufferSize, QString& err);
    static int outCallback(const void*, void* out,
                           unsigned long frames,
                           const PaStreamCallbackTimeInfo*,
                           PaStreamCallbackFlags statusFlags, void* userData);
#else
    // Non-Windows: standard PortAudio full-duplex
    PaStream* m_stream = nullptr;
    static int paCallback(const void* in, void* out,
                          unsigned long frames,
                          const PaStreamCallbackTimeInfo*,
                          PaStreamCallbackFlags, void* userData);
#endif
};
