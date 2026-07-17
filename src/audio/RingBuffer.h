#pragma once
#include <algorithm>
#include <atomic>

// ──────────────────────────────────────────────────────────────────────────────
// StereoRingBuffer — lock-free SPSC (single-producer/single-consumer) ring
// buffer for interleaved-free stereo float frames.
//
// Fixed capacity, no heap allocation after construction — safe to use from a
// real-time audio thread as either producer or consumer. If full, push()
// drops the incoming frame rather than blocking or overwriting unread data.
// ──────────────────────────────────────────────────────────────────────────────
class StereoRingBuffer {
public:
    // Hard safety ceiling only — NOT the steady-state latency for the
    // capture/output use case. See trimToTargetLatency().
    static constexpr int kCapacity = 16384; // frames — ~340 ms @ 48 kHz
    static constexpr int kMask     = kCapacity - 1;

    // ── Active latency control (capture/output use case) ────────────────────
    // The capture thread (WASAPI event period, ~10 ms bursts) and the output
    // thread (PortAudio callback, its own device clock) are two independently
    // clocked producer/consumer threads. Nothing about that pairing bounds
    // *queued* latency on its own: a one-time startup gap or slow sample-rate
    // drift between the two clocks accumulates backlog that never drains on
    // its own. kTargetLatencyFrames is the latency we settle back to after a
    // trim; kMaxLatencyFrames is the backlog threshold that triggers one.
    // Call trimToTargetLatency() once per output callback (cheap, O(1)).
    //
    // Why 120 ms target / 200 ms max:
    //   Windows shared-mode WASAPI runs the capture thread at normal OS
    //   priority, subject to scheduling jitter of up to 50-100 ms under load.
    //   A 10-40 ms pre-buffer drains completely when the OS delays the capture
    //   thread, causing the outCallback to output silence — heard as a periodic
    //   "freeze" or dropout. 120 ms of pre-buffer absorbs worst-case Windows
    //   scheduling jitter without underrunning. The trim ceiling (200 ms) keeps
    //   steady-state latency bounded. For a DSP enhancement tool (not a
    //   real-time monitor), 120 ms end-to-end latency is imperceptible.
    static constexpr int kTargetLatencyFrames = 5760;  // ~120 ms @ 48 kHz
    static constexpr int kMaxLatencyFrames    = 9600;  // ~200 ms @ 48 kHz

    // Max frames to skip per outCallback call when trimming.
    // Gradual trim: instead of one large jump (~120 ms at once — clearly
    // audible as a cut), advance the read pointer by at most one small block
    // per callback. At a typical 256-frame callback size this means an excess
    // of e.g. 4000 frames (~83 ms) is corrected over ~16 callbacks (~0.09 s)
    // as an imperceptibly slow speed-up rather than an abrupt skip.
    static constexpr int kTrimStepFrames = 256;

    void reset() {
        writeIdx.store(0, std::memory_order_relaxed);
        readIdx .store(0, std::memory_order_relaxed);
    }

    // Called once from the main thread, right after the startup pre-fill
    // wait and before Pa_StartStream().
    void resetToTargetLatency() {
        int w = writeIdx.load(std::memory_order_acquire);
        int rd = readIdx.load(std::memory_order_relaxed);
        if ((w - rd) > kTargetLatencyFrames) {
            readIdx.store(w - kTargetLatencyFrames, std::memory_order_release);
        }
    }

    // Counts frames silently dropped by push() because the ring was full.
    // Written by the producer thread, read+reset by the consumer thread via
    // exchange(0) in outCallback. Used by AudioDiagnostics to detect overruns.
    std::atomic<uint32_t> pushDropCount{0};

    // Called from the producer thread.
    // Drops the frame and increments pushDropCount if the buffer is full.
    void push(float l, float r) {
        int w = writeIdx.load(std::memory_order_relaxed);
        int rd = readIdx.load(std::memory_order_acquire);
        if ((w - rd) >= kCapacity) {
            pushDropCount.fetch_add(1, std::memory_order_relaxed);
            return; // full — drop rather than overwrite
        }
        left [w & kMask] = l;
        right[w & kMask] = r;
        writeIdx.store(w + 1, std::memory_order_release);
    }

    // Called from the consumer thread.
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

    // Called from the consumer thread, once per block.
    // If queued latency exceeds kMaxLatencyFrames, advance the read pointer
    // by at most kTrimStepFrames per call — a gradual catch-up.
    //
    // Returns the number of frames actually skipped (0 when nothing was trimmed).
    // The caller (outCallback) uses the return value to detect and log trim
    // events, which are a direct source of audible discontinuities (clicks).
    int trimToTargetLatency() {
        int w  = writeIdx.load(std::memory_order_acquire);
        int rd = readIdx.load(std::memory_order_relaxed);
        int queued = w - rd;
        if (queued > kMaxLatencyFrames) {
            int excess = queued - kTargetLatencyFrames;
            int skip   = std::min(excess, kTrimStepFrames);
            readIdx.store(rd + skip, std::memory_order_release);
            return skip;
        }
        return 0;
    }

private:
    float left [kCapacity] = {};
    float right[kCapacity] = {};
    std::atomic<int> writeIdx{0};
    std::atomic<int> readIdx {0};
};
