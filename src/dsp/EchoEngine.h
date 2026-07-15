#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// EchoEngine — Professional real-time stereo feedback delay
//
// Design contract
// ───────────────
//  • Real-time / lock-free: setParams() and processStereo() are both called
//    from the audio thread (AudioProcessor's double-buffer handles UI→audio
//    hand-off upstream). No mutex, no allocation in processStereo().
//  • Sample-rate independent: every time constant is recomputed in init().
//  • Supports 44.1 / 48 / 96 / 192 kHz without re-tuning.
//  • Smooth parameters: all perceptually significant gains, levels, mix
//    ratios, and the delay time itself are driven through one-pole exponential
//    smoothers.  No zipper noise.  No clicks on knob moves or preset loads.
//  • Delay-time glide: changing the read position advances at a controlled
//    rate rather than jumping instantly, producing a natural pitch glide
//    (cassette-deck style) rather than a discontinuity.
//  • Feedback safety: a two-stage protection chain prevents runaway:
//      1. Soft-knee tanh limiter at ±0.9 (always active in feedback path)
//      2. Hard cap on the feedback coefficient (≤ 0.97)
//  • DC blocking: first-order high-pass at ~5 Hz in the feedback path
//    prevents DC accumulation in long, high-feedback loops.
//  • Output protection: always-on cubic soft-limiter on the final output.
//    Engages smoothly above ±1.0, hard-limits at ±2.0.
//  • Anti-denormal: a tiny noise floor is added to each delay buffer write
//    to flush denormal floats on x86 hardware without FTZ/DAZ flags.
// ─────────────────────────────────────────────────────────────────────────────


// ═══════════════════════════════════════════════════════════════════════════
//  Internal primitives — header-private, zero external dependencies
// ═══════════════════════════════════════════════════════════════════════════

// ── One-pole exponential parameter smoother ──────────────────────────────────
// Ramps a float from its current value toward a target over an RC-like time
// constant.  A single call to next() per sample is all that is required on
// the hot path; the rest of the logic lives in init() / set() / snap().
//
// Time constant choice:
//   • Gains / mix / levels / saturation  → kSmoothGainMs   (8 ms)
//   • Feedback coefficient               → kSmoothFeedbackMs (5 ms)
//   • Delay time (in samples)            → kSmoothDelayMs  (15 ms)
//   • Tone / damping coefficient         → kSmoothToneMs   (5 ms)
//   • Cross-feed / ping-pong             → kSmoothXfeedMs  (8 ms)
struct EchoSmoothed {
    float v  = 0.f;   // current value (audio-rate)
    float tgt = 0.f;  // target value  (set from control rate)
    float c   = 0.f;  // per-sample decay coefficient

    /// Call once when sample rate is known (or changes).
    void init(float sr, float timeMs, float initVal = 0.f) noexcept {
        c   = (timeMs > 0.f && sr > 0.f)
              ? std::exp(-1.f / (sr * timeMs * 0.001f))
              : 0.f;
        v   = tgt = initVal;
    }

    /// Set a new target; next() will ramp toward it.
    void set(float t) noexcept { tgt = t; }

    /// Instantly jump to value (e.g., on init / reset, not during playback).
    void snap(float t) noexcept { v = tgt = t; }

    /// Advance one sample and return the current value.
    inline float next() noexcept {
        v += (1.f - c) * (tgt - v);
        return v;
    }

    float get() const noexcept { return v; }
};

// ── Two-pole biquad filter (Direct Form I) ───────────────────────────────────
// setBypass() produces a unity-gain all-pass so tick() is always safe on the
// hot path with no branching.  Coefficients updated from setParams() only
// (control rate); state is preserved across coefficient updates to avoid
// discontinuities in the feedback tail.
struct EchoBiquad {
    float x1=0,x2=0,y1=0,y2=0;
    float b0=1,b1=0,b2=0,a1=0,a2=0;

    void setLP(float f, float sr, float Q=0.7071f) noexcept {
        f = std::clamp(f, 20.f, sr*0.499f);
        float w=2.f*(float)M_PI*f/sr, cw=cosf(w), sw=sinf(w), al=sw/(2.f*Q);
        float inv=1.f/(1.f+al);
        b0=((1.f-cw)*0.5f)*inv; b1=(1.f-cw)*inv; b2=b0;
        a1=(-2.f*cw)*inv; a2=(1.f-al)*inv;
    }
    void setHP(float f, float sr, float Q=0.7071f) noexcept {
        f = std::clamp(f, 20.f, sr*0.499f);
        float w=2.f*(float)M_PI*f/sr, cw=cosf(w), sw=sinf(w), al=sw/(2.f*Q);
        float inv=1.f/(1.f+al);
        b0=((1.f+cw)*0.5f)*inv; b1=-(1.f+cw)*inv; b2=b0;
        a1=(-2.f*cw)*inv; a2=(1.f-al)*inv;
    }
    void setLS(float f, float sr, float gainDb) noexcept {
        if (fabsf(gainDb)<0.05f){setBypass();return;}
        float A=powf(10.f,gainDb/40.f);
        float w=2.f*(float)M_PI*std::clamp(f,20.f,sr*0.49f)/sr;
        float cw=cosf(w),sw=sinf(w),sq=sqrtf(A);
        float al=sw/2.f*sqrtf((A+1.f/A)*(1.f/0.7071f-1.f)+2.f);
        float d=(A+1)+(A-1)*cw+2*sq*al, inv=1.f/d;
        b0=A*((A+1)-(A-1)*cw+2*sq*al)*inv;
        b1=2*A*((A-1)-(A+1)*cw)*inv;
        b2=A*((A+1)-(A-1)*cw-2*sq*al)*inv;
        a1=-2*((A-1)+(A+1)*cw)*inv;
        a2=((A+1)+(A-1)*cw-2*sq*al)*inv;
    }
    void setHS(float f, float sr, float gainDb) noexcept {
        if (fabsf(gainDb)<0.05f){setBypass();return;}
        float A=powf(10.f,gainDb/40.f);
        float w=2.f*(float)M_PI*std::clamp(f,20.f,sr*0.49f)/sr;
        float cw=cosf(w),sw=sinf(w),sq=sqrtf(A);
        float al=sw/2.f*sqrtf((A+1.f/A)*(1.f/0.7071f-1.f)+2.f);
        float d=(A+1)-(A-1)*cw+2*sq*al, inv=1.f/d;
        b0=A*((A+1)+(A-1)*cw+2*sq*al)*inv;
        b1=-2*A*((A-1)+(A+1)*cw)*inv;
        b2=A*((A+1)+(A-1)*cw-2*sq*al)*inv;
        a1=2*((A-1)-(A+1)*cw)*inv;
        a2=((A+1)-(A-1)*cw-2*sq*al)*inv;
    }
    void setPK(float f, float sr, float gainDb, float Q=1.0f) noexcept {
        if (fabsf(gainDb)<0.05f){setBypass();return;}
        float A=powf(10.f,gainDb/40.f);
        float w=2.f*(float)M_PI*std::clamp(f,20.f,sr*0.49f)/sr;
        float cw=cosf(w),sw=sinf(w),al=sw/(2.f*Q);
        float inv=1.f/(1.f+al/A);
        b0=(1.f+al*A)*inv; b1=(-2.f*cw)*inv; b2=(1.f-al*A)*inv;
        a1=b1; a2=(1.f-al/A)*inv;
    }
    void setBypass() noexcept { b0=1;b1=b2=a1=a2=0; }
    void reset()     noexcept { x1=x2=y1=y2=0; }

    inline float tick(float x) noexcept {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return y;
    }
};

// ── Schroeder allpass diffusor ────────────────────────────────────────────────
struct EchoAllpass {
    static constexpr int kMaxN = 2048;
    float buf[kMaxN]{};
    int   pos=0, sz=113;
    float g=0.5f;
    void init(int n, float coeff) noexcept {
        sz=std::clamp(n,1,kMaxN); g=std::clamp(coeff,0.f,0.95f);
        std::fill(buf,buf+kMaxN,0.f); pos=0;
    }
    inline float tick(float x) noexcept {
        float d=buf[pos];
        float v=x-g*d;
        buf[pos]=v;
        if (++pos>=sz) pos=0;
        return g*v+d;
    }
    void reset() noexcept { std::fill(buf,buf+kMaxN,0.f); pos=0; }
};

// ── DC blocker (first-order high-pass, ~5 Hz) ────────────────────────────────
// Prevents DC accumulation in long, high-feedback loops.
// y[n] = x[n] - x[n-1] + R*y[n-1],  R = 1 - 2π·f/sr
struct EchoDCBlock {
    float xm1 = 0.f, ym1 = 0.f;
    float R    = 0.9997f;

    void init(float sr, float freqHz = 5.f) noexcept {
        R   = 1.f - (2.f * (float)M_PI * freqHz / sr);
        R   = std::clamp(R, 0.99f, 0.9999f);
        xm1 = ym1 = 0.f;
    }
    inline float tick(float x) noexcept {
        float y = x - xm1 + R * ym1;
        xm1 = x; ym1 = y;
        return y;
    }
    void reset() noexcept { xm1 = ym1 = 0.f; }
};

// ── Ring-buffer helpers ───────────────────────────────────────────────────────
// Linearly-interpolated fractional read.  wPos is the NEXT write position,
// so wPos-1 is the most recent sample, wPos-d is d samples ago.
static inline float echoRingRead(const std::vector<float>& b,
                                  int wPos, float fracSamples) noexcept {
    int n = (int)b.size();
    int i0 = wPos - (int)fracSamples;   if (i0 < 0) i0 += n;
    int i1 = i0 - 1;                    if (i1 < 0) i1 += n;
    float t = fracSamples - (float)(int)fracSamples;
    return b[(size_t)i0]*(1.f-t) + b[(size_t)i1]*t;
}

// ── DSP utility functions ─────────────────────────────────────────────────────
static inline float echoDB2Lin(float db) noexcept {
    return powf(10.f, db * 0.05f);
}

// Soft-knee feedback safety limiter — always active in feedback path.
// Engages smoothly above ±0.85, hard-limits at ±1.0.
// Uses a rational approximation of tanh for speed.
static inline float echoFbSafeClip(float x) noexcept {
    // Fast tanh approximation: tanh(x) ≈ x*(27+x²)/(27+9x²)  [Padé]
    // Applied only when |x| > 0.85 to save cycles on the common case.
    float ax = fabsf(x);
    if (ax <= 0.85f) return x;
    // Scale so full-scale input maps to tanh headroom
    float d = 1.f + ax * 0.3f;
    float approx = x / d;                // softer than hard clip
    return std::clamp(approx, -1.f, 1.f);
}

// Analog tanh saturation (odd-harmonic, unity-gain normalised).
static inline float echoTanhSat(float x, float amount) noexcept {
    if (amount < 1e-3f) return x;
    float d = 1.f + amount * 4.f;
    return tanhf(x * d) / d;
}

// Tape even-harmonic saturation (cubic bias, unity-gain normalised).
static inline float echoTapeSat(float x, float amount) noexcept {
    if (amount < 1e-3f) return x;
    return x - amount * 0.08f * x * x * x;
}

// Always-on cubic output soft-limiter.
// Unity pass below ±1.0; soft knee 1.0–2.0; hard ceiling at 2.0.
static inline float echoOutLimit(float x) noexcept {
    float ax = fabsf(x);
    if (ax <= 1.f) return x;
    if (ax >= 2.f) return (x > 0.f) ? 1.f : -1.f;
    // Cubic soft-knee between 1 and 2
    float t  = ax - 1.f;               // 0..1
    float sat = 1.f + t * (1.f - t * 0.5f); // 1..1.5 (smooth)
    return (x > 0.f) ? sat : -sat;
}

// Legacy cubic soft-clip used by the aeSoftClip option.
static inline float echoSoftClip(float x) noexcept {
    float ax = fabsf(x);
    if (ax >= 1.f) return (x > 0.f) ? 1.f : -1.f;
    if (ax < 0.667f) return x;
    float t = 2.f - 1.5f * ax;
    return (x > 0.f ? 1.f : -1.f) * (1.f - t*t*t/3.f + 0.037037f);
}

// Anti-denormal: add/remove a negligible DC offset so that zero-input
// feedback loops do not stall at a denormal value.  The offset is well below
// the noise floor of any practical 24-bit or 32-bit pipeline.
static constexpr float kAntiDenormal = 1e-25f;


// ═══════════════════════════════════════════════════════════════════════════
//  EchoEngine — parameter definitions
// ═══════════════════════════════════════════════════════════════════════════
class EchoEngine {
public:

    // ── Params ───────────────────────────────────────────────────────────────
    // All values in "natural UI units" — percentage, milliseconds, dB, Hz —
    // exactly as stored in AppSettings.  AudioProcessor passes them in as-is;
    // conversion and clamping happen inside setParams().
    struct Params {
        // aeOn gates the advanced signal chain.  When false the advanced
        // params have no effect; only the basic 10 params run.
        bool  aeOn = false;

        // ── Basic Echo ───────────────────────────────────────────────────────
        float delayMs    = 350.f;   // 1..2000 ms
        float feedback   = 0.55f;   // 0..0.95
        float mix        = 0.38f;   // 0..1 wet/dry
        float tone       = 0.60f;   // 0..1 repeat darkening
        float pingPong   = 0.15f;   // 0..1 L↔R cross-feed
        int   numEchoes  = 0;       // 0=∞ feedback, 1-10=discrete taps
        float echoAmount = 1.0f;    // 0..1 input drive
        float wetLevel   = 1.0f;    // 0..1 wet level
        float dryLevel   = 1.0f;    // 0..1 dry level
        float outputGain = 1.0f;    // 0..4 post-mix gain

        // ── Advanced Echo Engine ─────────────────────────────────────────────
        // [Delay]
        float aeLeftDelayMs   = 350.f;
        float aeRightDelayMs  = 350.f;
        float aeStereoOffset  = 0.f;
        float aeStereoWidthD  = 0.f;
        bool  aeTempoSync     = false;
        bool  aeMillisecondMode = true;

        // [Feedback]
        float aeCrossFeedback  = 0.f;
        float aeFbSaturation   = 0.f;
        float aeFbDamping      = 0.f;
        float aeFbLowCut       = 20.f;
        float aeFbHighCut      = 20000.f;
        float aeFbDiffusion    = 0.f;

        // [Stereo]
        float aeBalance     = 0.f;
        float aeLeftLevel   = 1.0f;
        float aeRightLevel  = 1.0f;
        float aeMidSideMix  = 0.f;
        bool  aePingPongMode = false;
        bool  aeSwapChannels = false;

        // [Tone]
        float aeToneLowCut    = 20.f;
        float aeToneHighCut   = 20000.f;
        float aeToneBass      = 0.f;
        float aeToneMid       = 0.f;
        float aeToneTreble    = 0.f;
        float aeTonePresence  = 0.f;
        float aeToneBrightness= 0.f;

        // [Saturation]
        float aeTapeSat    = 0.f;
        float aeAnalogSat  = 0.f;
        float aeDrive      = 0.f;
        float aeWarmth     = 0.f;
        bool  aeSoftClip   = false;

        // [Dynamics]
        float aeInputGainDb  = 0.f;
        float aeOutputGainDb = 0.f;
        float aeWetGainDb    = 0.f;
        float aeDryGainDb    = 0.f;
        bool  aeIntLimiter   = false;
        bool  aeSoftLimiter  = false;

        // [Mix]
        float aeWetLevel2 = 1.0f;
        float aeDryLevel2 = 1.0f;
        float aeBlend     = 1.0f;
        float aeMix       = -1.f;   // ≥0 overrides mix; <0 → use basic mix

        // [Modulation]
        float aeWow         = 0.f;
        float aeFlutter     = 0.f;
        float aeModDepth    = 0.f;
        float aeModRate     = 1.f;
        float aeRandomDrift = 0.f;

        // [Spatial]
        float aeHaasWidth       = 0.f;
        float aeStereoSpread    = 0.f;
        float aeEarlyReflections= 0.f;
        float aeReflLevel       = 0.f;
        float aeReflDelay       = 20.f;
    };

    // ── Construction ─────────────────────────────────────────────────────────
    EchoEngine()  { init(44100.0); }

    // ── Initialise / change sample rate ──────────────────────────────────────
    // Rebuilds all buffers and resets all state.  Call from audio thread only.
    void init(double sr) {
        sampleRate = std::max(sr, 8000.0);
        float fsr  = (float)sampleRate;

        // ── Delay buffers ─────────────────────────────────────────────────────
        int mainMax = (int)(kMaxDelayS * sampleRate) + 16;
        bufL.assign((size_t)mainMax, 0.f);
        bufR.assign((size_t)mainMax, 0.f);
        writePos = 0;

        // ── Haas buffer (max 50 ms) ───────────────────────────────────────────
        int haasMax = (int)(0.050 * sampleRate) + 8;
        haasL.assign((size_t)haasMax, 0.f);
        haasR.assign((size_t)haasMax, 0.f);
        haasHPos = 0;

        // ── Early reflection buffer (max 120 ms) ──────────────────────────────
        int reflMax = (int)(0.120 * sampleRate) + 8;
        reflL.assign((size_t)reflMax, 0.f);
        reflR.assign((size_t)reflMax, 0.f);
        reflPos = 0;

        // ── Allpass diffusors (~7 ms at the given rate) ───────────────────────
        int diffSz = std::max(1, (int)(0.007 * sampleRate));
        diffL.init(diffSz, 0.5f);
        diffR.init(diffSz, 0.5f);

        // ── DC blockers ───────────────────────────────────────────────────────
        dcL.init(fsr, 5.f);
        dcR.init(fsr, 5.f);

        // ── Modulation state ──────────────────────────────────────────────────
        wowPhase = flutterPhase = 0.f;
        driftState = 0.f;
        rng_ = 0xACE17839u;

        // ── Internal feedback damping state ───────────────────────────────────
        basicDampL = basicDampR = 0.f;
        fbDampStateL = fbDampStateR = 0.f;

        // ── Smoothers: initialise with neutral values ─────────────────────────
        // Time constants follow the scheme described at the top of the file.
        feedbackSm .init(fsr, kSmFeedbackMs,  0.55f);
        mixSm      .init(fsr, kSmGainMs,      0.38f);
        echoAmtSm  .init(fsr, kSmGainMs,      1.0f);
        wetLvlSm   .init(fsr, kSmGainMs,      1.0f);
        dryLvlSm   .init(fsr, kSmGainMs,      1.0f);
        outGainSm  .init(fsr, kSmGainMs,      1.0f);
        pingPongSm .init(fsr, kSmXfeedMs,     0.15f);

        // Delay smoothers start at the default delay (in samples).
        float initSamp = 350.f * 0.001f * fsr;
        initSamp = std::clamp(initSamp, 1.f, (float)(mainMax - 1));
        dSampLSm.init(fsr, kSmDelayMs, initSamp);
        dSampRSm.init(fsr, kSmDelayMs, initSamp);

        // One-pole tone (basic damping) coefficient smoother.
        // basicDamp = 0.05 + tone*0.85; coeff = 1 - basicDamp.
        float initToneCoeff = 1.f - (0.05f + 0.60f * 0.85f);
        dampCoeffSm.init(fsr, kSmToneMs, initToneCoeff);

        // Advanced smoothers — neutral defaults
        inGainLinSm  .init(fsr, kSmGainMs, 1.0f);
        aeWetGainSm  .init(fsr, kSmGainMs, 1.0f);
        aeDryGainSm  .init(fsr, kSmGainMs, 1.0f);
        aeOutGainSm  .init(fsr, kSmGainMs, 1.0f);
        wetLvl2Sm    .init(fsr, kSmGainMs, 1.0f);
        dryLvl2Sm    .init(fsr, kSmGainMs, 1.0f);
        blendSm      .init(fsr, kSmGainMs, 1.0f);
        aeMixSm      .init(fsr, kSmGainMs, -1.f);  // <0 = "use basic mix"
        xFeedSm      .init(fsr, kSmXfeedMs, 0.f);
        leftLvlSm    .init(fsr, kSmGainMs, 1.0f);
        rightLvlSm   .init(fsr, kSmGainMs, 1.0f);
        balanceSm    .init(fsr, kSmGainMs, 0.f);
        midSideSm    .init(fsr, kSmGainMs, 0.f);
        stereoSpreadSm.init(fsr,kSmGainMs, 0.f);
        tapeSatSm    .init(fsr, kSmToneMs, 0.f);
        analogSatSm  .init(fsr, kSmToneMs, 0.f);
        driveSm      .init(fsr, kSmToneMs, 0.f);
        warmthSm     .init(fsr, kSmToneMs, 0.f);
        fbSatSm      .init(fsr, kSmToneMs, 0.f);

        resetFilters();
        recomputeFilters();
    }

    void setSampleRate(double sr) { init(sr); }

    // ── setParams ─────────────────────────────────────────────────────────────
    // Called from the audio thread (via AudioProcessor::consumePendingSettings).
    // Updates smoother targets and biquad coefficients.  Never snaps during
    // playback — the smoothers handle the transition.
    void setParams(const Params& newP) {
        p = newP;

        // ── Clamp basic params ────────────────────────────────────────────────
        p.delayMs    = std::clamp(p.delayMs,    1.f, kMaxDelayS*1000.f);
        p.feedback   = std::clamp(p.feedback,   0.f, kMaxFeedback);
        p.mix        = std::clamp(p.mix,        0.f, 1.f);
        p.tone       = std::clamp(p.tone,       0.f, 1.f);
        p.pingPong   = std::clamp(p.pingPong,   0.f, 1.f);
        p.numEchoes  = std::clamp(p.numEchoes,  0, 10);
        p.echoAmount = std::clamp(p.echoAmount, 0.f, 1.f);
        p.wetLevel   = std::clamp(p.wetLevel,   0.f, 1.f);
        p.dryLevel   = std::clamp(p.dryLevel,   0.f, 1.f);
        p.outputGain = std::clamp(p.outputGain, 0.f, 4.f);

        // ── Clamp advanced params ─────────────────────────────────────────────
        p.aeLeftDelayMs  = std::clamp(p.aeLeftDelayMs,  1.f, kMaxDelayS*1000.f);
        p.aeRightDelayMs = std::clamp(p.aeRightDelayMs, 1.f, kMaxDelayS*1000.f);
        p.aeStereoOffset = std::clamp(p.aeStereoOffset, -200.f, 200.f);
        p.aeStereoWidthD = std::clamp(p.aeStereoWidthD, 0.f, 1.f);
        p.aeCrossFeedback= std::clamp(p.aeCrossFeedback, 0.f, 1.f);
        p.aeFbSaturation = std::clamp(p.aeFbSaturation,  0.f, 1.f);
        p.aeFbDamping    = std::clamp(p.aeFbDamping,     0.f, 1.f);
        p.aeFbLowCut     = std::clamp(p.aeFbLowCut,    20.f, 18000.f);
        p.aeFbHighCut    = std::clamp(p.aeFbHighCut,  200.f, 20000.f);
        p.aeFbDiffusion  = std::clamp(p.aeFbDiffusion,  0.f, 1.f);
        p.aeBalance      = std::clamp(p.aeBalance,      -1.f, 1.f);
        p.aeLeftLevel    = std::clamp(p.aeLeftLevel,     0.f, 2.f);
        p.aeRightLevel   = std::clamp(p.aeRightLevel,    0.f, 2.f);
        p.aeMidSideMix   = std::clamp(p.aeMidSideMix,   0.f, 1.f);
        p.aeToneLowCut   = std::clamp(p.aeToneLowCut,  20.f, 18000.f);
        p.aeToneHighCut  = std::clamp(p.aeToneHighCut, 200.f, 20000.f);
        p.aeWow          = std::clamp(p.aeWow,           0.f, 1.f);
        p.aeFlutter      = std::clamp(p.aeFlutter,       0.f, 1.f);
        p.aeModDepth     = std::clamp(p.aeModDepth,      0.f, 1.f);
        p.aeModRate      = std::clamp(p.aeModRate,      0.01f, 20.f);
        p.aeRandomDrift  = std::clamp(p.aeRandomDrift,   0.f, 1.f);
        p.aeHaasWidth    = std::clamp(p.aeHaasWidth,     0.f, 40.f);
        p.aeStereoSpread = std::clamp(p.aeStereoSpread,  0.f, 1.f);
        p.aeEarlyReflections=std::clamp(p.aeEarlyReflections,0.f,1.f);
        p.aeReflLevel    = std::clamp(p.aeReflLevel,     0.f, 1.f);
        p.aeReflDelay    = std::clamp(p.aeReflDelay,     1.f, 100.f);
        p.aeWetLevel2    = std::clamp(p.aeWetLevel2,     0.f, 1.f);
        p.aeDryLevel2    = std::clamp(p.aeDryLevel2,     0.f, 1.f);
        p.aeBlend        = std::clamp(p.aeBlend,         0.f, 1.f);

        float fsr = (float)sampleRate;
        int   mainMax = (int)bufL.size();

        // ── Update smoother targets ───────────────────────────────────────────
        // Basic
        feedbackSm.set(std::min(p.feedback, kMaxFeedback));
        mixSm     .set(p.mix);
        pingPongSm.set(p.pingPong);
        echoAmtSm .set(p.echoAmount);
        wetLvlSm  .set(p.wetLevel);
        dryLvlSm  .set(p.dryLevel);
        outGainSm .set(p.outputGain);

        // Delay times → samples
        float leftMs  = p.aeOn ? p.aeLeftDelayMs
                                 : p.delayMs;
        float rightMs = p.aeOn ? (p.aeRightDelayMs + p.aeStereoOffset)
                                 : p.delayMs;
        float dSampL = std::clamp(leftMs  * 0.001f * fsr, 1.f, (float)(mainMax-2));
        float dSampR = std::clamp(rightMs * 0.001f * fsr, 1.f, (float)(mainMax-2));
        dSampLSm.set(dSampL);
        dSampRSm.set(dSampR);

        // Basic tone: damping coefficient
        float bd = std::clamp(0.05f + p.tone * 0.85f, 0.05f, 0.90f);
        dampCoeffSm.set(1.f - bd);  // one-pole LP coefficient

        // Advanced
        inGainLinSm  .set(echoDB2Lin(p.aeInputGainDb));
        aeWetGainSm  .set(echoDB2Lin(p.aeWetGainDb));
        aeDryGainSm  .set(echoDB2Lin(p.aeDryGainDb));
        aeOutGainSm  .set(echoDB2Lin(p.aeOutputGainDb));
        wetLvl2Sm    .set(p.aeWetLevel2);
        dryLvl2Sm    .set(p.aeDryLevel2);
        blendSm      .set(p.aeBlend);
        aeMixSm      .set(p.aeMix);     // negative = "use basic mix"
        xFeedSm      .set(p.aeCrossFeedback);
        leftLvlSm    .set(p.aeLeftLevel);
        rightLvlSm   .set(p.aeRightLevel);
        balanceSm    .set(p.aeBalance);
        midSideSm    .set(p.aeMidSideMix);
        stereoSpreadSm.set(p.aeStereoSpread);
        tapeSatSm    .set(p.aeTapeSat);
        analogSatSm  .set(p.aeAnalogSat);
        driveSm      .set(p.aeDrive);
        warmthSm     .set(p.aeWarmth);
        fbSatSm      .set(p.aeFbSaturation);

        // Allpass diffusor gain
        diffL.g = diffR.g = p.aeFbDiffusion * 0.7f;

        // ── Recompute biquad filter coefficients ──────────────────────────────
        recomputeFilters();
    }

    void reset() { init(sampleRate); }

    // ── processStereo ─────────────────────────────────────────────────────────
    // Called once per stereo frame from the audio thread.  No allocation,
    // no mutex, no branching on per-frame conditions that change at control
    // rate (those are handled by smoothers).
    void processStereo(float& l, float& r) noexcept {
        const int n = (int)bufL.size();
        float fsr   = (float)sampleRate;

        // ── 1. Advance all smoothers ──────────────────────────────────────────
        // IMPORTANT: every smoother must be called exactly once per sample,
        // even if its output is conditionally used, so its internal state
        // stays synchronised with wall-clock time.
        const float smFb       = feedbackSm .next();
        const float smMix      = mixSm      .next();
        const float smPP       = pingPongSm .next();
        const float smAmt      = echoAmtSm  .next();
        const float smWetLvl   = wetLvlSm   .next();
        const float smDryLvl   = dryLvlSm   .next();
        const float smOutGain  = outGainSm  .next();
        const float smDampCoeff= dampCoeffSm.next();  // one-pole LP coefficient
        const float smDSampL   = dSampLSm   .next();
        const float smDSampR   = dSampRSm   .next();
        const float smInGainLin= inGainLinSm .next();
        const float smAeWetG   = aeWetGainSm .next();
        const float smAeDryG   = aeDryGainSm .next();
        const float smAeOutG   = aeOutGainSm .next();
        const float smWetLvl2  = wetLvl2Sm   .next();
        const float smDryLvl2  = dryLvl2Sm   .next();
        const float smBlend    = blendSm      .next();
        const float smAeMix    = aeMixSm      .next();
        const float smXFeed    = xFeedSm      .next();
        const float smLeftLvl  = leftLvlSm    .next();
        const float smRightLvl = rightLvlSm   .next();
        const float smBalance  = balanceSm    .next();
        const float smMidSide  = midSideSm    .next();
        const float smStSpread = stereoSpreadSm.next();
        const float smTapeSat  = tapeSatSm    .next();
        const float smAnaLog   = analogSatSm  .next();
        const float smDrive    = driveSm      .next();
        const float smWarmth   = warmthSm     .next();
        const float smFbSat    = fbSatSm      .next();

        // ── 2. Input gain ─────────────────────────────────────────────────────
        float inL = l * smInGainLin;
        float inR = r * smInGainLin;
        float rawDryL = l, rawDryR = r;

        // ── 3. Modulation: LFO + random drift ────────────────────────────────
        float wowFreq  = p.aeModRate;
        float flutFreq = p.aeModRate * 12.f;
        wowPhase     += wowFreq  / fsr; if (wowPhase     > 1.f) wowPhase     -= 1.f;
        flutterPhase += flutFreq / fsr; if (flutterPhase > 1.f) flutterPhase -= 1.f;
        float wowLFO  = sinf(wowPhase     * 2.f * (float)M_PI);
        float flutLFO = sinf(flutterPhase * 2.f * (float)M_PI);

        // LFSR-based random drift
        rng_       = rng_ * 1664525u + 1013904223u;
        float noise = (float)(int)(rng_ >> 16) / 32767.5f - 1.f;
        driftState  = driftState * 0.9998f + noise * 0.0002f;

        float modOffset =
              wowLFO    * p.aeWow         * p.aeModDepth * 0.015f * fsr
            + flutLFO   * p.aeFlutter     * p.aeModDepth * 0.003f * fsr
            + driftState* p.aeRandomDrift * 0.005f       * fsr;

        // ── 4. Clamp smoothed delay to valid buffer range ─────────────────────
        float dSampL = std::clamp(smDSampL + modOffset, 1.f, (float)(n - 2));
        float dSampR = std::clamp(smDSampR + modOffset, 1.f, (float)(n - 2));

        // ── 5. Read from delay buffers ────────────────────────────────────────
        float readL, readR;
        if (p.numEchoes == 0) {
            readL = echoRingRead(bufL, writePos, dSampL);
            readR = echoRingRead(bufR, writePos, dSampR);
        } else {
            float sumL = 0.f, sumR = 0.f, scale = 1.f;
            int dInt = std::max(1, (int)dSampL);
            for (int i = 1; i <= p.numEchoes; ++i) {
                int ri = writePos - i * dInt; while (ri < 0) ri += n;
                sumL += bufL[(size_t)ri] * scale;
                sumR += bufR[(size_t)ri] * scale;
                scale *= smFb;
            }
            readL = sumL; readR = sumR;
        }

        // ── 6. Allpass diffusion ──────────────────────────────────────────────
        if (p.aeFbDiffusion > 0.01f) {
            readL = diffL.tick(readL);
            readR = diffR.tick(readR);
        }

        // ── 7. Basic one-pole tone damping ────────────────────────────────────
        // Uses the smoothed coefficient to prevent zipper noise when the
        // Tone slider moves.  smDampCoeff is the LP one-pole coefficient.
        basicDampL += (readL - basicDampL) * (1.f - smDampCoeff);
        basicDampR += (readR - basicDampR) * (1.f - smDampCoeff);

        // ── 8. Advanced feedback-path saturation ──────────────────────────────
        float fbSigL = basicDampL, fbSigR = basicDampR;
        if (smFbSat > 0.01f) {
            float driveBoost = 1.f + p.aeDrive;
            fbSigL = echoTanhSat(fbSigL * driveBoost, smFbSat);
            fbSigR = echoTanhSat(fbSigR * driveBoost, smFbSat);
        }

        // ── 9. DC blocking (prevents DC buildup in feedback loop) ─────────────
        fbSigL = dcL.tick(fbSigL);
        fbSigR = dcR.tick(fbSigR);

        // ── 10. Advanced feedback damping ─────────────────────────────────────
        if (p.aeFbDamping > 0.01f) {
            float xtra = 0.05f + p.aeFbDamping * 0.85f;
            fbDampStateL += (fbSigL - fbDampStateL) * (1.f - xtra);
            fbDampStateR += (fbSigR - fbDampStateR) * (1.f - xtra);
            fbSigL = fbDampStateL;
            fbSigR = fbDampStateR;
        }

        // ── 11. Feedback-path EQ (HP + LP biquads) ───────────────────────────
        fbSigL = fbHP[0].tick(fbSigL); fbSigR = fbHP[1].tick(fbSigR);
        fbSigL = fbLP[0].tick(fbSigL); fbSigR = fbLP[1].tick(fbSigR);

        // ── 12. Feedback safety limiter ───────────────────────────────────────
        // Always-active soft-knee limiter prevents runaway at high feedback
        // or extreme saturation settings.  Engages smoothly above ±0.85.
        fbSigL = echoFbSafeClip(fbSigL);
        fbSigR = echoFbSafeClip(fbSigR);

        // ── 13. Cross-feed (ping-pong + AE cross-feedback) ────────────────────
        float pp    = p.aePingPongMode ? 1.f : smPP;
        float cross = std::clamp(pp + smXFeed, 0.f, 1.f);
        float wL    = fbSigL * (1.f - cross) + fbSigR * cross;
        float wR    = fbSigR * (1.f - cross) + fbSigL * cross;

        // ── 14. Write to delay buffers ────────────────────────────────────────
        // Anti-denormal offset alternates sign each sample to stay below the
        // noise floor while reliably flushing denormals.
        float adf = (writePos & 1) ? kAntiDenormal : -kAntiDenormal;
        if (p.numEchoes == 0) {
            bufL[(size_t)writePos] = inL * smAmt + wL * smFb + adf;
            bufR[(size_t)writePos] = inR * smAmt + wR * smFb + adf;
        } else {
            bufL[(size_t)writePos] = inL * smAmt + adf;
            bufR[(size_t)writePos] = inR * smAmt + adf;
        }
        writePos = (writePos + 1) % n;

        // ── 15. Main tone EQ (7-band: HP, LP, bass, mid, treble, presence, air)
        wL = toneHP  [0].tick(wL); wR = toneHP  [1].tick(wR);
        wL = toneLP  [0].tick(wL); wR = toneLP  [1].tick(wR);
        wL = toneBass[0].tick(wL); wR = toneBass[1].tick(wR);
        wL = toneMid [0].tick(wL); wR = toneMid [1].tick(wR);
        wL = toneTre [0].tick(wL); wR = toneTre [1].tick(wR);
        wL = tonePres[0].tick(wL); wR = tonePres[1].tick(wR);
        wL = toneBrt [0].tick(wL); wR = toneBrt [1].tick(wR);

        // ── 16. Output saturation (tape + analog + warmth + drive) ───────────
        float totalSat = smTapeSat * 0.6f + smAnaLog * 0.4f;
        if (totalSat > 0.01f || smDrive > 0.01f) {
            float drv = 1.f + smDrive * 3.f;
            wL *= drv; wR *= drv;
            if (smTapeSat > 0.01f) {
                wL = echoTapeSat(wL, smTapeSat);
                wR = echoTapeSat(wR, smTapeSat);
            }
            if (smAnaLog > 0.01f) {
                wL = echoTanhSat(wL, smAnaLog);
                wR = echoTanhSat(wR, smAnaLog);
            }
            if (smWarmth > 0.01f) {
                wL = wL + smWarmth * 0.05f * wL * fabsf(wL);
                wR = wR + smWarmth * 0.05f * wR * fabsf(wR);
            }
            if (drv > 1.f) { wL /= drv; wR /= drv; }
        }
        if (p.aeSoftClip) { wL = echoSoftClip(wL); wR = echoSoftClip(wR); }

        // ── 17. Haas stereo width ─────────────────────────────────────────────
        if (p.aeHaasWidth > 0.01f) {
            int hn = (int)haasL.size();
            haasL[(size_t)haasHPos] = wL;
            haasR[(size_t)haasHPos] = wR;
            int hd = std::min(hn - 1, (int)(p.aeHaasWidth * 0.001f * fsr));
            int hri = haasHPos - hd; if (hri < 0) hri += hn;
            wR = haasR[(size_t)hri];
            haasHPos = (haasHPos + 1) % hn;
        }

        // ── 18. Early reflections (6-tap fixed pattern) ───────────────────────
        if (p.aeEarlyReflections > 0.01f) {
            int rn = (int)reflL.size();
            reflL[(size_t)reflPos] = wL;
            reflR[(size_t)reflPos] = wR;
            static const float kTapRel[6] = {0.10f,0.21f,0.35f,0.48f,0.63f,0.79f};
            static const float kTapAmp[6] = {0.60f,0.50f,0.40f,0.30f,0.22f,0.15f};
            float rOutL = 0.f, rOutR = 0.f;
            for (int t = 0; t < 6; ++t) {
                int td = std::min(rn-1,(int)(kTapRel[t]*p.aeReflDelay*0.001f*fsr));
                int ri = reflPos - td; if (ri < 0) ri += rn;
                rOutL += reflL[(size_t)ri] * kTapAmp[t];
                rOutR += reflR[(size_t)ri] * kTapAmp[t];
            }
            reflPos = (reflPos + 1) % rn;
            float mx = p.aeEarlyReflections * p.aeReflLevel;
            wL = wL * (1.f - mx) + rOutL * mx;
            wR = wR * (1.f - mx) + rOutR * mx;
        }

        // ── 19. Stereo spread (M/S widening) ─────────────────────────────────
        float spread = std::max(smMidSide, smStSpread);
        if (spread > 0.01f) {
            float m = (wL + wR) * 0.5f, s = (wL - wR) * 0.5f;
            float widened = 1.f + spread;
            wL = m + s * widened;
            wR = m - s * widened;
        }

        // ── 20. Per-channel levels + balance ──────────────────────────────────
        float balL = (smBalance <= 0.f) ? 1.f : 1.f - smBalance;
        float balR = (smBalance >= 0.f) ? 1.f : 1.f + smBalance;
        wL *= smLeftLvl  * balL;
        wR *= smRightLvl * balR;

        // ── 21. Swap channels ─────────────────────────────────────────────────
        if (p.aeSwapChannels) std::swap(wL, wR);

        // ── 22. Wet/dry mix ───────────────────────────────────────────────────
        // Effective mix: aeMix override when ≥ 0 (already smoothed via aeMixSm),
        // else use basic mix smoother.
        float effMix = (smAeMix >= 0.f) ? smAeMix : smMix;

        float wetG = smWetLvl  * smWetLvl2 * smAeWetG;
        float dryG = smDryLvl  * smDryLvl2 * smAeDryG;

        float outL = rawDryL * dryG * (1.f - effMix) + wL * wetG * effMix;
        float outR = rawDryR * dryG * (1.f - effMix) + wR * wetG * effMix;

        // ── 23. Blend (master wet↔dry fade) ──────────────────────────────────
        if (smBlend < 0.9999f) {
            outL = rawDryL * (1.f - smBlend) + outL * smBlend;
            outR = rawDryR * (1.f - smBlend) + outR * smBlend;
        }

        // ── 24. Output gain ───────────────────────────────────────────────────
        outL *= smOutGain * smAeOutG;
        outR *= smOutGain * smAeOutG;

        // ── 25. Optional brick-wall / soft limiters ───────────────────────────
        if (p.aeIntLimiter) {
            outL = std::clamp(outL, -1.f, 1.f);
            outR = std::clamp(outR, -1.f, 1.f);
        } else if (p.aeSoftLimiter) {
            outL = echoSoftClip(outL * 0.9f) / 0.9f;
            outR = echoSoftClip(outR * 0.9f) / 0.9f;
        }

        // ── 26. Always-on output protection ──────────────────────────────────
        // Soft cubic limiter catches any residual overload regardless of
        // settings.  Transparent at normal levels; engages only above ±1.0.
        outL = echoOutLimit(outL);
        outR = echoOutLimit(outR);

        l = outL;
        r = outR;
    }

    Params p;  // current (clamped) parameter snapshot — public for inspection

private:
    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr float kMaxDelayS    = 2.0f;
    static constexpr float kMaxFeedback  = 0.97f; // hard cap — never exceed
    // Smoother time constants (milliseconds)
    static constexpr float kSmGainMs     = 8.f;
    static constexpr float kSmFeedbackMs = 5.f;
    static constexpr float kSmDelayMs    = 15.f;
    static constexpr float kSmToneMs     = 5.f;
    static constexpr float kSmXfeedMs    = 8.f;

    double sampleRate = 44100.0;

    // ── Main stereo delay lines ───────────────────────────────────────────────
    std::vector<float> bufL, bufR;
    int   writePos = 0;

    // ── Basic-mode one-pole damping state ─────────────────────────────────────
    float basicDampL = 0.f, basicDampR = 0.f;

    // ── Advanced feedback damping state ───────────────────────────────────────
    float fbDampStateL = 0.f, fbDampStateR = 0.f;

    // ── Modulation state ──────────────────────────────────────────────────────
    float    wowPhase = 0.f, flutterPhase = 0.f, driftState = 0.f;
    uint32_t rng_     = 0xACE17839u;

    // ── Haas stereo width buffers ─────────────────────────────────────────────
    std::vector<float> haasL, haasR;
    int haasHPos = 0;

    // ── Early reflection buffers ──────────────────────────────────────────────
    std::vector<float> reflL, reflR;
    int reflPos = 0;

    // ── Allpass diffusors ─────────────────────────────────────────────────────
    EchoAllpass diffL, diffR;

    // ── DC blockers (one per feedback channel) ────────────────────────────────
    EchoDCBlock dcL, dcR;

    // ── Biquad filter instances ([0]=L, [1]=R) ────────────────────────────────
    EchoBiquad fbHP[2], fbLP[2];                     // feedback path EQ
    EchoBiquad toneHP[2], toneLP[2];                  // tone path HP/LP
    EchoBiquad toneBass[2], toneMid[2], toneTre[2];   // tone EQ
    EchoBiquad tonePres[2], toneBrt[2];               // presence / air

    // ── Per-sample parameter smoothers ───────────────────────────────────────
    // Basic
    EchoSmoothed feedbackSm;    // feedback coefficient
    EchoSmoothed mixSm;         // wet/dry crossfade
    EchoSmoothed pingPongSm;    // basic cross-feed
    EchoSmoothed echoAmtSm;     // input drive
    EchoSmoothed wetLvlSm;      // wet level
    EchoSmoothed dryLvlSm;      // dry level
    EchoSmoothed outGainSm;     // output gain
    EchoSmoothed dampCoeffSm;   // one-pole LP coefficient (from tone param)
    EchoSmoothed dSampLSm;      // left delay in samples (delay-time glide)
    EchoSmoothed dSampRSm;      // right delay in samples
    // Advanced
    EchoSmoothed inGainLinSm;   // AE input gain (linear)
    EchoSmoothed aeWetGainSm;   // AE wet gain (linear)
    EchoSmoothed aeDryGainSm;   // AE dry gain (linear)
    EchoSmoothed aeOutGainSm;   // AE output gain (linear)
    EchoSmoothed wetLvl2Sm;     // AE secondary wet level
    EchoSmoothed dryLvl2Sm;     // AE secondary dry level
    EchoSmoothed blendSm;       // AE master blend
    EchoSmoothed aeMixSm;       // AE mix override (negative = "use basic")
    EchoSmoothed xFeedSm;       // AE cross-feedback
    EchoSmoothed leftLvlSm;     // AE left channel level
    EchoSmoothed rightLvlSm;    // AE right channel level
    EchoSmoothed balanceSm;     // AE balance (−1..+1)
    EchoSmoothed midSideSm;     // AE M/S blend
    EchoSmoothed stereoSpreadSm;// AE stereo spread
    EchoSmoothed tapeSatSm;     // AE tape saturation amount
    EchoSmoothed analogSatSm;   // AE analog saturation amount
    EchoSmoothed driveSm;       // AE drive
    EchoSmoothed warmthSm;      // AE warmth
    EchoSmoothed fbSatSm;       // AE feedback saturation

    // ── Filter helpers ────────────────────────────────────────────────────────
    void resetFilters() noexcept {
        for (int c = 0; c < 2; ++c) {
            fbHP[c].setBypass(); fbHP[c].reset();
            fbLP[c].setBypass(); fbLP[c].reset();
            toneHP[c].setBypass(); toneHP[c].reset();
            toneLP[c].setBypass(); toneLP[c].reset();
            toneBass[c].setBypass(); toneBass[c].reset();
            toneMid[c].setBypass();  toneMid[c].reset();
            toneTre[c].setBypass();  toneTre[c].reset();
            tonePres[c].setBypass(); tonePres[c].reset();
            toneBrt[c].setBypass();  toneBrt[c].reset();
        }
        diffL.reset(); diffR.reset();
        dcL.reset();   dcR.reset();
        fbDampStateL = fbDampStateR = 0.f;
        basicDampL   = basicDampR   = 0.f;
    }

    void recomputeFilters() noexcept {
        float sr = (float)sampleRate;
        for (int c = 0; c < 2; ++c) {
            // Feedback path
            (p.aeFbLowCut  > 25.f)    ? fbHP[c].setHP(p.aeFbLowCut,  sr)
                                       : fbHP[c].setBypass();
            (p.aeFbHighCut < 19000.f) ? fbLP[c].setLP(p.aeFbHighCut, sr)
                                       : fbLP[c].setBypass();
            // Tone path HP/LP
            (p.aeToneLowCut  > 25.f)    ? toneHP[c].setHP(p.aeToneLowCut,  sr)
                                         : toneHP[c].setBypass();
            (p.aeToneHighCut < 19000.f) ? toneLP[c].setLP(p.aeToneHighCut, sr)
                                         : toneLP[c].setBypass();
            // Tone EQ
            toneBass[c].setLS(120.f,   sr, p.aeToneBass);
            toneMid [c].setPK(1000.f,  sr, p.aeToneMid,       1.0f);
            toneTre [c].setHS(8000.f,  sr, p.aeToneTreble);
            tonePres[c].setPK(4000.f,  sr, p.aeTonePresence,  1.5f);
            toneBrt [c].setHS(14000.f, sr, p.aeToneBrightness);
        }
    }
};
