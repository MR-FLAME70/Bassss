#pragma once
#include "BiquadFilter.h"
#include "Compressor.h"
#include <cmath>
#include <algorithm>

// Exact port of createDynamicBassModule from offscreen.js.
// Additive bass enhancer: dry path always passes through; when enabled,
// a lowpass-filtered copy of the signal is compressed and mixed in on top.
class DynamicBass {
public:
    DynamicBass() {
        lowSplit.setType(BiquadFilter::LowPass, 44100.0, 150.0, 0.707, 0.0);
        comp.setKnee(10.f);
        comp.setRatio(6.f);
        comp.setAttack(0.01f);
        comp.setRelease(0.25f);
        comp.setThreshold(-10.f);
    }

    void setSampleRate(double sr) {
        sampleRate = sr;
        lowSplit.setSampleRate(sr);
        comp.setSampleRate(sr);
    }

    // sensitivity % (0-100): lowers compressor threshold
    // strength % (0-100): output mix gain
    void setParams(float sensitivity, float strength) {
        float sens = std::max(0.f, std::min(100.f, sensitivity)) / 100.f;
        float str  = std::max(0.f, std::min(100.f, strength))    / 100.f;
        comp.setThreshold(-10.f - sens * 40.f);
        makeupGain = str * 4.f;
    }

    // stereo in-place; dry is always passed through, additive branch mixed on top
    void processStereo(float& l, float& r) {
        // Dry path is just l,r unchanged — we ADD the enhanced bass signal
        float bl = l, br = r;
        lowSplit.processStereo(bl, br);
        comp.processStereo(bl, br);
        bl *= makeupGain;
        br *= makeupGain;
        l += bl;
        r += br;
    }

    void reset() {
        lowSplit.reset();
        comp.reset();
    }

private:
    double       sampleRate = 44100.0;
    BiquadFilter lowSplit;
    Compressor   comp;
    float        makeupGain = 0.f;
};
