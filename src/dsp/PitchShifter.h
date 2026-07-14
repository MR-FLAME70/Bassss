#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// PitchShifter — exact port of pitch-shift-worklet.js
// Overlap-add granular pitch shifter using two Hann-windowed grains that are
// always half a grain length apart so their windows sum to exactly 1 (no
// amplitude pumping). Grain size = 4096 samples, ring = 16384 samples.
// ──────────────────────────────────────────────────────────────────────────────
class PitchShifter {
public:
    static constexpr int RING_SIZE = 16384;
    static constexpr int GRAIN     = 4096;

    PitchShifter() {
        bufsL.assign(RING_SIZE, 0.f);
        bufsR.assign(RING_SIZE, 0.f);
        writePos = 0;
        posA = 0.f;
        posB = GRAIN / 2.f;
    }

    void setSampleRate(double /*sr*/) {} // algorithm is sample-count-based, not rate-dependent
    void setPitchSemitones(float st) { ratio = std::pow(2.f, st / 12.f); }
    void setRatio(float r)           { ratio = r; }

    void processStereo(float& l, float& r) {
        bufsL[writePos] = l;
        bufsR[writePos] = r;

        // Hann window: winA + winB = 1 always
        float winA = 0.5f - 0.5f * std::cos(2.f*(float)M_PI * posA / GRAIN);
        float winB = 1.f - winA;

        l = winA * readInterp(bufsL, writePos - GRAIN + posA)
          + winB * readInterp(bufsL, writePos - GRAIN + posB);
        r = winA * readInterp(bufsR, writePos - GRAIN + posA)
          + winB * readInterp(bufsR, writePos - GRAIN + posB);

        writePos = (writePos + 1) % RING_SIZE;

        posA += ratio;
        if (posA >= GRAIN) posA -= GRAIN;
        posB += ratio;
        if (posB >= GRAIN) posB -= GRAIN;
    }

    void reset() {
        std::fill(bufsL.begin(), bufsL.end(), 0.f);
        std::fill(bufsR.begin(), bufsR.end(), 0.f);
        writePos = 0;
        posA = 0.f;
        posB = GRAIN / 2.f;
    }

private:
    std::vector<float> bufsL, bufsR;
    int   writePos = 0;
    float posA     = 0.f;
    float posB     = (float)(GRAIN / 2);
    float ratio    = 1.f;

    inline float readInterp(const std::vector<float>& buf, float pos) const {
        // wrap to [0, RING_SIZE)
        int n = RING_SIZE;
        float rp = pos;
        while (rp < 0.f)    rp += n;
        int base = (int)rp;
        float frac = rp - base;
        int i0 = base % n;
        int i1 = (i0+1) % n;
        return buf[i0] * (1.f-frac) + buf[i1] * frac;
    }
};
