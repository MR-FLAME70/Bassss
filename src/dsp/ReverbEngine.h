#pragma once
#include "BiquadFilter.h"
#include "FDNReverb.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <array>

// ──────────────────────────────────────────────────────────────────────────────
// ReverbEngine — exact C++ port of createHybridReverbEngine() in reverb-engine.js.
//
// Signal chain (identical to the original Web Audio graph):
//   input → preDelay → ┬→ erDelay → erConvolver → erGain ─┐
//                       └→ FDN late tail          → lateGain ┤→ wetBus
//   wetBus → hfDampShelf(highshelf) → lfDampShelf(lowshelf) → lowCut(HP)
//          → highCut(LP) → M/S width → wetLevel ─┐
//   input → dryLevel ──────────────────────────────┴→ output
//
// Early reflections use a *true* FIR convolution against a synthesized stereo
// "cave" impulse response (makeCaveEarlyReflectionIR), reproduced bit-for-bit
// from the original algorithm: a deterministic mulberry32 PRNG, golden-ratio
// tap scatter, per-channel decorrelated one-pole-lowpassed noise bursts, an
// exponential decay envelope, and peak normalization to 0.9. This replaces a
// prior sparse-tap-delay approximation that did not match the original sound.
//
// Per the Web Audio ConvolverNode spec (2-channel IR × 2-channel input is
// handled as two independent per-channel convolutions — verified against the
// Chromium/Blink Reverb implementation, case "2 -> 2 -> 2"), channel L is
// convolved with impulse channel 0 and channel R with impulse channel 1;
// there is no cross-channel mixing.
// ──────────────────────────────────────────────────────────────────────────────
class ReverbEngine {
public:
    struct Params {
        float preDelay              = 0.015f;  // s
        float earlyReflectionDelay  = 0.f;     // s  (additional ER pre-delay)
        float earlyReflectionLevel  = 0.35f;   // linear
        float lateReverbLevel       = 1.0f;    // linear
        float wetLevel              = 1.0f;    // linear
        float dryLevel              = 0.0f;    // linear
        float highCut               = 9000.f;  // Hz
        float lowCut                = 80.f;    // Hz
        float hfDamping             = 0.f;     // 0..1
        float lfDamping             = 0.f;     // 0..1
        float stereoWidth           = 1.0f;    // 0..2
        float roomSize              = 1.0f;
        float decayTime             = 1.8f;
        float diffusion             = 0.7f;
        float density               = 0.5f;
        float modulationDepth       = 0.4f;
        float modulationRate        = 0.35f;
    };

    ReverbEngine() { init(44100.0); }
    ~ReverbEngine() = default;

    void init(double sr) {
        sampleRate = sr;

        fdn.init(sr);

        hfDampShelf .setType(BiquadFilter::HighShelf, sr, 6000.0, 0.707, 0.0);
        lfDampShelf .setType(BiquadFilter::LowShelf,  sr, 200.0,  0.707, 0.0);
        lowCutFilter .setType(BiquadFilter::HighPass, sr, 80.0,   0.707, 0.0);
        highCutFilter.setType(BiquadFilter::LowPass,  sr, 9000.0, 0.707, 0.0);

        // Pre-delay buffer (up to 2 s, matches ctx.createDelay(2) in the original)
        int pdMax = (int)(2.0 * sr) + 4;
        preDelayBufL.assign(pdMax, 0.f);
        preDelayBufR.assign(pdMax, 0.f);
        preDelayWrite = 0;

        // ER additional delay (up to 0.5 s, matches ctx.createDelay(0.5))
        int erMax = (int)(0.5 * sr) + 4;
        erDelayBufL.assign(erMax, 0.f);
        erDelayBufR.assign(erMax, 0.f);
        erDelayWrite = 0;

        // Force a fresh IR build for the current room parameters.
        erImpulseKeyValid = false;
        rebuildErImpulseIfNeeded(p.roomSize, p.diffusion);
    }

    void setSampleRate(double sr) { init(sr); }

    void setParams(const Params& newP) {
        p = newP;

        // Clamps mirror setParams() in reverb-engine.js / parameterDescriptors
        // in fdn-reverb-worklet.js.
        p.preDelay             = std::max(0.f, p.preDelay);
        p.earlyReflectionDelay = std::max(0.f, p.earlyReflectionDelay);
        p.earlyReflectionLevel = std::max(0.f, p.earlyReflectionLevel);
        p.lateReverbLevel      = std::max(0.f, p.lateReverbLevel);
        p.wetLevel              = std::max(0.f, p.wetLevel);
        p.dryLevel              = std::max(0.f, p.dryLevel);
        p.highCut     = clampf(p.highCut, 200.f, 20000.f);
        p.lowCut      = clampf(p.lowCut,   20.f,  2000.f);
        p.hfDamping   = clampf(p.hfDamping, 0.f, 1.f);
        p.lfDamping   = clampf(p.lfDamping, 0.f, 1.f);
        p.stereoWidth = clampf(p.stereoWidth, 0.f, 2.f);
        p.roomSize    = clampf(p.roomSize, 0.25f, 3.0f);
        p.decayTime   = clampf(p.decayTime, 0.1f, 25.0f);
        p.diffusion   = clampf(p.diffusion, 0.f, 1.f);
        p.density     = clampf(p.density, 0.f, 1.f);
        p.modulationDepth = clampf(p.modulationDepth, 0.f, 1.f);
        p.modulationRate  = clampf(p.modulationRate, 0.f, 1.f);

        rebuildErImpulseIfNeeded(p.roomSize, p.diffusion);

        FDNReverb::Params fdnP;
        fdnP.roomSize  = p.roomSize;
        fdnP.decayTime = p.decayTime;
        fdnP.diffusion = p.diffusion;
        fdnP.density   = p.density;
        fdnP.hfDamping = p.hfDamping;
        fdnP.lfDamping = p.lfDamping;
        fdnP.modDepth  = p.modulationDepth;
        fdnP.modRate   = p.modulationRate;
        fdn.setParams(fdnP);

        hfDampShelf .setGain(-p.hfDamping * 12.f);
        lfDampShelf .setGain(-p.lfDamping *  6.f);
        lowCutFilter .setFrequency(p.lowCut);
        highCutFilter.setFrequency(p.highCut);
    }

    // Process one stereo frame
    void processStereo(float inL, float inR, float& outL, float& outR) {
        // Pre-delay
        float pdL = readDelay(preDelayBufL, preDelayWrite, preDelaySamples());
        float pdR = readDelay(preDelayBufR, preDelayWrite, preDelaySamples());
        writeDelay(preDelayBufL, preDelayWrite, inL);
        writeDelay(preDelayBufR, preDelayWrite, inR);

        // ER additional pre-delay (parallel branch off the shared pre-delay)
        float erInL = readDelay(erDelayBufL, erDelayWrite, erDelaySamples());
        float erInR = readDelay(erDelayBufR, erDelayWrite, erDelaySamples());
        writeDelay(erDelayBufL, erDelayWrite, pdL);
        writeDelay(erDelayBufR, erDelayWrite, pdR);

        // Early reflections — true FIR convolution against the synthesized
        // stereo cave impulse response (independent per channel).
        float convL = erConvL.process(erInL) * p.earlyReflectionLevel;
        float convR = erConvR.process(erInR) * p.earlyReflectionLevel;

        // Late tail (FDN), fed directly from the shared pre-delay (parallel
        // with the ER branch, not chained through erDelay).
        float lateL = 0.f, lateR = 0.f;
        fdn.processStereo(pdL, pdR, lateL, lateR);
        lateL *= p.lateReverbLevel;
        lateR *= p.lateReverbLevel;

        // Wet bus sum
        float wetL = convL + lateL;
        float wetR = convR + lateR;

        // Spectral shaping on wet bus
        hfDampShelf .processStereo(wetL, wetR);
        lfDampShelf .processStereo(wetL, wetR);
        lowCutFilter .processStereo(wetL, wetR);
        highCutFilter.processStereo(wetL, wetR);

        // M/S stereo width
        applyWidth(wetL, wetR, p.stereoWidth);

        // Engine-level wet/dry mix
        outL = wetL * p.wetLevel + inL * p.dryLevel;
        outR = wetR * p.wetLevel + inR * p.dryLevel;
    }

    void reset() { init(sampleRate); }

    Params p;

private:
    double sampleRate = 44100.0;
    FDNReverb    fdn;
    BiquadFilter hfDampShelf, lfDampShelf, lowCutFilter, highCutFilter;

    std::vector<float> preDelayBufL, preDelayBufR;
    int preDelayWrite = 0;

    std::vector<float> erDelayBufL, erDelayBufR;
    int erDelayWrite = 0;

    static float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

    // ── Delay helpers ──────────────────────────────────────────────────────────
    int preDelaySamples() const {
        return std::min((int)(p.preDelay * (float)sampleRate),
                        (int)preDelayBufL.size() - 1);
    }
    int erDelaySamples() const {
        return std::min((int)(p.earlyReflectionDelay * (float)sampleRate),
                        (int)erDelayBufL.size() - 1);
    }

    static float readDelay(const std::vector<float>& buf, int writeIdx, int delay) {
        if (delay <= 0 || buf.empty()) return 0.f;
        int n   = (int)buf.size();
        // delay is always < n by construction, so idx is at most one buffer
        // length negative — a single conditional add is enough. The extra
        // `% n` that used to follow the wrap loop was always a no-op (idx is
        // already in [0, n) at that point) but still cost a division every
        // call — this runs 4x per sample (pre-delay L/R, ER-delay L/R).
        int idx = writeIdx - delay;
        if (idx < 0) idx += n;
        return buf[idx];
    }
    static void writeDelay(std::vector<float>& buf, int& writeIdx, float v) {
        if (buf.empty()) return;
        buf[writeIdx] = v;
        writeIdx = (writeIdx + 1) % (int)buf.size();
    }

    // ── M/S stereo width ──────────────────────────────────────────────────────
    // out = M ± S, M = 0.5*(L+R), S = 0.5*(L-R)*width  (matches the original
    // exactly, including that width only scales the side signal).
    static void applyWidth(float& l, float& r, float width) {
        float mid  = 0.5f * (l + r);
        float side = 0.5f * (l - r);
        side *= width;
        l = mid + side;
        r = mid - side;
    }

    // ══════════════════════════════════════════════════════════════════════════
    // Early-reflection convolution engine
    // ══════════════════════════════════════════════════════════════════════════

    // mulberry32 — bit-for-bit port of makeSeededRandom() in reverb-engine.js.
    // JS bitwise ops treat numbers as 32-bit values; uint32_t arithmetic here
    // reproduces the exact same bit patterns (unsigned wraparound == JS |0
    // wraparound for XOR/shift/multiply purposes).
    struct Mulberry32 {
        uint32_t a;
        explicit Mulberry32(uint32_t seed) : a(seed) {}
        float next() {
            a = a + 0x6d2b79f5u;
            uint32_t t = (a ^ (a >> 15)) * (1u | a);
            t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
            uint32_t r = t ^ (t >> 14);
            return (float)((double)r / 4294967296.0);
        }
    };

    // Direct FIR convolution line using the classic "doubled circular buffer"
    // trick: each incoming sample is mirrored at pos and pos+M, so the last M
    // samples are always readable as one contiguous descending run with no
    // per-tap modulo. Mathematically identical to Web Audio's ConvolverNode
    // linear convolution (given a freshly (re)built impulse each parameter
    // change, matching the original's "assign a new buffer" behavior).
    struct ConvLine {
        std::vector<float> ir;
        std::vector<float> hist;
        int M   = 1;
        int pos = 0;

        void setImpulse(std::vector<float> newIr) {
            ir = std::move(newIr);
            if (ir.empty()) ir.assign(1, 0.f);
            M = (int)ir.size();
            hist.assign((size_t)M * 2, 0.f);
            pos = 0;
        }

        // Four independent accumulator chains (same math, reordered summation)
        // let the compiler auto-vectorize this hot loop under normal strict
        // floating-point settings (no -ffast-math needed) — this keeps the
        // early-reflection convolution comfortably real-time even with IRs
        // approaching 0.28s at high sample rates. This does not change the
        // convolution algorithm, only the order partial sums are added in
        // (a difference on the order of float rounding noise, inaudible).
        inline float process(float x) {
            hist[pos]     = x;
            hist[pos + M] = x;
            const int base = pos + M;
            const float* irP  = ir.data();
            const float* hP   = hist.data() + base;
            float s0 = 0.f, s1 = 0.f, s2 = 0.f, s3 = 0.f;
            int k = 0;
            const int M4 = M - (M & 3);
            for (; k < M4; k += 4) {
                s0 += irP[k]     * hP[-k];
                s1 += irP[k + 1] * hP[-(k + 1)];
                s2 += irP[k + 2] * hP[-(k + 2)];
                s3 += irP[k + 3] * hP[-(k + 3)];
            }
            float sum = (s0 + s1) + (s2 + s3);
            for (; k < M; ++k) sum += irP[k] * hP[-k];
            pos++;
            if (pos >= M) pos = 0;
            return sum;
        }
    };

    ConvLine erConvL, erConvR;

    bool  erImpulseKeyValid    = false;
    float erImpulseRoomSize    = -1.f;
    float erImpulseDiffusion   = -1.f;

    void rebuildErImpulseIfNeeded(float roomSize, float diffusion) {
        if (erImpulseKeyValid &&
            std::fabs(roomSize  - erImpulseRoomSize)  < 0.0005f &&
            std::fabs(diffusion - erImpulseDiffusion) < 0.0005f) return;
        erImpulseKeyValid  = true;
        erImpulseRoomSize  = roomSize;
        erImpulseDiffusion = diffusion;
        buildErImpulse(roomSize, diffusion);
    }

    // Exact port of makeCaveEarlyReflectionIR(ctx, {roomSize, diffusion}) from
    // reverb-engine.js: a 2-channel buffer built from 7 golden-ratio-scattered,
    // per-channel-decorrelated, one-pole-lowpassed noise bursts under an
    // exponential decay envelope, peak-normalized to 0.9.
    void buildErImpulse(float roomSize, float diffusion) {
        const double sr = sampleRate;
        const double durationSec = std::min(0.28, 0.09 + (double)roomSize * 0.06);
        const int length = std::max(1, (int)std::floor(sr * durationSec));

        std::vector<float> irL((size_t)length, 0.f), irR((size_t)length, 0.f);
        const int tapCount = 7;
        const double spread = 0.05 + (1.0 - (double)diffusion) * 0.15;

        for (int ch = 0; ch < 2; ++ch) {
            std::vector<float>& data = (ch == 0) ? irL : irR;
            const double chSeed = (ch == 0) ? 0.37 : 0.71;

            const uint32_t seed =
                (uint32_t)std::floor((double)roomSize * 100003.0) ^
                (uint32_t)std::floor((double)diffusion * 65537.0) ^
                (uint32_t)(ch * 0x9e3779b9u);
            Mulberry32 rng(seed);

            for (int k = 0; k < tapCount; ++k) {
                double frac = std::fmod((k + 1) * 0.618 + chSeed, 1.0);
                double tapTime = 0.006 + frac * spread + k * (spread / (tapCount * 2.2));
                int idx = (int)std::floor(tapTime * sr);
                if (idx >= length) continue;

                double amp = std::pow(0.72, k) * (0.85 + 0.3 * std::fmod(k * chSeed, 1.0));
                int burstLen = std::max(1, (int)std::round(sr * 0.001));

                float lp = 0.f;
                for (int b = 0; b < burstLen && idx + b < length; ++b) {
                    float white = rng.next() * 2.f - 1.f;
                    lp += 0.5f * (white - lp);
                    data[(size_t)(idx + b)] += lp * (float)amp * (1.f - (float)b / (float)burstLen);
                }
            }

            for (int i = 0; i < length; ++i) {
                double t = (double)i / sr;
                data[(size_t)i] *= (float)std::pow(10.0, (-1.2 * t) / durationSec);
            }
        }

        float peak = 0.f;
        for (int i = 0; i < length; ++i) {
            peak = std::max(peak, std::fabs(irL[(size_t)i]));
            peak = std::max(peak, std::fabs(irR[(size_t)i]));
        }
        const float TARGET_PEAK = 0.9f;
        if (peak > TARGET_PEAK) {
            float scale = TARGET_PEAK / peak;
            for (int i = 0; i < length; ++i) {
                irL[(size_t)i] *= scale;
                irR[(size_t)i] *= scale;
            }
        }

        erConvL.setImpulse(std::move(irL));
        erConvR.setImpulse(std::move(irR));
    }
};
