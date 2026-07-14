#pragma once
#include <cmath>
#include <algorithm>

// Dynamics compressor — matches Web Audio API DynamicsCompressorNode behaviour
// (logarithmic gain computer + ballistic envelope follower, knee=6dB default).
// Also used as the Limiter when ratio=20, knee=0, attack=0.001.
class Compressor {
public:
    Compressor() = default;

    void setSampleRate(double sr) {
        sampleRate = sr;
        updateCoeffs();
    }

    void setThreshold(float dB)  { threshold = dB; }
    void setKnee(float dB)       { knee = dB; }
    void setRatio(float r)       { ratio = r; }
    void setAttack(float sec)    { attack = sec; updateCoeffs(); }
    void setRelease(float sec)   { release = sec; updateCoeffs(); }
    void setMakeupGain(float dB) { makeupLin = std::pow(10.f, dB/20.f); }

    void processStereo(float& l, float& r) {
        // Level detection: max of absolute values (peak detector)
        float inputLevel = std::max(std::abs(l), std::abs(r));
        float inputDb = (inputLevel < 1e-8f) ? -160.f : 20.f*std::log10(inputLevel);

        // Gain computer (soft-knee)
        float csDb = computeGain(inputDb);

        // Ballistic envelope follower on the gain reduction
        float target = csDb - inputDb; // gain reduction in dB (≤0)
        if (target < gainDb) {
            // attack (fast)
            gainDb += (target - gainDb) * attackCoeff;
        } else {
            // release (slow)
            gainDb += (target - gainDb) * releaseCoeff;
        }

        float linGain = std::pow(10.f, gainDb/20.f) * makeupLin;
        l *= linGain;
        r *= linGain;
    }

    float getGainReductionDb() const { return gainDb; }

    void reset() { gainDb = 0.f; }

private:
    double sampleRate = 44100.0;
    float  threshold  = -24.f; // dB
    float  knee       = 6.f;   // dB
    float  ratio      = 4.f;
    float  attack     = 0.003f; // seconds
    float  release    = 0.25f;  // seconds
    float  makeupLin  = 1.f;
    float  attackCoeff  = 0.f;
    float  releaseCoeff = 0.f;
    float  gainDb       = 0.f;

    void updateCoeffs() {
        // One-pole IIR coefficients: c = 1 - exp(-1/(sr*time))
        attackCoeff  = 1.f - std::exp(-1.f / (float)(sampleRate * attack));
        releaseCoeff = 1.f - std::exp(-1.f / (float)(sampleRate * release));
    }

    float computeGain(float inDb) const {
        // Soft-knee gain computer (matches Web Audio spec):
        float kneeHalf = knee * 0.5f;
        if (inDb < threshold - kneeHalf) {
            return inDb;                         // below knee — no compression
        } else if (inDb > threshold + kneeHalf) {
            // Hard compression above knee
            return threshold + (inDb - threshold) / ratio;
        } else {
            // Within soft knee
            float x = inDb - threshold + kneeHalf;
            return inDb + (1.f/ratio - 1.f) * x*x / (2.f * knee);
        }
    }
};
