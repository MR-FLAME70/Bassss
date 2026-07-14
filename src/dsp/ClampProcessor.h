#pragma once
#include <cmath>

// Exact port of clamp-worklet.js — soft-knee tanh limiting above 0.8.
struct ClampProcessor {
    inline void processStereo(float& l, float& r) const {
        l = process(l);
        r = process(r);
    }
    inline float process(float v) const {
        constexpr float threshold = 0.8f;
        constexpr float headroom  = 1.f - threshold; // 0.2
        float av = std::abs(v);
        if (av <= threshold) return v;
        float sign   = v < 0.f ? -1.f : 1.f;
        float excess = av - threshold;
        return sign * (threshold + headroom * std::tanh(excess / headroom));
    }
};
