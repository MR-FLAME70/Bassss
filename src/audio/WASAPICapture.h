#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// WASAPICapture — Windows-native Core Audio capture.
//
// On Windows, PortAudio's WASAPI extension header (pa_win_wasapi.h) is not
// available in the vcpkg-pinned port, so we implement device enumeration and
// loopback capture ourselves using the Windows MMDevice / WASAPI APIs directly.
//
// Responsibilities:
//   • Enumerate all render (playback) and capture (microphone) endpoints via
//     IMMDeviceEnumerator — so the user can select Speakers, Headphones,
//     VoiceMeeter Input, VB-Cable, any mic, etc.
//   • Capture loopback audio from a render device with
//     AUDCLNT_STREAMFLAGS_LOOPBACK (hears everything playing through it).
//   • Capture direct audio from a capture device (microphone).
//   • Convert the endpoint's native mix format to float32 stereo and push each
//     frame to AudioProcessor::processStereo().
//   • Feed processed output into a lock-free ring buffer consumed by a
//     PortAudio output-only callback.
//
// Non-Windows builds compile to an empty stub — PortAudio fallback is used.
// ──────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QObject>
#include <QThread>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <array>

class AudioProcessor;

// ── Device type ───────────────────────────────────────────────────────────────
enum class AudioDeviceType {
    Loopback,   // Render endpoint — use WASAPI loopback (captures playback)
    Microphone  // Capture endpoint — use WASAPI directly
};

// ── Device descriptor ─────────────────────────────────────────────────────────
struct AudioDeviceInfo {
    std::string     id;            // Stable Windows device ID (IMMDevice::GetId)
    std::string     name;          // Friendly display name
    AudioDeviceType type;
    double          defaultSampleRate = 48000.0;
    int             channels          = 2;
};

// ── Simple lock-free ring buffer (power-of-two size, float stereo frames) ────
//
// Written by producer (capture thread) and consumed by reader (PortAudio cb).
// Uses two independent monotonic indices; safe for single-producer /
// single-consumer without locks.
//
class StereoRingBuffer {
public:
    static constexpr int kCapacity = 16384; // frames — ~340 ms @ 48 kHz
    static constexpr int kMask     = kCapacity - 1;

    void reset() {
        writeIdx.store(0, std::memory_order_relaxed);
        readIdx .store(0, std::memory_order_relaxed);
    }

    // Called from capture thread
    void push(float l, float r) {
        int w = writeIdx.load(std::memory_order_relaxed);
        left [w & kMask] = l;
        right[w & kMask] = r;
        writeIdx.store(w + 1, std::memory_order_release);
    }

    // Called from PortAudio callback
    bool pop(float& l, float& r) {
        int rIdx = readIdx.load(std::memory_order_relaxed);
        int wIdx = writeIdx.load(std::memory_order_acquire);
        if (rIdx == wIdx) return false; // empty
        l = left [rIdx & kMask];
        r = right[rIdx & kMask];
        readIdx.store(rIdx + 1, std::memory_order_release);
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
class WASAPICapture : public QObject {
    Q_OBJECT
public:
    explicit WASAPICapture(AudioProcessor* proc,
                           StereoRingBuffer& ringBuf,
                           QObject* parent = nullptr);
    ~WASAPICapture();

    // Enumerate all usable input sources (render loopback + capture mics)
    static std::vector<AudioDeviceInfo> enumerateInputSources();

    // Open capture from a specific device.  deviceId="" → default device of
    // given type.  sampleRate is advisory; actual rate is reported via
    // actualSampleRate() after open().
    bool open(const std::string& deviceId, AudioDeviceType type,
              double requestedSampleRate, QString& errorOut);
    void close();

    bool   isOpen()            const { return m_running.load(); }
    double actualSampleRate()  const { return m_actualRate; }

signals:
    void errorOccurred(const QString& msg);

private:
    AudioProcessor*   m_proc;
    StereoRingBuffer& m_ring;

    std::atomic<bool> m_running{false};
    double            m_actualRate = 48000.0;

    // Capture thread (owns all COM objects)
    class CaptureWorker;
    QThread*        m_thread  = nullptr;
    CaptureWorker*  m_worker  = nullptr;
};
