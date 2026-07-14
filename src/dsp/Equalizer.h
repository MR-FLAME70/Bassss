#pragma once
#include "BiquadFilter.h"
#include <array>

// 10-band peaking EQ — matches the Web Audio API cascade from popup/offscreen.
// Band frequencies: 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
// Q = 1.4 for every band (matches offscreen.js createEqModule).
class Equalizer {
public:
    static constexpr int BANDS = 10;
    static constexpr double BAND_HZ[BANDS] = {
        31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
    };

    Equalizer() { setSampleRate(44100.0); }

    void setSampleRate(double sr) {
        sampleRate = sr;
        for (int i = 0; i < BANDS; ++i) {
            filters[i].setType(BiquadFilter::Peaking, sr, BAND_HZ[i], 1.4, gains[i]);
        }
    }

    void setBands(const std::array<float,BANDS>& g) {
        gains = g;
        for (int i = 0; i < BANDS; ++i)
            filters[i].setGain(gains[i]);
    }

    void setBand(int i, float gainDb) {
        if (i < 0 || i >= BANDS) return;
        gains[i] = gainDb;
        filters[i].setGain(gainDb);
    }

    void processStereo(float& l, float& r) {
        for (int i = 0; i < BANDS; ++i)
            filters[i].processStereo(l, r);
    }

    void reset() {
        for (auto& f : filters) f.reset();
    }

private:
    double sampleRate = 44100.0;
    std::array<float,BANDS> gains = {0,0,0,0,0,0,0,0,0,0};
    std::array<BiquadFilter,BANDS> filters;
};
