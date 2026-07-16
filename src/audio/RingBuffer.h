#pragma once
#include <atomic>

// ──────────────────────────────────────────────────────────────────────────────
// StereoRingBuffer — lock-free SPSC (single-producer/single-consumer) ring
// buffer for interleaved-free stereo float frames.
//
// Fixed capacity, no heap allocation after construction — safe to use from a
// real-time audio thread as either producer or consumer. If full, push()
// drops the incoming frame rather than blocking or overwriting unread data.
//
// Shared by:
//   • WASAPICapture / AudioCapture — bridges the capture thread and the
//     PortAudio output thread (see trimToTargetLatency() for why active
//     latency control matters there).
//   • AudioProcessor's WAV recorder — bridges the audio thread (producer,
//     pushes processed frames while recording) and a dedicated low-priority
//     writer thread (consumer, streams frames to disk) so recording never
//     takes a lock or grows an unbounded in-memory buffer on the audio
//     thread.
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
    // Why 120 ms target / 240 ms max:
    //   Windows shared-mode WASAPI runs the capture thread at normal OS
    //   priority, subject to scheduling jitter of up to 50-100 ms under load.
    //   A 10-40 ms pre-buffer (the former values) drains completely when the
    //   OS delays the capture thread, causing the outCallback to output
    //   silence — heard as a periodic "freeze" or dropout. 120 ms of pre-
    //   buffer absorbs worst-case Windows scheduling jitter without
    //   underrunning. The trim ceiling (240 ms) keeps steady-state latency
    //   bounded. For a DSP enhancement tool (not a real-time monitor), 120 ms
    //   end-to-end latency is imperceptible to the user.
    static constexpr int kTargetLatencyFrames = 5760;  // ~120 ms @ 48 kHz
    static constexpr int kMaxLatencyFrames    = 11520; // ~240 ms @ 48 kHz

    void reset() {
        writeIdx.store(0, std::memory_order_relaxed);
        readIdx .store(0, std::memory_order_relaxed);
    }

    // Called from the producer thread.
    // Drops the frame silently if the buffer is full (consumer is stalling).
    void push(float l, float r) {
        int w = writeIdx.load(std::memory_order_relaxed);
        int rd = readIdx.load(std::memory_order_acquire);
        if ((w - rd) >= kCapacity) return; // full — drop rather than overwrite
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
    // If the queued backlog exceeds kMaxLatencyFrames, fast-forward the read
    // pointer to bring queued latency straight back down to
    // kTargetLatencyFrames. A single, rare, deliberate skip (a few ms of
    // audio dropped in one shot, inaudible) versus silently carrying a
    // growing, unbounded delay for the entire session.
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
