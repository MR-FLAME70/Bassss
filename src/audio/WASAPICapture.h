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
    // Hard safety ceiling only — NOT the steady-state latency. Real-world
    // latency is actively pinned to kTargetLatencyFrames by trimToTargetLatency()
    // below. This cap just bounds worst-case memory/drop behavior if the
    // consumer stalls completely (e.g. device glitch).
    static constexpr int kCapacity = 16384; // frames — ~340 ms @ 48 kHz
    static constexpr int kMask     = kCapacity - 1;

    // ── Active latency control ────────────────────────────────────────────
    // The capture thread (WASAPI event period, ~10 ms bursts) and the output
    // thread (PortAudio callback, its own device clock) are two independently
    // clocked producer/consumer threads. Nothing about that pairing bounds
    // *queued* latency on its own: a one-time startup gap (WASAPI already
    // pushing frames while the PortAudio output device is still opening) or
    // slow sample-rate drift between the two clocks both accumulate backlog
    // in the ring buffer, and previously nothing ever drained it back down
    // — it would just sit whatever the accumulated depth was for the life of
    // the stream (up to the old 680 ms overflow cap). That is the "hear the
    // dry signal, then hear the wet signal 1-2s later" bug: it is a queued
    // buffer/latency problem, not a DSP or reverb-decay problem.
    //
    // Fix: the consumer actively pins the queue depth to a small target.
    // kTargetLatencyFrames is the latency we settle back to after a trim.
    // kMaxLatencyFrames is the backlog threshold that triggers a trim.
    // Call trimToTargetLatency() once per output callback (cheap, O(1)).
    static constexpr int kTargetLatencyFrames = 480;  // ~10 ms @ 48 kHz
    static constexpr int kMaxLatencyFrames    = 1920; // ~40 ms @ 48 kHz

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

    // Called from the consumer (PortAudio callback) thread, once per block.
    // If the queued backlog exceeds kMaxLatencyFrames — from an initial
    // startup gap or accumulated clock drift — fast-forward the read
    // pointer to bring queued latency straight back down to
    // kTargetLatencyFrames. This is a single, rare, deliberate skip (a
    // few ms of audio dropped in one shot), which is inaudible, versus the
    // previous behavior of silently carrying a growing, unbounded delay for
    // the entire session. This is what actually keeps dry and wet
    // perceptually locked together instead of drifting apart over time.
    void trimToTargetLatency() {
        int w  = writeIdx.load(std::memory_order_acquire);
        int rd = readIdx.load(std::memory_order_relaxed);
        int queued = w - rd;
        if (queued > kMaxLatencyFrames) {
            readIdx.store(w - kTargetLatencyFrames, std::memory_order_release);
        }
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
