#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Internal DSP helpers (header-private, zero external dependencies)
// ─────────────────────────────────────────────────────────────────────────────

// Two-pole biquad filter (Direct Form I). setBypass() produces a unity-gain
// all-pass so the tick() call is always safe with no branching in the hot path.
struct EchoBiquad {
    float x1=0,x2=0,y1=0,y2=0;
    float b0=1,b1=0,b2=0,a1=0,a2=0;

    void setLP(float f, float sr, float Q=0.7071f) {
        f = std::clamp(f, 20.f, sr*0.499f);
        float w=2.f*(float)M_PI*f/sr, cw=cosf(w), sw=sinf(w), al=sw/(2.f*Q);
        float inv=1.f/(1.f+al);
        b0=((1.f-cw)*0.5f)*inv; b1=(1.f-cw)*inv; b2=b0;
        a1=(-2.f*cw)*inv; a2=(1.f-al)*inv;
    }
    void setHP(float f, float sr, float Q=0.7071f) {
        f = std::clamp(f, 20.f, sr*0.499f);
        float w=2.f*(float)M_PI*f/sr, cw=cosf(w), sw=sinf(w), al=sw/(2.f*Q);
        float inv=1.f/(1.f+al);
        b0=((1.f+cw)*0.5f)*inv; b1=-(1.f+cw)*inv; b2=b0;
        a1=(-2.f*cw)*inv; a2=(1.f-al)*inv;
    }
    void setLS(float f, float sr, float gainDb) {
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
    void setHS(float f, float sr, float gainDb) {
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
    void setPK(float f, float sr, float gainDb, float Q=1.0f) {
        if (fabsf(gainDb)<0.05f){setBypass();return;}
        float A=powf(10.f,gainDb/40.f);
        float w=2.f*(float)M_PI*std::clamp(f,20.f,sr*0.49f)/sr;
        float cw=cosf(w),sw=sinf(w),al=sw/(2.f*Q);
        float inv=1.f/(1.f+al/A);
        b0=(1.f+al*A)*inv; b1=(-2.f*cw)*inv; b2=(1.f-al*A)*inv;
        a1=b1; a2=(1.f-al/A)*inv;
    }
    void setBypass() { b0=1;b1=b2=a1=a2=0; }
    void reset()     { x1=x2=y1=y2=0; }
    inline float tick(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return y;
    }
};

// Schroeder allpass diffusor — adds density/diffusion to the echo tail.
// Delay length and coefficient set once per params update; processing is O(1).
struct EchoAllpass {
    static constexpr int kMaxN = 2048;
    float buf[kMaxN]{};
    int   pos=0, sz=113;
    float g=0.5f;
    void init(int n, float coeff) {
        sz=std::clamp(n,1,kMaxN); g=std::clamp(coeff,0.f,0.95f);
        std::fill(buf,buf+kMaxN,0.f); pos=0;
    }
    inline float tick(float x) {
        float d=buf[pos];
        float v=x-g*d;
        buf[pos]=v;
        if (++pos>=sz) pos=0;
        return g*v+d;
    }
    void reset() { std::fill(buf,buf+kMaxN,0.f); pos=0; }
};

// Linearly-interpolated ring-buffer read at a fractional sample delay.
static inline float echoRingRead(const std::vector<float>& b, int wPos, float fracSamples) {
    int n=(int)b.size();
    int i0=wPos-(int)fracSamples; if (i0<0) i0+=n;
    int i1=i0-1;                  if (i1<0) i1+=n;
    float t=fracSamples-(float)(int)fracSamples;
    return b[(size_t)i0]*(1.f-t)+b[(size_t)i1]*t;
}

static inline float echoDB2Lin(float db)      { return powf(10.f, db*0.05f); }
static inline float echoTanhSat(float x,float a){ // a=0→bypass, a=1→full; unity-gain
    if (a<1e-3f) return x;
    float d=1.f+a*4.f; return tanhf(x*d)/d;
}
static inline float echoSoftClip(float x) {    // cubic soft-limiter at ±1
    float ax=fabsf(x);
    if (ax>=1.f) return (x>0.f)?1.f:-1.f;
    if (ax<0.667f) return x;
    float t=2.f-1.5f*ax;
    return (x>0.f?1.f:-1.f)*(1.f-t*t*t/3.f+0.037037f);
}

// ─────────────────────────────────────────────────────────────────────────────
// EchoEngine — real-time stereo feedback delay with a full Advanced signal
// chain. Parameters are divided into two tiers:
//
//   Basic (echoDelayMs … outputGain): the original 10 params used by the
//   Basic Echo UI panel on LiveTab.
//
//   Advanced (aeLeftDelayMs … aeSpatialReflDelay): 46 additional params
//   controlled by the Advanced Echo Engine collapsible on AdvancedAudioTab.
//   All advanced params default to neutral values so enabling the basic echo
//   without touching the advanced section sounds identical to the original.
//
// Signal path (one stereo frame):
//   inputGain  →  LFO-modulated fractional delay read (L/R separate delays)
//   → allpass diffusion  → fb-path saturation  → fb-path EQ  → fb damping
//   → cross-feed (ping-pong)  → delay write  →  main tone EQ  → saturation
//   → Haas stereo offset  → early reflections  → stereo spread (M/S)
//   → balance / per-channel levels  → wet/dry mix  → output gain & limiter
// ─────────────────────────────────────────────────────────────────────────────
class EchoEngine {
public:
    // ── Params ───────────────────────────────────────────────────────────────
    struct Params {
        // aeOn gates whether the advanced signal chain is active. When false,
        // the advanced params are all at neutral defaults and only the basic
        // 10-param path runs. AudioProcessor sets this from AppSettings::aeOn.
        bool  aeOn = false;

        // ── Basic Echo (LiveTab) ─────────────────────────────────────────────
        float delayMs    = 350.f;   // 1..2000 ms — master delay time
        float feedback   = 0.55f;   // 0..0.95
        float mix        = 0.38f;   // 0..1 wet/dry crossfade
        float tone       = 0.60f;   // 0..1 basic damping
        float pingPong   = 0.15f;   // 0..1 stereo cross-feed
        int   numEchoes  = 0;       // 0=∞ feedback, 1-10=discrete taps
        float echoAmount = 1.0f;    // 0..1 input drive into delay
        float wetLevel   = 1.0f;    // 0..1 wet signal level
        float dryLevel   = 1.0f;    // 0..1 dry signal level
        float outputGain = 1.0f;    // 0..4 post-mix gain

        // ── Advanced Echo Engine (AdvancedAudioTab) ─────────────────────────
        // [Delay]
        float aeLeftDelayMs   = 350.f;  // independent left channel delay (ms)
        float aeRightDelayMs  = 350.f;  // independent right channel delay (ms)
        float aeStereoOffset  = 0.f;    // extra R–L offset in ms (±200)
        float aeStereoWidthD  = 0.f;    // 0..1 widen the delay imaging
        bool  aeTempoSync     = false;  // (UI hint; quantisation done in UI)
        bool  aeMillisecondMode = true; // display preference only

        // [Feedback]
        float aeCrossFeedback  = 0.f;     // 0..1 L→R, R→L cross-feed (in addition to pingPong)
        float aeFbSaturation   = 0.f;     // 0..1 saturation in feedback path
        float aeFbDamping      = 0.f;     // 0..1 extra HF damp on feedback
        float aeFbLowCut       = 20.f;    // Hz HPF on feedback path
        float aeFbHighCut      = 20000.f; // Hz LPF on feedback path
        float aeFbDiffusion    = 0.f;     // 0..1 allpass diffusion amount

        // [Stereo]
        float aeBalance     = 0.f;    // -1..+1 L/R pan
        float aeLeftLevel   = 1.0f;   // 0..1 left wet level
        float aeRightLevel  = 1.0f;   // 0..1 right wet level
        float aeMidSideMix  = 0.f;    // 0..1 M/S width blend on wet
        bool  aePingPongMode = false; // hard ping-pong (overrides pingPong)
        bool  aeSwapChannels = false; // swap L/R of wet output

        // [Tone]
        float aeToneLowCut    = 20.f;     // Hz HPF on wet
        float aeToneHighCut   = 20000.f;  // Hz LPF on wet
        float aeToneBass      = 0.f;      // dB low shelf @120Hz
        float aeToneMid       = 0.f;      // dB peak @1kHz
        float aeToneTreble    = 0.f;      // dB high shelf @8kHz
        float aeTonePresence  = 0.f;      // dB peak @4kHz
        float aeToneBrightness= 0.f;      // dB high shelf @14kHz

        // [Saturation]
        float aeTapeSat    = 0.f;   // 0..1 tape-style even-harmonic saturation
        float aeAnalogSat  = 0.f;   // 0..1 tanh odd-harmonic saturation
        float aeDrive      = 0.f;   // 0..1 pre-saturation gain boost
        float aeWarmth     = 0.f;   // 0..1 low-end harmonic enhancement
        bool  aeSoftClip   = false; // cubic soft-limiter on wet output

        // [Dynamics]
        float aeInputGainDb  = 0.f;   // dB pre-delay input gain
        float aeOutputGainDb = 0.f;   // dB post-mix output gain
        float aeWetGainDb    = 0.f;   // dB gain on wet bus before mix
        float aeDryGainDb    = 0.f;   // dB gain on dry bus before mix
        bool  aeIntLimiter   = false; // brick-wall at 0 dBFS on output
        bool  aeSoftLimiter  = false; // soft-knee limiter at -3 dBFS

        // [Mix]
        float aeWetLevel2 = 1.0f;   // secondary wet level (multiplied with wetLevel)
        float aeDryLevel2 = 1.0f;   // secondary dry level (multiplied with dryLevel)
        float aeBlend     = 1.0f;   // 0..1 master blend (fades whole effect to dry)
        float aeMix       = -1.f;   // ≥0 overrides `mix`; <0 → use basic `mix`

        // [Modulation]
        float aeWow         = 0.f;  // 0..1 slow pitch/time waver (~1 Hz)
        float aeFlutter     = 0.f;  // 0..1 fast flutter (~12 Hz)
        float aeModDepth    = 0.f;  // 0..1 general modulation depth
        float aeModRate     = 1.f;  // Hz LFO rate (wow uses this; flutter ×12)
        float aeRandomDrift = 0.f;  // 0..1 stochastic delay drift

        // [Spatial]
        float aeHaasWidth       = 0.f;   // ms channel offset (Haas effect, 0-40)
        float aeStereoSpread    = 0.f;   // 0..1 M/S widening on final output
        float aeEarlyReflections= 0.f;  // 0..1 early reflection mix level
        float aeReflLevel       = 0.f;   // 0..1 reflection amplitude
        float aeReflDelay       = 20.f;  // ms base delay for reflections
    };

    // ── Construction ─────────────────────────────────────────────────────────
    EchoEngine() { init(44100.0); }

    void init(double sr) {
        sampleRate = sr;
        int mainMax = (int)(kMaxDelayS * sr) + 8;
        bufL.assign((size_t)mainMax, 0.f);
        bufR.assign((size_t)mainMax, 0.f);
        writePos = 0;
        dampL = dampR = 0.f;
        wowPhase = flutterPhase = 0.f;
        driftState = 0.f;
        rng_ = 0xACE17839u;

        // Haas buffer: max 50 ms
        int haasMax = (int)(0.050 * sr) + 4;
        haasL.assign((size_t)haasMax, 0.f); haasHPos = 0;
        haasR.assign((size_t)haasMax, 0.f);

        // Early reflection buffer: max 120 ms
        int reflMax = (int)(0.120 * sr) + 4;
        reflL.assign((size_t)reflMax, 0.f); reflPos = 0;
        reflR.assign((size_t)reflMax, 0.f);

        // Allpass diffusors at prime-ish lengths for diffuse character
        int diffSz = std::max(1, (int)(0.007*sr)); // ~7 ms
        diffL.init(diffSz, 0.5f);
        diffR.init(diffSz, 0.5f);

        resetFilters();
        recomputeFilters();
    }

    void setSampleRate(double sr) { init(sr); }

    void setParams(const Params& newP) {
        p = newP;
        // Clamp basic params
        p.delayMs    = std::clamp(p.delayMs, 1.f, kMaxDelayS*1000.f);
        p.feedback   = std::clamp(p.feedback, 0.f, 0.95f);
        p.mix        = std::clamp(p.mix, 0.f, 1.f);
        p.tone       = std::clamp(p.tone, 0.f, 1.f);
        p.pingPong   = std::clamp(p.pingPong, 0.f, 1.f);
        p.numEchoes  = std::clamp(p.numEchoes, 0, 10);
        p.echoAmount = std::clamp(p.echoAmount, 0.f, 1.f);
        p.wetLevel   = std::clamp(p.wetLevel, 0.f, 1.f);
        p.dryLevel   = std::clamp(p.dryLevel, 0.f, 1.f);
        p.outputGain = std::clamp(p.outputGain, 0.f, 4.f);
        // Clamp advanced params
        p.aeLeftDelayMs  = std::clamp(p.aeLeftDelayMs,  1.f, kMaxDelayS*1000.f);
        p.aeRightDelayMs = std::clamp(p.aeRightDelayMs, 1.f, kMaxDelayS*1000.f);
        p.aeStereoOffset = std::clamp(p.aeStereoOffset, -200.f, 200.f);
        p.aeStereoWidthD = std::clamp(p.aeStereoWidthD, 0.f, 1.f);
        p.aeCrossFeedback= std::clamp(p.aeCrossFeedback, 0.f, 1.f);
        p.aeFbSaturation = std::clamp(p.aeFbSaturation, 0.f, 1.f);
        p.aeFbDamping    = std::clamp(p.aeFbDamping, 0.f, 1.f);
        p.aeFbLowCut     = std::clamp(p.aeFbLowCut, 20.f, 18000.f);
        p.aeFbHighCut    = std::clamp(p.aeFbHighCut, 200.f, 20000.f);
        p.aeFbDiffusion  = std::clamp(p.aeFbDiffusion, 0.f, 1.f);
        p.aeBalance      = std::clamp(p.aeBalance, -1.f, 1.f);
        p.aeLeftLevel    = std::clamp(p.aeLeftLevel, 0.f, 2.f);
        p.aeRightLevel   = std::clamp(p.aeRightLevel, 0.f, 2.f);
        p.aeMidSideMix   = std::clamp(p.aeMidSideMix, 0.f, 1.f);
        p.aeToneLowCut   = std::clamp(p.aeToneLowCut, 20.f, 18000.f);
        p.aeToneHighCut  = std::clamp(p.aeToneHighCut, 200.f, 20000.f);
        p.aeWow          = std::clamp(p.aeWow, 0.f, 1.f);
        p.aeFlutter      = std::clamp(p.aeFlutter, 0.f, 1.f);
        p.aeModDepth     = std::clamp(p.aeModDepth, 0.f, 1.f);
        p.aeModRate      = std::clamp(p.aeModRate, 0.01f, 20.f);
        p.aeRandomDrift  = std::clamp(p.aeRandomDrift, 0.f, 1.f);
        p.aeHaasWidth    = std::clamp(p.aeHaasWidth, 0.f, 40.f);
        p.aeStereoSpread = std::clamp(p.aeStereoSpread, 0.f, 1.f);
        p.aeEarlyReflections = std::clamp(p.aeEarlyReflections, 0.f, 1.f);
        p.aeReflLevel    = std::clamp(p.aeReflLevel, 0.f, 1.f);
        p.aeReflDelay    = std::clamp(p.aeReflDelay, 1.f, 100.f);
        p.aeWetLevel2    = std::clamp(p.aeWetLevel2, 0.f, 1.f);
        p.aeDryLevel2    = std::clamp(p.aeDryLevel2, 0.f, 1.f);
        p.aeBlend        = std::clamp(p.aeBlend, 0.f, 1.f);
        // Update allpass diffusor gain
        diffL.g = diffR.g = p.aeFbDiffusion * 0.7f;
        recomputeFilters();
    }

    void reset() { init(sampleRate); }

    // ── Main processing entry-point ───────────────────────────────────────────
    void processStereo(float& l, float& r) {
        const int n = (int)bufL.size();

        // ── 1. Input gain ────────────────────────────────────────────────────
        float inL = l * echoDB2Lin(p.aeInputGainDb);
        float inR = r * echoDB2Lin(p.aeInputGainDb);
        // Save untouched dry for later
        float rawDryL = l, rawDryR = r;

        // ── 2. Modulation: compute fractional delay offsets ──────────────────
        // Wow: slow LFO on overall delay time (sinusoidal pitch flutter)
        float wowFreq    = p.aeModRate;
        float flutFreq   = p.aeModRate * 12.f;
        float wowInc     = wowFreq    / (float)sampleRate;
        float flutInc    = flutFreq   / (float)sampleRate;
        wowPhase    += wowInc;    if (wowPhase>1.f)    wowPhase-=1.f;
        flutterPhase+= flutInc;   if (flutterPhase>1.f) flutterPhase-=1.f;
        float wowLFO  = sinf(wowPhase    * 2.f*(float)M_PI);
        float flutLFO = sinf(flutterPhase* 2.f*(float)M_PI);

        // Random drift: simple LFSR-based walk
        rng_ = rng_*1664525u + 1013904223u;
        float noise = ((float)(int)(rng_ >> 16) / 32767.5f - 1.f); // −1..+1
        driftState  = driftState*0.9998f + noise*0.0002f;

        float modOffset = // total sample-count perturbation
              wowLFO    * p.aeWow      * p.aeModDepth * 0.015f * (float)sampleRate
            + flutLFO   * p.aeFlutter  * p.aeModDepth * 0.003f * (float)sampleRate
            + driftState* p.aeRandomDrift * 0.005f    * (float)sampleRate;

        // ── 3. Compute delay times in samples (L and R independently) ────────
        // Basic mode: both channels use the same delayMs.
        // Advanced (aeOn): use per-channel aeLeftDelayMs / aeRightDelayMs
        // plus stereoOffset (which is 0 at default).
        float leftMs  = p.aeOn ? p.aeLeftDelayMs                  : p.delayMs;
        float rightMs = p.aeOn ? p.aeRightDelayMs + p.aeStereoOffset : p.delayMs;

        float dSampL = std::clamp(leftMs  * 0.001f * (float)sampleRate + modOffset,
                                  1.f, (float)(n-1));
        float dSampR = std::clamp(rightMs * 0.001f * (float)sampleRate + modOffset,
                                  1.f, (float)(n-1));

        // ── 4. Read from delay buffers (fractional interp) ───────────────────
        float readL, readR;
        if (p.numEchoes == 0) {
            // Infinite feedback mode — fractional read for modulation
            readL = echoRingRead(bufL, writePos, dSampL);
            readR = echoRingRead(bufR, writePos, dSampR);
        } else {
            // Discrete N-tap mode — sum N taps
            float sumL=0.f, sumR=0.f, scale=1.f;
            int dInt = std::max(1, (int)dSampL);
            for (int i=1; i<=p.numEchoes; ++i) {
                int ri=writePos-i*dInt; while(ri<0) ri+=n;
                sumL+=bufL[(size_t)ri]*scale;
                sumR+=bufR[(size_t)ri]*scale;
                scale*=p.feedback;
            }
            readL=sumL; readR=sumR;
        }

        // ── 5. Allpass diffusion ─────────────────────────────────────────────
        if (p.aeFbDiffusion > 0.01f) {
            readL = diffL.tick(readL);
            readR = diffR.tick(readR);
        }

        // ── 6. Feedback-path saturation ──────────────────────────────────────
        // Basic tone darkening (one-pole LP, original behavior)
        float basicDamp = 0.05f + p.tone * 0.85f;
        dampL += (readL - dampL) * (1.f - basicDamp);
        dampR += (readR - dampR) * (1.f - basicDamp);

        // Advanced feedback path saturation (applied to the damped signal)
        float fbSigL = dampL, fbSigR = dampR;
        if (p.aeFbSaturation > 0.01f) {
            fbSigL = echoTanhSat(fbSigL * (1.f + p.aeDrive), p.aeFbSaturation);
            fbSigR = echoTanhSat(fbSigR * (1.f + p.aeDrive), p.aeFbSaturation);
        }

        // ── 7. Feedback-path EQ (LP/HP) ──────────────────────────────────────
        // Advanced feedback damping (one-pole LP on top of basic damp)
        if (p.aeFbDamping > 0.01f) {
            float xtra = 0.05f + p.aeFbDamping * 0.85f;
            fbDampStateL += (fbSigL - fbDampStateL) * (1.f - xtra);
            fbDampStateR += (fbSigR - fbDampStateR) * (1.f - xtra);
            fbSigL = fbDampStateL;
            fbSigR = fbDampStateR;
        }
        fbSigL = fbHP[0].tick(fbSigL);
        fbSigR = fbHP[1].tick(fbSigR);
        fbSigL = fbLP[0].tick(fbSigL);
        fbSigR = fbLP[1].tick(fbSigR);

        // ── 8. Cross-feed (ping-pong + advanced cross-feedback) ───────────────
        float pp    = p.aePingPongMode ? 1.f : p.pingPong;
        float cross = std::clamp(pp + p.aeCrossFeedback, 0.f, 1.f);
        float wL    = fbSigL*(1.f-cross) + fbSigR*cross;
        float wR    = fbSigR*(1.f-cross) + fbSigL*cross;

        // ── 9. Write back to delay (with echoAmount scaling on input) ─────────
        float echoIn = p.echoAmount;
        if (p.numEchoes == 0) {
            bufL[(size_t)writePos] = inL*echoIn + wL*p.feedback;
            bufR[(size_t)writePos] = inR*echoIn + wR*p.feedback;
        } else {
            bufL[(size_t)writePos] = inL*echoIn;
            bufR[(size_t)writePos] = inR*echoIn;
        }
        writePos = (writePos+1) % n;

        // From here: `wL/wR` is the wet signal to shape and mix.

        // ── 10. Main tone EQ (7-band: HP, LP, bass, mid, treble, presence, air)
        wL = toneHP[0].tick(wL); wR = toneHP[1].tick(wR);
        wL = toneLP[0].tick(wL); wR = toneLP[1].tick(wR);
        wL = toneBass[0].tick(wL); wR = toneBass[1].tick(wR);
        wL = toneMid[0].tick(wL);  wR = toneMid[1].tick(wR);
        wL = toneTreble[0].tick(wL);   wR = toneTreble[1].tick(wR);
        wL = tonePresence[0].tick(wL); wR = tonePresence[1].tick(wR);
        wL = toneBright[0].tick(wL);   wR = toneBright[1].tick(wR);

        // ── 11. Output saturation (tape + analog + drive + warmth) ───────────
        float totalSat = p.aeTapeSat * 0.6f + p.aeAnalogSat * 0.4f;
        if (totalSat > 0.01f || p.aeDrive > 0.01f) {
            float drv = 1.f + p.aeDrive * 3.f;
            wL *= drv; wR *= drv;
            // Tape: even-harmonic (x² bias) — warm, smooth
            if (p.aeTapeSat > 0.01f) {
                wL = wL - p.aeTapeSat * 0.08f * wL*wL*wL;
                wR = wR - p.aeTapeSat * 0.08f * wR*wR*wR;
            }
            // Analog: odd-harmonic (tanh) — adds grit and bite
            if (p.aeAnalogSat > 0.01f) {
                wL = echoTanhSat(wL, p.aeAnalogSat);
                wR = echoTanhSat(wR, p.aeAnalogSat);
            }
            // Warmth: low-end boost bias (adds even-order bass harmonics)
            if (p.aeWarmth > 0.01f) {
                wL = wL + p.aeWarmth * 0.05f * wL * fabsf(wL);
                wR = wR + p.aeWarmth * 0.05f * wR * fabsf(wR);
            }
            // Compensate drive gain
            if (drv > 1.f) { wL /= drv; wR /= drv; }
        }
        if (p.aeSoftClip) { wL=echoSoftClip(wL); wR=echoSoftClip(wR); }

        // ── 12. Haas stereo width (small channel delay for L–R spaciousness) ──
        if (p.aeHaasWidth > 0.01f) {
            int hn = (int)haasL.size();
            haasL[(size_t)haasHPos] = wL;
            haasR[(size_t)haasHPos] = wR;
            // Delay the RIGHT channel by haasWidth ms relative to left
            int hd = std::min(hn-1, (int)(p.aeHaasWidth * 0.001f * (float)sampleRate));
            int hri = haasHPos - hd; if (hri<0) hri+=hn;
            wR = haasR[(size_t)hri]; // read delayed right
            haasHPos = (haasHPos+1) % hn;
        }

        // ── 13. Early reflections (6-tap fixed pattern) ─────────────────────
        if (p.aeEarlyReflections > 0.01f) {
            int rn = (int)reflL.size();
            reflL[(size_t)reflPos] = wL;
            reflR[(size_t)reflPos] = wR;
            // Fixed tap pattern: relative ms offsets × aeReflDelay scale
            static const float kTapRel[6] = {0.10f,0.21f,0.35f,0.48f,0.63f,0.79f};
            static const float kTapAmp[6] = {0.60f,0.50f,0.40f,0.30f,0.22f,0.15f};
            float reflOutL=0.f, reflOutR=0.f;
            for (int t=0; t<6; ++t) {
                int td = std::min(rn-1, (int)(kTapRel[t]*p.aeReflDelay*0.001f*(float)sampleRate));
                int ri = reflPos-td; if (ri<0) ri+=rn;
                reflOutL += reflL[(size_t)ri]*kTapAmp[t];
                reflOutR += reflR[(size_t)ri]*kTapAmp[t];
            }
            reflPos = (reflPos+1) % rn;
            float mix = p.aeEarlyReflections * p.aeReflLevel;
            wL = wL*(1.f-mix) + reflOutL*mix;
            wR = wR*(1.f-mix) + reflOutR*mix;
        }

        // ── 14. Stereo spread (M/S widening on wet) ──────────────────────────
        if (p.aeMidSideMix > 0.01f || p.aeStereoSpread > 0.01f) {
            float spread = std::max(p.aeMidSideMix, p.aeStereoSpread);
            float m = (wL+wR)*0.5f, s = (wL-wR)*0.5f;
            float widened = 1.f + spread;
            wL = m + s*widened;
            wR = m - s*widened;
        }

        // ── 15. Per-channel levels + balance ─────────────────────────────────
        // balance: -1→full left, 0→center, +1→full right
        float balL = (p.aeBalance <= 0.f) ? 1.f : 1.f - p.aeBalance;
        float balR = (p.aeBalance >= 0.f) ? 1.f : 1.f + p.aeBalance;
        wL *= p.aeLeftLevel  * balL;
        wR *= p.aeRightLevel * balR;

        // ── 16. Swap channels ─────────────────────────────────────────────────
        if (p.aeSwapChannels) std::swap(wL, wR);

        // ── 17. Wet/dry mix ───────────────────────────────────────────────────
        // Effective mix: override from advanced if set (aeMix >= 0), else basic
        float effMix = (p.aeMix >= 0.f) ? p.aeMix : p.mix;

        // Wet and dry gains
        float wetG = p.wetLevel * p.aeWetLevel2 * echoDB2Lin(p.aeWetGainDb);
        float dryG = p.dryLevel * p.aeDryLevel2 * echoDB2Lin(p.aeDryGainDb);

        float outL = rawDryL*dryG*(1.f-effMix) + wL*wetG*effMix;
        float outR = rawDryR*dryG*(1.f-effMix) + wR*wetG*effMix;

        // ── 18. Blend (master wet↔dry fade without altering mix ratio) ───────
        if (p.aeBlend < 0.9999f) {
            outL = rawDryL*(1.f-p.aeBlend) + outL*p.aeBlend;
            outR = rawDryR*(1.f-p.aeBlend) + outR*p.aeBlend;
        }

        // ── 19. Output gain ───────────────────────────────────────────────────
        float outGain = p.outputGain * echoDB2Lin(p.aeOutputGainDb);
        outL *= outGain; outR *= outGain;

        // ── 20. Limiter ───────────────────────────────────────────────────────
        if (p.aeIntLimiter) {
            outL = std::clamp(outL, -1.f, 1.f);
            outR = std::clamp(outR, -1.f, 1.f);
        } else if (p.aeSoftLimiter) {
            // Soft knee at −3 dBFS (≈0.708) using cubic
            outL = echoSoftClip(outL * 0.9f) / 0.9f;
            outR = echoSoftClip(outR * 0.9f) / 0.9f;
        }

        l = outL; r = outR;
    }

    Params p;

private:
    static constexpr float kMaxDelayS = 2.0f;
    double sampleRate = 44100.0;

    // Main stereo delay lines
    std::vector<float> bufL, bufR;
    int   writePos = 0;

    // Basic-mode damping state
    float dampL = 0.f, dampR = 0.f;

    // Advanced feedback damping state
    float fbDampStateL = 0.f, fbDampStateR = 0.f;

    // Modulation state
    float wowPhase = 0.f, flutterPhase = 0.f, driftState = 0.f;
    uint32_t rng_ = 0xACE17839u;

    // Haas effect buffers (for stereo width)
    std::vector<float> haasL, haasR;
    int haasHPos = 0;

    // Early reflection buffers
    std::vector<float> reflL, reflR;
    int reflPos = 0;

    // Allpass diffusors (one per channel)
    EchoAllpass diffL, diffR;

    // Filter instances (indexed [0]=L [1]=R for each band)
    EchoBiquad fbHP[2], fbLP[2];                           // feedback EQ
    EchoBiquad toneHP[2], toneLP[2];                       // tone HP/LP
    EchoBiquad toneBass[2], toneMid[2], toneTreble[2];     // tone EQ
    EchoBiquad tonePresence[2], toneBright[2];             // tone presence/air

    void resetFilters() {
        for (int c=0;c<2;++c) {
            fbHP[c].setBypass(); fbHP[c].reset();
            fbLP[c].setBypass(); fbLP[c].reset();
            toneHP[c].setBypass(); toneHP[c].reset();
            toneLP[c].setBypass(); toneLP[c].reset();
            toneBass[c].setBypass(); toneBass[c].reset();
            toneMid[c].setBypass();  toneMid[c].reset();
            toneTreble[c].setBypass(); toneTreble[c].reset();
            tonePresence[c].setBypass(); tonePresence[c].reset();
            toneBright[c].setBypass();   toneBright[c].reset();
        }
        diffL.reset(); diffR.reset();
        fbDampStateL = fbDampStateR = 0.f;
        dampL = dampR = 0.f;
    }

    void recomputeFilters() {
        float sr = (float)sampleRate;
        for (int c=0;c<2;++c) {
            // Feedback path
            if (p.aeFbLowCut > 25.f)
                fbHP[c].setHP(p.aeFbLowCut, sr);
            else fbHP[c].setBypass();
            if (p.aeFbHighCut < 19000.f)
                fbLP[c].setLP(p.aeFbHighCut, sr);
            else fbLP[c].setBypass();

            // Main tone EQ
            if (p.aeToneLowCut > 25.f)
                toneHP[c].setHP(p.aeToneLowCut, sr);
            else toneHP[c].setBypass();
            if (p.aeToneHighCut < 19000.f)
                toneLP[c].setLP(p.aeToneHighCut, sr);
            else toneLP[c].setBypass();

            toneBass[c].setLS(120.f,  sr, p.aeToneBass);
            toneMid[c].setPK(1000.f, sr, p.aeToneMid, 1.0f);
            toneTreble[c].setHS(8000.f, sr, p.aeToneTreble);
            tonePresence[c].setPK(4000.f, sr, p.aeTonePresence, 1.5f);
            toneBright[c].setHS(14000.f, sr, p.aeToneBrightness);
        }
    }
};
