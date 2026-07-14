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

// ── Lock-free ring buffer — raw float stereo frames ──────────────────────────
//
// Producer: CaptureWorker pushes raw (unprocessed) frames.
// Consumer: PortAudio outCallback pops and runs DSP before output.
//
// SPSC (single-producer / single-consumer), no locks.
// If full, push() drops the incoming frame rather than overwriting unread data.
//
class StereoRingBuffer {
public:
    static constexpr int kCapacity = 32768; // frames — ~680 ms @ 48 kHz
    static constexpr int kMask     = kCapacity - 1;

    void reset() {
        writeIdx.store(0, std::memory_order_relaxed);
        readIdx .store(0, std::memory_order_relaxed);
    }

    // Called from capture thread.
    // Drops the frame silently if the buffer is full (output is stalling).
    void push(float l, float r) {
        int w = writeIdx.load(std::memory_order_relaxed);
        int rd = readIdx.load(std::memory_order_acquire);
        if ((w - rd) >= kCapacity) return; // full — drop rather than overwrite
        left [w & kMask] = l;
        right[w & kMask] = r;
        writeIdx.store(w + 1, std::memory_order_release);
    }

    // Called from PortAudio callback.
    // Returns true if a frame was available; l/r=0 on underrun.
    bool pop(float& l, float& r) {
        int rd = readIdx.load(std::memory_order_relaxed);
        int w  = writeIdx.load(std::memory_order_acquire);
        if (rd == w) { l = r = 0.f; return false; } // empty
        l = left [rd & kMask];
        r = right[rd & kMask];
        readIdx.store(rd + 1, std::memory_order_release);
        return true;
    }

    int available() const {
        return writeIdx.load(std::memory_order_acquire)
             - readIdx .load(std::memory_order_relaxed);
    }

private:
    float left [kCapacity] = {};
    float right[kCapacity] = {};
    std::atomic<int> writeIdx{0};
    std::atomic<int> readIdx {0};
};


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
