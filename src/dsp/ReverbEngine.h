#pragma once
#include "BiquadFilter.h"
#include "FDNReverb.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <functional>

// ──────────────────────────────────────────────────────────────────────────────
// ReverbEngine — C++ port of createHybridReverbEngine() in reverb-engine.js.
//
// Signal chain:
//   input → pre-delay → +──→ Early Reflections (FIR convolver) → erGain ──┐
//                        └──→ FDN late tail (FDNReverb)          → lateGain ┤→ wetBus
//   wetBus → HF-damp shelf → LF-damp shelf → lowCut HP → highCut LP
//          → M/S width stage → wetOut → output
//   input → dryOut → output   (engine-internal dry, per setParams wetLevel/dryLevel)
// ──────────────────────────────────────────────────────────────────────────────
class ReverbEngine {
public:
    struct Params {
        float preDelay              = 0.015f;  // s
        float earlyReflectionDelay  = 0.f;     // s
        float earlyReflectionLevel  = 0.35f;   // linear
        float lateReverbLevel       = 1.0f;    // linear
        float wetLevel              = 1.0f;    // linear
        float dryLevel              = 0.0f;    // linear
        float highCut               = 9000.f;  // Hz
        float lowCut                = 80.f;    // Hz
        float hfDamping             = 0.f;     // 0..1
        float lfDamping             = 0.f;     // 0..1
        float stereoWidth           = 1.0f;    // 0..2
        // FDN params
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

        // Wet-bus filters
        hfDampShelf.setType(BiquadFilter::HighShelf, sr, 6000.0, 0.707, 0.0);
        lfDampShelf.setType(BiquadFilter::LowShelf,  sr, 200.0,  0.707, 0.0);
        lowCutFilter.setType(BiquadFilter::HighPass,  sr, 80.0,   0.707, 0.0);
        highCutFilter.setType(BiquadFilter::LowPass,  sr, 9000.0, 0.707, 0.0);

        // Pre-delay buffer (up to 2s)
        int preDelayMax = (int)(2.0 * sr) + 4;
        preDelayBuf.assign(preDelayMax, 0.f);
        preDelayBufR.assign(preDelayMax, 0.f);
        preDelayWrite = 0;

        // ER additional delay (up to 0.5s)
        int erDelayMax = (int)(0.5 * sr) + 4;
        erDelayBufL.assign(erDelayMax, 0.f);
        erDelayBufR.assign(erDelayMax, 0.f);
        erDelayWrite = 0;

        buildErImpulse(p.roomSize, p.diffusion);
        erConvPos = 0;
        erConvBufL.assign(erImpulseL.size() + 1024, 0.f);
        erConvBufR.assign(erImpulseR.size() + 1024, 0.f);
    }

    void setSampleRate(double sr) { init(sr); }
    void setParams(const Params& newP) {
        bool needIR = (std::abs(newP.roomSize  - p.roomSize)  > 0.001f ||
                       std::abs(newP.diffusion - p.diffusion) > 0.001f);
        p = newP;
        if (needIR) buildErImpulse(p.roomSize, p.diffusion);

        // Update FDN
        FDNReverb::Params fdnP;
        fdnP.roomSize    = p.roomSize;
        fdnP.decayTime   = p.decayTime;
        fdnP.diffusion   = p.diffusion;
        fdnP.density     = p.density;
        fdnP.hfDamping   = p.hfDamping;
        fdnP.lfDamping   = p.lfDamping;
        fdnP.modDepth    = p.modulationDepth;
        fdnP.modRate     = p.modulationRate;
        fdn.setParams(fdnP);

        // Update wet-bus filters
        hfDampShelf.setGain(-std::min(1.f, std::max(0.f, p.hfDamping)) * 12.f);
        lfDampShelf.setGain(-std::min(1.f, std::max(0.f, p.lfDamping)) *  6.f);
        lowCutFilter.setFrequency(std::min(2000.f, std::max(20.f, p.lowCut)));
        highCutFilter.setFrequency(std::min(20000.f, std::max(200.f, p.highCut)));
    }

    // Process one stereo frame
    void processStereo(float inL, float inR, float& outL, float& outR) {
        // Pre-delay
        float pdL = readDelay(preDelayBuf, preDelayWrite, preDelaySamples());
        float pdR = readDelay(preDelayBufR, preDelayWrite, preDelaySamples());
        writeDelay(preDelayBuf,  preDelayWrite, inL);
        writeDelay(preDelayBufR, preDelayWrite, inR);

        // Early Reflections additional delay
        float erL = readDelay(erDelayBufL, erDelayWrite, erDelaySamples());
        float erR = readDelay(erDelayBufR, erDelayWrite, erDelaySamples());
        writeDelay(erDelayBufL, erDelayWrite, pdL);
        writeDelay(erDelayBufR, erDelayWrite, pdR);

        // Early reflections convolution (FIR overlap-add, short block)
        float convL = 0.f, convR = 0.f;
        convolveER(erL, erR, convL, convR);
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

        // HF/LF damping shelves
        hfDampShelf.processStereo(wetL, wetR);
        lfDampShelf.processStereo(wetL, wetR);

        // High-pass (low cut) + low-pass (high cut)
        lowCutFilter.processStereo(wetL, wetR);
        highCutFilter.processStereo(wetL, wetR);

        // M/S stereo width
        applyWidth(wetL, wetR, p.stereoWidth);

        // Engine-internal wet/dry mix
        outL = wetL * p.wetLevel + inL * p.dryLevel;
        outR = wetR * p.wetLevel + inR * p.dryLevel;
    }

    void reset() { init(sampleRate); }

    Params p;

private:
    double sampleRate = 44100.0;
    FDNReverb fdn;
    BiquadFilter hfDampShelf, lfDampShelf, lowCutFilter, highCutFilter;

    // Pre-delay
    std::vector<float> preDelayBuf, preDelayBufR;
    int preDelayWrite = 0;

    // ER additional delay
    std::vector<float> erDelayBufL, erDelayBufR;
    int erDelayWrite = 0;

    // Early reflection FIR impulse response (stereo, computed from first principles
    // — same algorithm as makeCaveEarlyReflectionIR in reverb-engine.js).
    std::vector<float> erImpulseL, erImpulseR;
    std::vector<float> erConvBufL, erConvBufR;
    int erConvPos = 0;

    // ── Delay helpers ──────────────────────────────────────────────────────────
    int preDelaySamples() const {
        return std::min((int)(p.preDelay * (float)sampleRate),
                        (int)preDelayBuf.size()-1);
    }
    int erDelaySamples() const {
        return std::min((int)(p.earlyReflectionDelay * (float)sampleRate),
                        (int)erDelayBufL.size()-1);
    }

    static float readDelay(const std::vector<float>& buf, int writeIdx, int delay) {
        if (delay <= 0 || buf.empty()) return 0.f;
        int n = (int)buf.size();
        int idx = writeIdx - delay;
        while (idx < 0) idx += n;
        return buf[idx % n];
    }
    static void writeDelay(std::vector<float>& buf, int& writeIdx, float v) {
        if (buf.empty()) return;
        buf[writeIdx] = v;
        writeIdx = (writeIdx + 1) % (int)buf.size();
    }

    // ── Early Reflections FIR convolution (direct, short IR) ─────────────────
    void convolveER(float inL, float inR, float& outL, float& outR) {
        int irLen = (int)erImpulseL.size();
        if (irLen == 0) { outL = outR = 0.f; return; }
        int bufSz = (int)erConvBufL.size();
        // Write new sample
        erConvBufL[erConvPos] = inL;
        erConvBufR[erConvPos] = inR;

        // Direct convolution
        float sumL = 0.f, sumR = 0.f;
        for (int k = 0; k < irLen; ++k) {
            int idx = erConvPos - k;
            if (idx < 0) idx += bufSz;
            sumL += erConvBufL[idx] * erImpulseL[k];
            sumR += erConvBufR[idx] * erImpulseR[k];
        }
        outL = sumL;
        outR = sumR;
        erConvPos = (erConvPos + 1) % bufSz;
    }

    // ── M/S stereo width ─────────────────────────────────────────────────────
    static void applyWidth(float& l, float& r, float width) {
        // M/S encode → scale side → decode
        // Matches reverb-engine.js stereo width implementation
        float mid  = 0.5f*(l+r);
        float side = 0.5f*(l-r);
        side *= width;
        l = mid + side;
        r = mid - side;
    }

    // ── Early Reflection impulse response builder ─────────────────────────────
    // Exact port of makeCaveEarlyReflectionIR in reverb-engine.js.
    void buildErImpulse(float roomSize, float diffusion) {
        double sr  = sampleRate;
        double durSec = std::min(0.28, 0.09 + roomSize * 0.06);
        int    length = std::max(1, (int)std::floor(sr * durSec));

        erImpulseL.assign(length, 0.f);
        erImpulseR.assign(length, 0.f);

        constexpr int TAP_COUNT = 7;
        double spread = 0.05 + (1.0 - diffusion) * 0.15;

        for (int ch = 0; ch < 2; ++ch) {
            auto& data = (ch == 0) ? erImpulseL : erImpulseR;
            double chSeed = (ch == 0) ? 0.37 : 0.71;

            uint32_t seed = (uint32_t)((int)(roomSize*100003) ^ (int)(diffusion*65537)
                                       ^ (ch * 0x9e3779b9u));
            auto rng = [&]() -> double {
                seed += 0x6d2b79f5u;
                uint32_t t = seed ^ (seed >> 15);
                t = (t + (uint32_t)((t ^ (t >> 7)) * 61u)) ^ t;
                return (double)((t ^ (t >> 14)) >> 0) / 4294967296.0;
            };

            for (int k = 0; k < TAP_COUNT; ++k) {
                double frac   = std::fmod((k+1)*0.618 + chSeed, 1.0);
                double tapTime= 0.006 + frac*spread + k*(spread/(TAP_COUNT*2.2));
                int    idx    = (int)std::floor(tapTime * sr);
                if (idx >= length) continue;
                double amp = std::pow(0.72, k) * (0.85 + 0.3*(std::fmod(k*chSeed,1.0)));
                int    burstLen = std::max(1,(int)std::round(sr*0.001));
                double lp = 0.0;
                for (int b = 0; b < burstLen && idx+b < length; ++b) {
                    double white = rng()*2.0 - 1.0;
                    lp += 0.5*(white - lp);
                    data[idx+b] += (float)(lp * amp * (1.0 - (double)b/burstLen));
                }
            }
            // Decay envelope
            for (int i = 0; i < length; ++i) {
                double t = i / sr;
                data[i] *= (float)std::pow(10.0, -1.2*t/durSec);
            }
        }

        // Normalize peak to 0.9
        float peak = 0.f;
        for (auto v : erImpulseL) peak = std::max(peak, std::abs(v));
        for (auto v : erImpulseR) peak = std::max(peak, std::abs(v));
        if (peak > 0.9f) {
            float scale = 0.9f / peak;
            for (auto& v : erImpulseL) v *= scale;
            for (auto& v : erImpulseR) v *= scale;
        }

        // Resize convolution ring buffers
        erConvBufL.assign(length + 1024, 0.f);
        erConvBufR.assign(length + 1024, 0.f);
        erConvPos = 0;
    }
};
