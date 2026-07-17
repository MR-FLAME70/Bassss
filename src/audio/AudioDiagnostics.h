#pragma once
// ──────────────────────────────────────────────────────────────────────────────
// AudioDiagnostics — lock-free, allocation-free instrumentation for the
// real-time audio pipeline.
//
// AUDIO-THREAD CONTRACT (never violated by any method tagged [RT]):
//   • No mutex / condition variable
//   • No heap allocation (new / delete / malloc / free)
//   • No blocking I/O (file write, socket, console)
//   • No blocking OS call (Sleep, WaitForSingleObject with timeout > 0)
//
// All write paths on the audio thread are:
//   atomic fetch_add / exchange / compare_exchange_weak   — O(1), never blocks
//   memcpy into a pre-allocated ring slot                 — O(1), no syscall
//
// UI / timer thread: call reset() on stream open, flushToLog() ≤ once/second.
// ──────────────────────────────────────────────────────────────────────────────

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <climits>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach/mach_time.h>
#else
#  include <time.h>
#endif

// ── Thread-local sub-timing accumulators ─────────────────────────────────────
// Written by consumePendingSettings() (audio thread), read by outCallback
// (same thread) via AudioDiagnostics::getSettingsUs() / getIrUs().
// inline thread_local: one instance per thread, shared across all TUs (C++17).
namespace AudioDiagnosticsDetail {
    inline thread_local int      g_settingsUs = 0;  // applySettingsInternal time
    inline thread_local int      g_irUs       = 0;  // adoptPendingImpulse time
    inline thread_local uint32_t g_cbSeq      = 0;  // current callback sequence
}

// ──────────────────────────────────────────────────────────────────────────────
class AudioDiagnostics {
public:
    // ── Event types ──────────────────────────────────────────────────────────
    enum EventType : uint8_t {
        EV_CALLBACK_STATS = 1,  // one per callback
        EV_XRUN           = 2,  // PortAudio statusFlags != 0
        EV_TRIM           = 3,  // trimToTargetLatency skipped frames  [CLICK SOURCE]
        EV_UNDERRUN       = 4,  // ring empty during callback (pop miss) [CLICK SOURCE]
        EV_PUSH_DROP      = 5,  // capture thread dropped frames (ring full)
        EV_SETTINGS_APPLY = 6,  // applySettingsInternal ran on audio thread
        EV_IR_ADOPT       = 7,  // adoptPendingImpulse swapped a new IR
        EV_CAPTURE_BURST  = 8,  // WASAPI period late or delivered extra frames
    };

    // 32-byte event — fits in one cache line, memcpy-able without padding issues.
    struct Event {
        int64_t   timestampNs;   //  8 — ns elapsed since stream open
        uint32_t  callbackSeq;   //  4 — monotonic callback counter
        EventType type;          //  1
        uint8_t   _pad[3];       //  3
        int32_t   v0, v1, v2, v3; // 16 — event-specific fields (see below)
        // EV_CALLBACK_STATS : v0=totalUs  v1=dspUs  v2=settingsUs  v3=irUs
        //                     callbackSeq also encodes underruns in high 12 bits:
        //                     low 20 bits = seq, bits 20-31 = underruns (capped at 4095)
        //   ringFill before trim stored in a second CALLBACK_STATS event as v0;
        //   kept separate so we see fill on every callback even when nothing else fired.
        // EV_XRUN           : v0=statusFlags (paOutputUnderflow etc.)
        // EV_TRIM           : v0=framesTrimmed  v1=ringFillBefore
        // EV_UNDERRUN       : v0=frameIdxInCb   v1=ringFill
        // EV_PUSH_DROP      : v0=dropsThisCb    v1=ringFill
        // EV_SETTINGS_APPLY : v0=applyUs        v1=irAdoptUs (0 if no IR pending)
        // EV_IR_ADOPT       : v0=adoptUs        v1=irLenSamples
        // EV_CAPTURE_BURST  : v0=numFrames      v1=periodUs  v2=expectedPeriodUs
    };
    static_assert(sizeof(Event) == 32, "Event size must be 32 bytes");

    static constexpr int kRingCap  = 4096; // must be power-of-2
    static constexpr int kRingMask = kRingCap - 1;

    // ── Singleton ─────────────────────────────────────────────────────────────
    static AudioDiagnostics& instance() {
        static AudioDiagnostics s;
        return s;
    }
    AudioDiagnostics(const AudioDiagnostics&) = delete;
    AudioDiagnostics& operator=(const AudioDiagnostics&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // UI-thread API
    // ─────────────────────────────────────────────────────────────────────────

    // Call on stream open (UI thread, before Pa_StartStream).
    void reset(double sampleRate, int framesPerCallback) {
        sampleRate_    = sampleRate;
        framesPerCb_   = framesPerCallback;
        budgetNs_      = (int64_t)((double)framesPerCallback / sampleRate * 1e9);

        callbackCount.store(0,          std::memory_order_relaxed);
        xrunCount.store(0,              std::memory_order_relaxed);
        underrunCount.store(0,          std::memory_order_relaxed);
        trimCount.store(0,              std::memory_order_relaxed);
        trimFramesTotal.store(0,        std::memory_order_relaxed);
        pushDropCount.store(0,          std::memory_order_relaxed);
        overBudgetCount.store(0,        std::memory_order_relaxed);
        maxCallbackUs.store(0,          std::memory_order_relaxed);
        minRingFill.store(INT_MAX,      std::memory_order_relaxed);
        maxRingFill.store(0,            std::memory_order_relaxed);
        settingsApplyCount.store(0,     std::memory_order_relaxed);
        irAdoptCount.store(0,           std::memory_order_relaxed);

        flushHead_ = 0;
        ringHead_.store(0, std::memory_order_release);
        streamOpenNs_ = nowNs();
    }

    // Drain the event ring and append a CSV log to `path`.
    // Safe to call from any non-audio thread. Call at most once per second.
    void flushToLog(const std::string& path) {
        // Ensure we see all audio-thread writes up to ringHead_.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        uint32_t head = ringHead_.load(std::memory_order_relaxed);
        if (head == flushHead_) return;

        std::ofstream f(path, std::ios::app);
        if (!f.is_open()) return;

        // Write CSV header on first call to this path.
        if (flushHead_ == 0) {
            f << "# Bass Nuker Qt audio diagnostics\n"
              << "# sample_rate=" << sampleRate_
              << " frames_per_cb=" << framesPerCb_
              << " budget_us=" << (budgetNs_ / 1000) << "\n"
              << "timestamp_ns,callback_seq,event_type,v0,v1,v2,v3,description\n";
        }

        while (flushHead_ != head) {
            // Safe to read: audio thread only overwrites old entries once ring
            // wraps (kRingCap / flush_rate_hz >> 1 in practice — never wraps
            // if flushed at least once per (kRingCap / max_events_per_sec) s).
            const Event& ev = ring_[flushHead_ & kRingMask];
            char desc[192] = {};
            buildDesc(ev, desc, sizeof(desc));
            f << ev.timestampNs  << ','
              << (ev.callbackSeq & 0xFFFFF) << ','   // low 20 bits = seq
              << typeName(ev.type) << ','
              << ev.v0 << ',' << ev.v1 << ',' << ev.v2 << ',' << ev.v3 << ','
              << desc << '\n';
            ++flushHead_;
        }

        // Emit a summary line so each flush produces a checkpoint.
        f << "# ---SUMMARY"
          << " elapsed_ms=" << (elapsedNs() / 1000000)
          << " cbs="        << callbackCount.load()
          << " xruns="      << xrunCount.load()
          << " underruns="  << underrunCount.load()
          << " pushDrops="  << pushDropCount.load()
          << " trims="      << trimCount.load()
          << " trimFrames=" << trimFramesTotal.load()
          << " overBudget=" << overBudgetCount.load()
          << " maxCbUs="    << maxCallbackUs.load()
          << " minFill="    << minRingFill.load()
          << " maxFill="    << maxRingFill.load()
          << " settApply="  << settingsApplyCount.load()
          << " irAdopts="   << irAdoptCount.load()
          << " budget_us="  << (budgetNs_ / 1000)
          << '\n';
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Audio-thread API  [RT] — no malloc, no mutex, no I/O
    // ─────────────────────────────────────────────────────────────────────────

    // [RT] Nanoseconds from a monotonic clock. Uses QPC on Windows (single
    // RDTSC-equivalent instruction on modern CPUs — constant, no syscall).
    int64_t nowNs() const {
#if defined(_WIN32)
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        // Multiply before divide to preserve precision with integer arithmetic.
        return (t.QuadPart * 1'000'000'000LL) / freq_;
#elif defined(__APPLE__)
        uint64_t t = mach_absolute_time();
        return (int64_t)(t * tbInfo_.numer / tbInfo_.denom);
#else
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
#endif
    }

    // [RT] Nanoseconds elapsed since reset().
    int64_t elapsedNs() const { return nowNs() - streamOpenNs_; }
    int64_t budgetNs()  const { return budgetNs_; }

    // [RT] Called at start of every callback to reset TL accumulators.
    void beginCallback(uint32_t seq) {
        AudioDiagnosticsDetail::g_settingsUs = 0;
        AudioDiagnosticsDetail::g_irUs       = 0;
        AudioDiagnosticsDetail::g_cbSeq      = seq;
    }

    // [RT] Called at end of every callback to log stats and update aggregates.
    void endCallback(uint32_t seq, int totalUs, int ringFill, int underruns) {
        int settUs = AudioDiagnosticsDetail::g_settingsUs;
        int irUs   = AudioDiagnosticsDetail::g_irUs;

        Event ev{};
        ev.timestampNs = elapsedNs();
        // Pack underruns into high 12 bits of callbackSeq (capped at 4095).
        ev.callbackSeq = (seq & 0xFFFFF) | ((uint32_t)std::min(underruns, 4095) << 20);
        ev.type = EV_CALLBACK_STATS;
        ev.v0   = totalUs;
        ev.v1   = ringFill;
        ev.v2   = settUs;
        ev.v3   = irUs;
        pushEvent(ev);

        // Aggregate counters
        underrunCount.fetch_add((uint32_t)underruns, std::memory_order_relaxed);

        int budget = (int)(budgetNs_ / 1000);
        if (totalUs > budget)
            overBudgetCount.fetch_add(1, std::memory_order_relaxed);

        // Update max callback duration (lock-free CAS loop).
        int cur = maxCallbackUs.load(std::memory_order_relaxed);
        while (totalUs > cur &&
               !maxCallbackUs.compare_exchange_weak(
                   cur, totalUs, std::memory_order_relaxed, std::memory_order_relaxed)) {}

        // Update min/max ring fill.
        updateMin(minRingFill, ringFill);
        updateMax(maxRingFill, ringFill);
    }

    // [RT] PortAudio reported an XRUN (paOutputUnderflow / paInputOverflow etc.)
    void logXrun(uint32_t seq, unsigned int statusFlags) {
        Event ev{};
        ev.timestampNs = elapsedNs();
        ev.callbackSeq = seq;
        ev.type = EV_XRUN;
        ev.v0   = (int32_t)statusFlags;
        pushEvent(ev);
        xrunCount.fetch_add(1, std::memory_order_relaxed);
    }

    // [RT] trimToTargetLatency() skipped frames — DIRECT CLICK SOURCE.
    void logTrim(uint32_t seq, int framesTrimmed, int ringFillBefore) {
        Event ev{};
        ev.timestampNs = elapsedNs();
        ev.callbackSeq = seq;
        ev.type = EV_TRIM;
        ev.v0   = framesTrimmed;
        ev.v1   = ringFillBefore;
        pushEvent(ev);
        trimCount.fetch_add(1, std::memory_order_relaxed);
        trimFramesTotal.fetch_add((uint32_t)framesTrimmed, std::memory_order_relaxed);
    }

    // [RT] pop() returned false (ring empty) — DIRECT CLICK SOURCE (silence injected).
    void logUnderrun(uint32_t seq, int frameIdxInCb, int ringFill) {
        Event ev{};
        ev.timestampNs = elapsedNs();
        ev.callbackSeq = seq;
        ev.type = EV_UNDERRUN;
        ev.v0   = frameIdxInCb;
        ev.v1   = ringFill;
        pushEvent(ev);
        // underrunCount updated in endCallback()
    }

    // [RT] Capture thread dropped frames (push() found ring full).
    void logPushDrop(int dropsThisCb, int ringFill) {
        Event ev{};
        ev.timestampNs = elapsedNs();
        ev.callbackSeq = AudioDiagnosticsDetail::g_cbSeq;
        ev.type = EV_PUSH_DROP;
        ev.v0   = dropsThisCb;
        ev.v1   = ringFill;
        pushEvent(ev);
        pushDropCount.fetch_add((uint32_t)dropsThisCb, std::memory_order_relaxed);
    }

    // [RT] applySettingsInternal or adoptPendingImpulse ran on audio thread.
    void logSettingsApply(uint32_t seq, int applyUs, int irAdoptUs) {
        Event ev{};
        ev.timestampNs = elapsedNs();
        ev.callbackSeq = seq;
        ev.type = EV_SETTINGS_APPLY;
        ev.v0   = applyUs;
        ev.v1   = irAdoptUs;
        pushEvent(ev);
        if (applyUs > 0)   settingsApplyCount.fetch_add(1, std::memory_order_relaxed);
        if (irAdoptUs > 0) irAdoptCount.fetch_add(1, std::memory_order_relaxed);
    }

    // [RT] (called from WASAPI capture thread) Period late or extra-large.
    void logCaptureBurst(int numFrames, int periodUs, int expectedPeriodUs) {
        Event ev{};
        ev.timestampNs = elapsedNs();
        ev.callbackSeq = 0; // not in a PA callback
        ev.type = EV_CAPTURE_BURST;
        ev.v0   = numFrames;
        ev.v1   = periodUs;
        ev.v2   = expectedPeriodUs;
        pushEvent(ev);
    }

    // [RT] Accumulate sub-timings from consumePendingSettings().
    void accSettingsUs(int us) { AudioDiagnosticsDetail::g_settingsUs += us; }
    void accIrUs(int us)       { AudioDiagnosticsDetail::g_irUs       += us; }

    // [RT] Read accumulated sub-timings (end of callback).
    int      getSettingsUs() const { return AudioDiagnosticsDetail::g_settingsUs; }
    int      getIrUs()       const { return AudioDiagnosticsDetail::g_irUs; }
    uint32_t currentSeq()    const { return AudioDiagnosticsDetail::g_cbSeq; }

    // ── Aggregate counters — read by UI thread ────────────────────────────────
    std::atomic<uint32_t> callbackCount{0};
    std::atomic<uint32_t> xrunCount{0};
    std::atomic<uint32_t> underrunCount{0};
    std::atomic<uint32_t> trimCount{0};
    std::atomic<uint32_t> trimFramesTotal{0};
    std::atomic<uint32_t> pushDropCount{0};
    std::atomic<uint32_t> overBudgetCount{0};
    std::atomic<int>      maxCallbackUs{0};
    std::atomic<int>      minRingFill{INT_MAX};
    std::atomic<int>      maxRingFill{0};
    std::atomic<uint32_t> settingsApplyCount{0};
    std::atomic<uint32_t> irAdoptCount{0};

private:
    AudioDiagnostics() {
#if defined(_WIN32)
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq_ = f.QuadPart;
#elif defined(__APPLE__)
        mach_timebase_info(&tbInfo_);
#endif
        streamOpenNs_ = nowNs();
    }

    // [RT] Write ev into the ring. Overwrites oldest entry if ring is full —
    // acceptable for a diagnostic tool (prefer keeping recent events).
    void pushEvent(const Event& ev) {
        uint32_t idx = ringHead_.fetch_add(1, std::memory_order_relaxed);
        ring_[idx & kRingMask] = ev; // trivially-copyable, no destructor
    }

    // [RT] Lock-free minimum update.
    static void updateMin(std::atomic<int>& a, int v) {
        int cur = a.load(std::memory_order_relaxed);
        while (v < cur &&
               !a.compare_exchange_weak(cur, v,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    static void updateMax(std::atomic<int>& a, int v) {
        int cur = a.load(std::memory_order_relaxed);
        while (v > cur &&
               !a.compare_exchange_weak(cur, v,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    static const char* typeName(EventType t) {
        switch (t) {
            case EV_CALLBACK_STATS: return "CALLBACK_STATS";
            case EV_XRUN:           return "XRUN";
            case EV_TRIM:           return "TRIM";
            case EV_UNDERRUN:       return "UNDERRUN";
            case EV_PUSH_DROP:      return "PUSH_DROP";
            case EV_SETTINGS_APPLY: return "SETTINGS_APPLY";
            case EV_IR_ADOPT:       return "IR_ADOPT";
            case EV_CAPTURE_BURST:  return "CAPTURE_BURST";
            default:                return "UNKNOWN";
        }
    }

    static void buildDesc(const Event& ev, char* buf, int bufsz) {
        switch (ev.type) {
        case EV_CALLBACK_STATS: {
            int underruns = (int)(ev.callbackSeq >> 20);
            snprintf(buf, bufsz,
                "totalUs=%d ringFill=%d settingsUs=%d irUs=%d underruns=%d",
                ev.v0, ev.v1, ev.v2, ev.v3, underruns);
            break;
        }
        case EV_XRUN: {
            char flags[64] = {};
            int off = 0;
            if (ev.v0 & 0x01) off += snprintf(flags+off, 64-off, "paInputUnderflow ");
            if (ev.v0 & 0x02) off += snprintf(flags+off, 64-off, "paInputOverflow ");
            if (ev.v0 & 0x04) off += snprintf(flags+off, 64-off, "paOutputUnderflow ");
            if (ev.v0 & 0x08) off += snprintf(flags+off, 64-off, "paOutputOverflow ");
            if (off == 0) snprintf(flags, 64, "flags=0x%X", ev.v0);
            snprintf(buf, bufsz, "*** XRUN *** %s", flags);
            break;
        }
        case EV_TRIM:
            snprintf(buf, bufsz,
                "*** TRIM *** skipped=%d frames  ringFillBefore=%d"
                "  (audible discontinuity — click source)",
                ev.v0, ev.v1);
            break;
        case EV_UNDERRUN:
            snprintf(buf, bufsz,
                "*** UNDERRUN *** frameIdx=%d ringFill=%d"
                "  (silence injected — click source)",
                ev.v0, ev.v1);
            break;
        case EV_PUSH_DROP:
            snprintf(buf, bufsz,
                "pushDrops=%d ringFill=%d  (capture dropped — ring full)",
                ev.v0, ev.v1);
            break;
        case EV_SETTINGS_APPLY:
            snprintf(buf, bufsz,
                "applyUs=%d irAdoptUs=%d"
                "%s",
                ev.v0, ev.v1,
                (ev.v0 + ev.v1) > 500 ? "  *** SLOW settings apply ***" : "");
            break;
        case EV_IR_ADOPT:
            snprintf(buf, bufsz, "adoptUs=%d irLen=%d", ev.v0, ev.v1);
            break;
        case EV_CAPTURE_BURST:
            snprintf(buf, bufsz,
                "*** CAPTURE BURST *** frames=%d periodUs=%d expectedUs=%d"
                "  (%.1fx late)",
                ev.v0, ev.v1, ev.v2,
                ev.v2 > 0 ? (double)ev.v1 / ev.v2 : 0.0);
            break;
        default:
            snprintf(buf, bufsz, "v0=%d v1=%d v2=%d v3=%d", ev.v0, ev.v1, ev.v2, ev.v3);
        }
    }

    // Platform timing
    int64_t streamOpenNs_ = 0;
    int64_t budgetNs_     = 5'333'333LL; // 256/48000 * 1e9
    double  sampleRate_   = 48000.0;
    int     framesPerCb_  = 256;

#if defined(_WIN32)
    int64_t freq_ = 10'000'000LL; // QueryPerformanceFrequency result
#elif defined(__APPLE__)
    mach_timebase_info_data_t tbInfo_{};
#endif

    // Pre-allocated event ring — 4096 × 32 = 128 KB, never malloc'd again.
    Event    ring_[kRingCap]{};
    std::atomic<uint32_t> ringHead_{0};
    uint32_t flushHead_ = 0; // only read/written by the UI thread
};
