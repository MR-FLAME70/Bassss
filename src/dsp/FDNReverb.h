#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// FDNReverb — exact C++ port of FDNReverbProcessor (fdn-reverb-worklet.js).
// 8-line Feedback Delay Network with Householder matrix, per-line allpass
// diffusion, per-line RT60-correct gain, HF/LF damping, and LFO modulation.
// Every algorithm decision mirrors the worklet exactly so the sonic result is
// indistinguishable from the Chrome extension.
// ──────────────────────────────────────────────────────────────────────────────
class FDNReverb {
public:
    static constexpr int N = 8;

    // Parameter struct (all values match worklet defaults/ranges)
    struct Params {
        float roomSize        = 1.0f;  // 0.25..3.0
        float decayTime       = 1.8f;  // s  0.1..25
        float diffusion       = 0.7f;  // 0..1
        float density         = 0.5f;  // 0..1
        float hfDamping       = 0.5f;  // 0..1
        float lfDamping       = 0.2f;  // 0..1
        float modDepth        = 0.4f;  // 0..1
        float modRate         = 0.35f; // 0..1
        float inputGain       = 1.0f;
        float outputGain      = 1.0f;
    };

    FDNReverb() { init(44100.0); }

    void init(double sr) {
        sampleRate = sr;
        // Max buffer: largest base delay * max roomSize + mod excursion + margin
        double maxBase    = *std::max_element(BASE_DELAY_SEC.begin(), BASE_DELAY_SEC.end());
        double maxDelaySec = maxBase * 3.0 + 0.008 + 0.02;
        bufLen = (int)std::ceil(sr * maxDelaySec) + 4;

        for (int i = 0; i < N; ++i) {
            buffers[i].assign(bufLen, 0.f);
            writeIdx[i] = 0;
        }
        ap1State.fill(0.f); ap1XState.fill(0.f);
        ap2State.fill(0.f); ap2XState.fill(0.f);
        hfState.fill(0.f);  lfState.fill(0.f);
        inDiffStagesL.fill(0.f);  inDiffXStagesL.fill(0.f);
        inDiffStagesR.fill(0.f);  inDiffXStagesR.fill(0.f);

        // Smooth coefficient: ~30ms time constant
        smoothCoeff = (float)std::exp(-1.0 / (sr * 0.03));

        smoothed = params; // initialise smoothed to current params
    }

    void setSampleRate(double sr) { init(sr); }
    void setParams(const Params& p) { params = p; }

    // Process one stereo frame
    void processStereo(float inL, float inR, float& outL, float& outR) {
        const float c = smoothCoeff;
        auto smooth = [&](float& s, float t){ s += (1.f-c)*(t-s); };
        smooth(smoothed.roomSize,   params.roomSize);
        smooth(smoothed.decayTime,  params.decayTime);
        smooth(smoothed.diffusion,  params.diffusion);
        smooth(smoothed.density,    params.density);
        smooth(smoothed.hfDamping,  params.hfDamping);
        smooth(smoothed.lfDamping,  params.lfDamping);
        smooth(smoothed.modDepth,   params.modDepth);
        smooth(smoothed.modRate,    params.modRate);

        const float decayTime = std::max(0.1f, smoothed.decayTime);
        const float diffK     = std::min(0.7f, std::max(0.f, smoothed.diffusion * 0.7f));

        // HF damping: one-pole lowpass cutoff 600..18000 Hz
        float hfCutoff = 600.f + (18000.f - 600.f) * std::pow(1.f - smoothed.hfDamping, 2.f);
        float hfA = std::exp(-(float)(2.0*M_PI) * hfCutoff / (float)sampleRate);

        // LF damping: slow component subtracted
        float lfCutoff = 80.f + 400.f * smoothed.lfDamping;
        float lfA      = std::exp(-(float)(2.0*M_PI) * lfCutoff / (float)sampleRate);
        float lfAmount = smoothed.lfDamping * 0.9f;

        // Modulation
        float modExcursion = smoothed.modDepth * 0.004f;
        float modRateHz    = 0.03f + smoothed.modRate * 1.47f;

        // Input diffusion (Density-controlled), separate L/R
        float dL = inL * params.inputGain;
        float dR = inR * params.inputGain;
        float densityMix = 0.2f + smoothed.density * 0.5f;
        for (int st = 0; st < 4; ++st) {
            float kL = IN_DIFF_COEFFS[st] * densityMix;
            float yL = -kL*dL + inDiffXStagesL[st] + kL*inDiffStagesL[st];
            inDiffXStagesL[st] = dL; inDiffStagesL[st] = yL; dL = yL;

            float kR = IN_DIFF_COEFFS[st] * densityMix;
            float yR = -kR*dR + inDiffXStagesR[st] + kR*inDiffStagesR[st];
            inDiffXStagesR[st] = dR; inDiffStagesR[st] = yR; dR = yR;
        }

        // Per-line processing
        std::array<float,N> damped;
        for (int ln = 0; ln < N; ++ln) {
            modPhase[ln] += (float)(2.0*M_PI) * modRateHz * MOD_RATE_MUL[ln] / (float)sampleRate;
            if (modPhase[ln] > (float)(2.0*M_PI)) modPhase[ln] -= (float)(2.0*M_PI);

            float baseSec   = BASE_DELAY_SEC[ln] * smoothed.roomSize;
            float modSec    = std::sin(modPhase[ln]) * modExcursion;
            float delaySec  = std::max(0.001f, baseSec + modSec);
            float delaySamples = delaySec * (float)sampleRate;

            // Fractional read (linear interpolation)
            float readPos = (float)writeIdx[ln] - delaySamples;
            float rp = std::fmod(readPos, (float)bufLen);
            if (rp < 0.f) rp += bufLen;
            int i0 = (int)rp;
            float frac = rp - i0;
            int i1 = (i0+1) % bufLen;
            float raw = buffers[ln][i0]*(1.f-frac) + buffers[ln][i1]*frac;

            // Per-line diffusion (2 cascaded first-order allpasses)
            float lineDiffK = std::min(0.7f, diffK * DIFF_MUL[ln]);
            float d = raw;
            d = allpass(d, lineDiffK,       ap1XState, ap1State, ln);
            d = allpass(d, lineDiffK*0.8f,  ap2XState, ap2State, ln);

            // HF damping (one-pole lowpass in feedback path)
            hfState[ln] = (1.f-hfA)*d + hfA*hfState[ln];
            d = hfState[ln];

            // LF damping (subtract slow LP component)
            lfState[ln] = (1.f-lfA)*d + lfA*lfState[ln];
            d = d - lfAmount*lfState[ln];

            // RT60-correct per-line gain
            float g = std::pow(10.f, (-3.f * delaySec) / decayTime);
            g = std::min(0.995f, std::max(0.f, g));
            damped[ln] = d * g;
        }

        // Householder feedback matrix: Hv = v - (2/N)*sum(v)*[1,...,1]
        float sum = 0.f;
        for (int ln = 0; ln < N; ++ln) sum += damped[ln];
        float sub = (2.f / N) * sum;

        float lOut = 0.f, rOut = 0.f;
        for (int ln = 0; ln < N; ++ln) {
            float mixed  = damped[ln] - sub;
            float inject = (ln % 2 == 0) ? dL : dR;
            float wv = mixed + inject * 0.6f;
            buffers[ln][writeIdx[ln]] = wv;
            writeIdx[ln] = (writeIdx[ln] + 1) % bufLen;
            lOut += mixed * TAP_L[ln];
            rOut += mixed * TAP_R[ln];
        }

        float norm = params.outputGain / std::sqrt((float)N);
        outL = lOut * norm;
        outR = rOut * norm;
    }

    void reset() { init(sampleRate); }

private:
    double sampleRate = 44100.0;
    Params params;
    Params smoothed;
    float  smoothCoeff = 0.f;
    int    bufLen = 0;

    std::array<std::vector<float>, N> buffers;
    std::array<int, N>   writeIdx   = {};
    std::array<float, N> modPhase   = {};
    std::array<float, N> ap1State   = {};
    std::array<float, N> ap1XState  = {};
    std::array<float, N> ap2State   = {};
    std::array<float, N> ap2XState  = {};
    std::array<float, N> hfState    = {};
    std::array<float, N> lfState    = {};
    std::array<float, 4> inDiffStagesL  = {};
    std::array<float, 4> inDiffXStagesL = {};
    std::array<float, 4> inDiffStagesR  = {};
    std::array<float, 4> inDiffXStagesR = {};

    // Constants from fdn-reverb-worklet.js
    static constexpr std::array<double,N> BASE_DELAY_SEC = {
        0.0233, 0.0297, 0.0331, 0.0389, 0.0431, 0.0479, 0.0523, 0.0577
    };
    static constexpr std::array<float,N> MOD_RATE_MUL = {
        0.83f, 1.0f, 1.14f, 0.96f, 1.27f, 0.9f, 1.08f, 1.2f
    };
    static constexpr std::array<float,N> DIFF_MUL = {
        0.92f, 1.05f, 0.97f, 1.1f, 0.9f, 1.06f, 0.95f, 1.08f
    };
    static constexpr std::array<int,N> TAP_L = { 1, 1,-1,-1, 1,-1,-1, 1 };
    static constexpr std::array<int,N> TAP_R = { 1,-1, 1,-1,-1, 1,-1, 1 };
    static constexpr std::array<float,4> IN_DIFF_COEFFS = { 0.68f,-0.59f, 0.51f,-0.45f };

    static inline float allpass(float x, float k,
                                std::array<float,N>& xState,
                                std::array<float,N>& yState, int idx) {
        float y = -k*x + xState[idx] + k*yState[idx];
        xState[idx] = x;
        yState[idx] = y;
        return y;
    }
};
