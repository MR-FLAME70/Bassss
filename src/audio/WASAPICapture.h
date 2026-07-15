#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// WASAPICapture — Windows-native Core Audio capture.
//
// Architecture (fixed):
//   CaptureWorker thread  → converts WASAPI frames to float → push to ring
//   PortAudio outCallback → pop raw frames → run DSP → write to output
//
// The DSP must NOT run inside the CaptureWorker thread.  WASAPI delivers data
// in bursts (one event every ~10ms, ~480 frames at 48 kHz); running the full
// DSP chain (FDN reverb, FIR convolution, EQ, …) inside the burst handler
// causes the thread to miss subsequent WASAPI events, producing dropped frames
// and audible glitches.  The PortAudio audio thread is the correct place for
// real-time DSP — it has proper OS scheduler priority and fires at a fixed,
// predictable period.
// ──────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QObject>
#include <QThread>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <array>
#include "RingBuffer.h"

// ── Device type ───────────────────────────────────────────────────────────────
enum class AudioDeviceType {
    Loopback,   // Render endpoint — use WASAPI loopback (captures playback)
    Microphone  // Capture endpoint — use WASAPI directly
};

// ── Device descriptor ─────────────────────────────────────────────────────────
struct AudioDeviceInfo {
    std::string     id;
    std::string     name;
    AudioDeviceType type;
    double          defaultSampleRate = 48000.0;
    int             channels          = 2;
};

// StereoRingBuffer (lock-free SPSC ring buffer) now lives in RingBuffer.h,
// shared with AudioProcessor's WAV recorder. See that header for details.


// ── WASAPICapture ─────────────────────────────────────────────────────────────
// Note: no longer takes an AudioProcessor*. DSP is the caller's responsibility.
class WASAPICapture : public QObject {
    Q_OBJECT
public:
    explicit WASAPICapture(StereoRingBuffer& ringBuf, QObject* parent = nullptr);
    ~WASAPICapture();

    static std::vector<AudioDeviceInfo> enumerateInputSources();

    bool open(const std::string& deviceId, AudioDeviceType type,
              double requestedSampleRate, QString& errorOut);
    void close();

    bool   isOpen()           const { return m_running.load(); }
    double actualSampleRate() const { return m_actualRate; }

signals:
    void errorOccurred(const QString& msg);

private:
    StereoRingBuffer& m_ring;
    std::atomic<bool> m_running{false};
    double            m_actualRate = 48000.0;

    class CaptureWorker;
    QThread*        m_thread = nullptr;
    CaptureWorker*  m_worker = nullptr;
};
