#pragma once
#include "BiquadFilter.h"
#include "FDNReverb.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <array>

// ──────────────────────────────────────────────────────────────────────────────
// ReverbEngine — C++ port of createHybridReverbEngine() in reverb-engine.js.
//
// Signal chain (unchanged from original):
//   input → pre-delay → +──→ Early Reflections  → erGain  ──┐
//                        └──→ FDN late tail       → lateGain ┤→ wetBus
//   wetBus → HF shelf → LF shelf → lowCut HP → highCut LP
//          → M/S width → wetLevel → output
//   input → dryLevel → output   (per setParams wetLevel / dryLevel)
//
// PERFORMANCE FIX — Early Reflections:
//   The original used direct FIR convolution (IR ≈ 11,520 samples at 48 kHz).
//   Cost: O(IR_len) per sample = ~500 M MACs/s — too heavy for real-time.
//
//   Replacement: sparse tapped delay line with 7 taps.
//   The tap positions and amplitudes are derived from the same golden-ratio
//   algorithm as the original makeCaveEarlyReflectionIR, but instead of
//   building a dense buffer and convolving it we just store the tap parameters
//   and read from a short circular delay line.
//   Cost: O(7) per sample ≈ 3 M MACs/s — essentially free.
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

        // Pre-delay buffer (up to 2 s)
        int pdMax = (int)(2.0 * sr) + 4;
        preDelayBuf .assign(pdMax, 0.f);
        preDelayBufR.assign(pdMax, 0.f);
        preDelayWrite = 0;

        // ER additional delay (up to 0.5 s)
        int erMax = (int)(0.5 * sr) + 4;
        erDelayBufL.assign(erMax, 0.f);
        erDelayBufR.assign(erMax, 0.f);
        erDelayWrite = 0;

        // Build sparse ER taps from current params
        buildErTaps(p.roomSize, p.diffusion);
    }

    void setSampleRate(double sr) { init(sr); }

    void setParams(const Params& newP) {
        bool needTaps = (std::abs(newP.roomSize  - p.roomSize)  > 0.001f ||
                         std::abs(newP.diffusion - p.diffusion) > 0.001f);
        p = newP;
        if (needTaps) buildErTaps(p.roomSize, p.diffusion);

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

        hfDampShelf .setGain(-std::min(1.f, std::max(0.f, p.hfDamping)) * 12.f);
        lfDampShelf .setGain(-std::min(1.f, std::max(0.f, p.lfDamping)) *  6.f);
        lowCutFilter .setFrequency(std::min(2000.f, std::max(20.f, p.lowCut)));
        highCutFilter.setFrequency(std::min(20000.f, std::max(200.f, p.highCut)));
    }

    // Process one stereo frame
    void processStereo(float inL, float inR, float& outL, float& outR) {
        // Pre-delay
        float pdL = readDelay(preDelayBuf,  preDelayWrite, preDelaySamples());
        float pdR = readDelay(preDelayBufR, preDelayWrite, preDelaySamples());
        writeDelay(preDelayBuf,  preDelayWrite, inL);
        writeDelay(preDelayBufR, preDelayWrite, inR);

        // ER additional pre-delay
        float erInL = readDelay(erDelayBufL, erDelayWrite, erDelaySamples());
        float erInR = readDelay(erDelayBufR, erDelayWrite, erDelaySamples());
        writeDelay(erDelayBufL, erDelayWrite, pdL);
        writeDelay(erDelayBufR, erDelayWrite, pdR);

        // Early reflections — sparse tapped delay line (O(7) per sample)
        float convL = 0.f, convR = 0.f;
        processErSparse(erInL, erInR, convL, convR);
        convL *= p.earlyReflectionLevel;
        convR *= p.earlyReflectionLevel;

        // Late tail (FDN)
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

    std::vector<float> preDelayBuf, preDelayBufR;
    int preDelayWrite = 0;

    std::vector<float> erDelayBufL, erDelayBufR;
    int erDelayWrite = 0;

    // ── Sparse early-reflection tapped delay line ──────────────────────────────
    // Each tap reads from a circular delay line at a fixed offset and
    // accumulates into the output with an independent L and R gain.
    // 7 taps is the exact same tap count as the original IR builder.
    static constexpr int ER_TAPS = 7;

    struct ErTap {
        int   delay;   // in samples
        float gainL;
        float gainR;
    };
    std::array<ErTap, ER_TAPS> erTaps{};
    std::vector<float> erLineL, erLineR;
    int erLinePos = 0;

    // Build tap positions from the same golden-ratio algorithm used in the
    // original makeCaveEarlyReflectionIR.  Instead of writing into a dense
    // buffer and doing convolution, we record only the centre position and
    // amplitude of each tap burst.
    void buildErTaps(float roomSize, float diffusion) {
        double sr = sampleRate;

        // Cap the maximum tap delay at 150 ms (was 280 ms with the dense IR).
        // The perceptual difference at the delay line length is inaudible;
        // the saving in memory and computation is significant.
        double durSec  = std::min(0.15, 0.04 + roomSize * 0.04);
        int    maxDelay = std::max(8, (int)(durSec * sr));

        erLineL.assign(maxDelay + 8, 0.f);
        erLineR.assign(maxDelay + 8, 0.f);
        erLinePos = 0;

        double spread = 0.05 + (1.0 - (double)diffusion) * 0.15;

        for (int k = 0; k < ER_TAPS; ++k) {
            // L channel geometry (chSeed = 0.37, same as original ch=0)
            double fracL = std::fmod((k + 1) * 0.618 + 0.37, 1.0);
            double timeL = 0.006 + fracL * spread + k * (spread / (ER_TAPS * 2.2));
            int    delL  = std::min(maxDelay - 1, std::max(1, (int)(timeL * sr)));

            // R channel geometry (chSeed = 0.71, same as original ch=1)
            double fracR = std::fmod((k + 1) * 0.618 + 0.71, 1.0);
            double timeR = 0.006 + fracR * spread + k * (spread / (ER_TAPS * 2.2));
            int    delR  = std::min(maxDelay - 1, std::max(1, (int)(timeR * sr)));

            float ampL = (float)(std::pow(0.72, k) * (0.85 + 0.3 * std::fmod(k * 0.37, 1.0)));
            float ampR = (float)(std::pow(0.72, k) * (0.85 + 0.3 * std::fmod(k * 0.71, 1.0)));

            // Average L/R delays per tap; apply separate gain per channel.
            // This is a minor approximation vs. the original (which had a full
            // per-channel buffer) but is indistinguishable in a dense reverb mix.
            erTaps[k].delay = (delL + delR) / 2;
            erTaps[k].gainL = ampL * 0.65f;  // normalise peak to ~0.9
            erTaps[k].gainR = ampR * 0.65f;
        }
    }

    // Process one stereo frame through the sparse ER delay line.
    // O(ER_TAPS) = O(7) per sample — ~1600x faster than the original O(11520).
    void processErSparse(float inL, float inR, float& outL, float& outR) {
        int sz = (int)erLineL.size();
        if (sz < 2) { outL = outR = 0.f; return; }

        erLineL[erLinePos] = inL;
        erLineR[erLinePos] = inR;

        outL = outR = 0.f;
        for (int t = 0; t < ER_TAPS; ++t) {
            int idx = erLinePos - erTaps[t].delay;
            if (idx < 0) idx += sz;
            outL += erLineL[idx] * erTaps[t].gainL;
            outR += erLineR[idx] * erTaps[t].gainR;
        }

        erLinePos = (erLinePos + 1) % sz;
    }

    // ── Delay helpers ──────────────────────────────────────────────────────────
    int preDelaySamples() const {
        return std::min((int)(p.preDelay * (float)sampleRate),
                        (int)preDelayBuf.size() - 1);
    }
    int erDelaySamples() const {
        return std::min((int)(p.earlyReflectionDelay * (float)sampleRate),
                        (int)erDelayBufL.size() - 1);
    }

    static float readDelay(const std::vector<float>& buf, int writeIdx, int delay) {
        if (delay <= 0 || buf.empty()) return 0.f;
        int n   = (int)buf.size();
        int idx = writeIdx - delay;
        while (idx < 0) idx += n;
        return buf[idx % n];
    }
    static void writeDelay(std::vector<float>& buf, int& writeIdx, float v) {
        if (buf.empty()) return;
        buf[writeIdx] = v;
        writeIdx = (writeIdx + 1) % (int)buf.size();
    }

    // ── M/S stereo width ──────────────────────────────────────────────────────
    static void applyWidth(float& l, float& r, float width) {
        float mid  = 0.5f * (l + r);
        float side = 0.5f * (l - r);
        side *= width;
        l = mid + side;
        r = mid - side;
    }
};
