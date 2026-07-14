#pragma once
#include <cmath>

// Exact port of rotator-worklet.js — 2×2 rotation matrix on stereo field.
// Lout = cos(θ)·Lin − sin(θ)·Rin
// Rout = sin(θ)·Lin + cos(θ)·Rin
// θ advances by 2π·rateHz / sampleRate each sample.
class Rotator {
public:
    Rotator() = default;

    void setSampleRate(double sr) { sampleRate = sr; }
    void setRate(float hz)        { rateHz = hz; }

    inline void processStereo(float& l, float& r) {
        phase += (float)(2.0*M_PI * rateHz / sampleRate);
        if (phase > (float)(2.0*M_PI)) phase -= (float)(2.0*M_PI);
        float c = std::cos(phase);
        float s = std::sin(phase);
        float nl =  c*l - s*r;
        float nr =  s*l + c*r;
        l = nl; r = nr;
    }

    void reset() { phase = 0.f; }

private:
    double sampleRate = 44100.0;
    float  rateHz     = 0.15f;
    float  phase      = 0.f;
};
