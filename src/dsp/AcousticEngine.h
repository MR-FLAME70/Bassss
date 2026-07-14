#pragma once
#include "BiquadFilter.h"
#include "Compressor.h"
#include <cmath>
#include <algorithm>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// AcousticEngine — from-scratch port of createAcousticEngine() in offscreen.js.
// Replicates the 5 SBX Pro Studio effects: Surround, Crystalizer, Bass,
// Smart Volume (compressor), and Dialog Plus, in signal order.
// ──────────────────────────────────────────────────────────────────────────────
class AcousticEngine {
public:
    AcousticEngine() {
        bassFilter.setType(BiquadFilter::LowShelf, 44100.0, 100.0, 0.707, 0.0);
        crystalShelf.setType(BiquadFilter::HighShelf, 44100.0, 7500.0, 0.707, 0.0);
        dialogFilter.setType(BiquadFilter::Peaking, 44100.0, 2800.0, 1.1, 0.0);
        smartVol.setThreshold(0.f);
        smartVol.setKnee(6.f);
        smartVol.setRatio(1.f);
        smartVol.setAttack(0.01f);
        smartVol.setRelease(0.3f);
        smartVol.setMakeupGain(0.f);
        shapingCurveSize = 1024;
        shapingCurve.resize(shapingCurveSize, 0.f);
        makeSaturationCurve(0.f);
    }

    void setSampleRate(double sr) {
        sampleRate = sr;
        bassFilter.setSampleRate(sr);
        crystalShelf.setSampleRate(sr);
        dialogFilter.setSampleRate(sr);
        smartVol.setSampleRate(sr);
    }

    // Update all effect amounts (match offscreen.js update() signature)
    // surround, crystalizer, bass, smartVolume, dialogPlus: all 0-100 %
    // crossover: Hz (20-500)
    void update(float surround, float crystalizer, float bass,
                float smartVolume, float dialogPlus, float crossover) {
        // Surround: M/S cross-feed matrix (matches offscreen.js llGain/lrGain etc.)
        float w = (std::min(100.f, std::max(0.f, surround)) / 100.f) * 1.5f;
        ll = rr = 1.f + 0.5f*w;
        lr = rl = -0.5f*w;

        // Crystalizer: high-shelf + saturation
        float cAmt = std::min(100.f, std::max(0.f, crystalizer)) / 100.f;
        crystalShelf.setGain(cAmt * 9.f);
        makeSaturationCurve(cAmt * 0.4f);

        // Bass shelf
        float bAmt = std::min(100.f, std::max(0.f, bass)) / 100.f;
        bassFilter.setGain(bAmt * 12.f);
        bassFilter.setFrequency(std::min(500.f, std::max(20.f, crossover)));

        // Dialog Plus (peaking around 2.8kHz)
        float dAmt = std::min(100.f, std::max(0.f, dialogPlus)) / 100.f;
        dialogFilter.setGain(dAmt * 10.f);

        // Smart Volume (compressor)
        float sAmt = std::min(100.f, std::max(0.f, smartVolume)) / 100.f;
        smartVol.setThreshold(-6.f - sAmt * 40.f);
        smartVol.setRatio(1.f + sAmt * 11.f);
        smartVolMakeup = 1.f + sAmt * 1.2f;
    }

    void processStereo(float& l, float& r) {
        // 1. Bass shelf
        bassFilter.processStereo(l, r);

        // 2. Crystalizer: high-shelf + soft saturation
        crystalShelf.processStereo(l, r);
        l = applySaturation(l);
        r = applySaturation(r);

        // 3. Dialog Plus (peaking mid-presence)
        dialogFilter.processStereo(l, r);

        // 4. Smart Volume
        smartVol.processStereo(l, r);
        l *= smartVolMakeup;
        r *= smartVolMakeup;

        // 5. Surround (M/S cross-feed)
        float nl = ll*l + lr*r;
        float nr = rl*l + rr*r;
        l = nl; r = nr;
    }

    void reset() {
        bassFilter.reset();
        crystalShelf.reset();
        dialogFilter.reset();
        smartVol.reset();
    }

private:
    double sampleRate = 44100.0;

    BiquadFilter bassFilter, crystalShelf, dialogFilter;
    Compressor   smartVol;

    float smartVolMakeup = 1.f;
    float ll=1, lr=0, rl=0, rr=1;

    // Wave shaper for Crystalizer (tanh-like soft saturation)
    std::vector<float> shapingCurve;
    int shapingCurveSize;

    void makeSaturationCurve(float amount) {
        float k = amount * 20.f;
        for (int i = 0; i < shapingCurveSize; ++i) {
            float x = (float)i / (shapingCurveSize-1) * 2.f - 1.f;
            if (k == 0.f)
                shapingCurve[i] = x;
            else
                shapingCurve[i] = ((1.f+k)*x) / (1.f + k*std::abs(x));
        }
    }

    float applySaturation(float x) const {
        // Map x in [-1,1] to curve index [0, size-1]
        float clamped = std::max(-1.f, std::min(1.f, x));
        float idx = (clamped + 1.f) * 0.5f * (shapingCurveSize - 1);
        int i0 = (int)idx;
        float frac = idx - i0;
        int i1 = std::min(i0+1, shapingCurveSize-1);
        return shapingCurve[i0]*(1.f-frac) + shapingCurve[i1]*frac;
    }
};
