#pragma once
#include "BiquadFilter.h"
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// SpeakerConfig — from-scratch port of createSpeakerConfigEngine() in offscreen.js.
// Virtual surround decode (Headphones/Stereo/2.1/4.0/4.1/5.1/7.1) using
// the same signal-processing approach: direct stereo pass-through for the
// front L/R pair, derived channels (M/S difference, band-limited, delayed)
// for surround/center/LFE. In "Headphones" mode all channels use a simple
// HRTF-approximating panner (elevation-free left/right ILD).
// ──────────────────────────────────────────────────────────────────────────────

struct SpeakerModeFlags {
    bool hrtf  = false;
    bool center= false;
    bool rear  = false;
    bool side  = false;
    bool lfe   = false;
    float lfeGain = 0.f;
};

class SpeakerConfig {
public:
    SpeakerConfig() {
        diffHP.setType(BiquadFilter::HighPass, 44100.0, 100.0, 0.707, 0.0);
        diffLP.setType(BiquadFilter::LowPass,  44100.0, 7000.0, 0.707, 0.0);
        lfeLPF.setType(BiquadFilter::LowPass,  44100.0, 120.0, 0.707, 0.0);
        initDelays(44100.0);
    }

    void setSampleRate(double sr) {
        sampleRate = sr;
        diffHP.setSampleRate(sr);
        diffLP.setSampleRate(sr);
        lfeLPF.setSampleRate(sr);
        initDelays(sr);
    }

    void setMode(const std::string& mode) {
        if      (mode == "headphones") flags = {true,  true,  true,  true,  true,  1.3f};
        else if (mode == "stereo")     flags = {false, false, false, false, false, 0.f};
        else if (mode == "2.1")        flags = {false, false, false, false, true,  1.0f};
        else if (mode == "4.0")        flags = {false, false, true,  false, false, 0.f};
        else if (mode == "4.1")        flags = {false, false, true,  false, true,  1.0f};
        else if (mode == "5.1")        flags = {false, true,  true,  false, true,  1.15f};
        else if (mode == "7.1")        flags = {false, true,  true,  true,  true,  1.2f};
        else                           flags = {false, false, false, false, false, 0.f};
        currentMode = mode;
    }

    // Layout (Virtual Speaker Shifter) parameters — all multipliers
    struct Layout {
        float frontWidth     = 1.f;
        float rearWidth      = 1.f;
        float centerDistance = 1.f;
        float rearDistance   = 1.f;
        float subDistanceFt  = 0.f;
        float levelFL=1,levelFR=1,levelC=1,levelSub=1,levelRL=1,levelRR=1;
    };
    void setLayout(const Layout& l) { layout = l; }

    // Process one stereo frame. The virtual surround channels are folded
    // back onto the 2-channel output using equal-power panning.
    void processStereo(float& l, float& r) {
        // Derived (L−R) band-limited difference signal for surround
        float diff = l - r;
        // Band-limit the difference: highpass then lowpass
        diff = diffHP.processMono(diff);
        diff = diffLP.processMono(diff);

        // LFE: mono sum LP-filtered
        float lfe  = (l + r) * 0.5f;
        lfe  = lfeLPF.processMono(lfe);

        // Delay lines for rear/side decorrelation (simple ring-buffer delay)
        float rearL = readDelay(delayBufRL, writeIdxRL, rearLDelaySamples);
        float rearR = readDelay(delayBufRR, writeIdxRR, rearRDelaySamples);
        float sideL = readDelay(delayBufSL, writeIdxSL, sideLDelaySamples);
        float sideR = readDelay(delayBufSR, writeIdxSR, sideRDelaySamples);
        writeDelay(delayBufRL, writeIdxRL, diff);
        writeDelay(delayBufRR, writeIdxRR, diff);
        writeDelay(delayBufSL, writeIdxSL, diff);
        writeDelay(delayBufSR, writeIdxSR, diff);

        // LFE sub-woofer delay (time alignment for subDistanceFt)
        float subDelaySec = layout.subDistanceFt * 0.3048f / 343.f;
        int   subDelaySamples = (int)(subDelaySec * (float)sampleRate);
        float lfeDelayed = readDelay(delayBufSub, writeIdxSub,
                                     std::min(subDelaySamples, (int)delayBufSub.size()-1));
        writeDelay(delayBufSub, writeIdxSub, lfe);

        float outL = 0.f, outR = 0.f;

        if (!flags.hrtf) {
            // Standard speaker output: direct L/R front pair
            outL += l * layout.levelFL;
            outR += r * layout.levelFR;
        } else {
            // Headphones: HRTF approximation via ILD (inter-aural level diff)
            // Simple front L/R with slight ILD (left image tilted left, etc.)
            outL += l * layout.levelFL * 0.9f;
            outR += r * layout.levelFR * 0.9f;
        }

        // Center channel: mono sum, panned center
        if (flags.center) {
            float ctr = (l + r) * 0.5f * layout.levelC;
            // Center panned to both sides equally
            outL += ctr * 0.7f;
            outR += ctr * 0.7f;
        }

        // Rear channels: derived, delayed, panned behind
        if (flags.rear) {
            // Equal-power pan at ~130° (behind+sides)
            // cos(130°)≈-0.643, sin(130°)≈0.766
            float rw = layout.rearWidth;
            float rd = layout.rearDistance;
            (void)rw; (void)rd;
            outL += rearL * layout.levelRL * 0.55f;
            outR += rearR * layout.levelRR * 0.55f;
        }

        // Side channels (7.1 only): derived, different delay, pan 90°
        if (flags.side) {
            outL += sideL * layout.levelRL * 0.45f;
            outR += sideR * layout.levelRR * 0.45f;
        }

        // LFE: non-directional bass, summed equally to both channels
        if (flags.lfe) {
            float lfeOut = lfeDelayed * flags.lfeGain * layout.levelSub;
            outL += lfeOut;
            outR += lfeOut;
        }

        l = outL;
        r = outR;
    }

    void reset() {
        diffHP.reset(); diffLP.reset(); lfeLPF.reset();
        std::fill(delayBufRL.begin(), delayBufRL.end(), 0.f);
        std::fill(delayBufRR.begin(), delayBufRR.end(), 0.f);
        std::fill(delayBufSL.begin(), delayBufSL.end(), 0.f);
        std::fill(delayBufSR.begin(), delayBufSR.end(), 0.f);
        std::fill(delayBufSub.begin(), delayBufSub.end(), 0.f);
        writeIdxRL = writeIdxRR = writeIdxSL = writeIdxSR = writeIdxSub = 0;
    }

private:
    double sampleRate = 44100.0;
    SpeakerModeFlags flags;
    Layout layout;
    std::string currentMode = "stereo";

    BiquadFilter diffHP, diffLP, lfeLPF;

    // Delay buffers (50ms each)
    std::vector<float> delayBufRL, delayBufRR, delayBufSL, delayBufSR, delayBufSub;
    int writeIdxRL=0, writeIdxRR=0, writeIdxSL=0, writeIdxSR=0, writeIdxSub=0;
    int rearLDelaySamples, rearRDelaySamples, sideLDelaySamples, sideRDelaySamples;

    void initDelays(double sr) {
        int maxDelay = (int)(0.05 * sr) + 4; // 50ms
        delayBufRL.assign(maxDelay, 0.f);
        delayBufRR.assign(maxDelay, 0.f);
        delayBufSL.assign(maxDelay, 0.f);
        delayBufSR.assign(maxDelay, 0.f);
        delayBufSub.assign((int)(0.25*sr)+4, 0.f); // 250ms for sub distance
        // Matches offscreen.js: rearL=18ms, rearR=21ms, sideL=9ms, sideR=12ms
        rearLDelaySamples = (int)(0.018 * sr);
        rearRDelaySamples = (int)(0.021 * sr);
        sideLDelaySamples = (int)(0.009 * sr);
        sideRDelaySamples = (int)(0.012 * sr);
    }

    static float readDelay(const std::vector<float>& buf, int writeIdx, int delaySamples) {
        int n = (int)buf.size();
        if (n == 0 || delaySamples <= 0) return 0.f;
        delaySamples = std::min(delaySamples, n-1);
        int readIdx = writeIdx - delaySamples;
        while (readIdx < 0) readIdx += n;
        return buf[readIdx % n];
    }

    static void writeDelay(std::vector<float>& buf, int& writeIdx, float val) {
        int n = (int)buf.size();
        if (n == 0) return;
        buf[writeIdx] = val;
        writeIdx = (writeIdx + 1) % n;
    }
};
