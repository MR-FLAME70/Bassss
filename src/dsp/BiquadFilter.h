#pragma once
#include <cmath>

// ──────────────────────────────────────────────────────────────────────────────
// BiquadFilter — exact Web Audio API biquad implementation.
// All coefficient formulas match the W3C Web Audio API specification so the
// frequency response is bit-identical to what the Chrome extension produced.
// ──────────────────────────────────────────────────────────────────────────────

class BiquadFilter {
public:
    enum Type {
        LowPass, HighPass, BandPass,
        LowShelf, HighShelf, Peaking,
        Notch, AllPass
    };

    BiquadFilter() { reset(); setType(LowPass, 44100.0, 350.0, 1.0, 0.0); }

    void setType(Type t, double sr, double freq, double q, double gainDb) {
        type       = t;
        sampleRate = sr;
        frequency  = freq;
        Q          = q;
        gainDb_    = gainDb;
        computeCoeffs();
    }

    // Convenience setters (each recomputes coefficients)
    void setFrequency(double f) { frequency = f; computeCoeffs(); }
    void setQ(double q)        { Q = q; computeCoeffs(); }
    void setGain(double gDb)   { gainDb_ = gDb; computeCoeffs(); }
    void setSampleRate(double sr){ sampleRate = sr; computeCoeffs(); }

    // Process one stereo frame in-place
    inline void processStereo(float& l, float& r) {
        float ol = processOne(l, x1l, x2l, y1l, y2l);
        float or_ = processOne(r, x1r, x2r, y1r, y2r);
        l = ol; r = or_;
    }

    // Process one mono sample
    inline float processMono(float x) {
        return processOne(x, x1l, x2l, y1l, y2l);
    }

    void reset() {
        x1l=x2l=y1l=y2l = 0.f;
        x1r=x2r=y1r=y2r = 0.f;
    }

private:
    Type   type;
    double sampleRate = 44100.0;
    double frequency  = 350.0;
    double Q          = 1.0;
    double gainDb_    = 0.0;

    // Biquad coefficients (normalised by a0)
    double b0=1, b1=0, b2=0, a1=0, a2=0;

    // State variables (separate L/R)
    float x1l,x2l,y1l,y2l;
    float x1r,x2r,y1r,y2r;

    inline float processOne(float x, float& x1, float& x2, float& y1, float& y2) {
        double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x;
        y2=y1; y1=(float)y;
        return (float)y;
    }

    void computeCoeffs() {
        const double pi = M_PI;
        double w0  = 2.0*pi*frequency/sampleRate;
        double cw0 = std::cos(w0);
        double sw0 = std::sin(w0);
        double A   = std::pow(10.0, gainDb_/40.0);
        double alpha = sw0 / (2.0*Q);
        double a0;

        switch (type) {
        case LowPass:
            b0=(1-cw0)/2; b1=1-cw0; b2=(1-cw0)/2;
            a0=1+alpha; a1=-2*cw0; a2=1-alpha;
            break;
        case HighPass:
            b0=(1+cw0)/2; b1=-(1+cw0); b2=(1+cw0)/2;
            a0=1+alpha; a1=-2*cw0; a2=1-alpha;
            break;
        case BandPass:
            b0=alpha; b1=0; b2=-alpha;
            a0=1+alpha; a1=-2*cw0; a2=1-alpha;
            break;
        case LowShelf: {
            double sqrtA = std::sqrt(A);
            // Web Audio API shelf slope S=1 → alpha = sin(w0)/sqrt(2)
            const double S = 1.0;
            alpha = sw0/2.0 * std::sqrt((A+1.0/A)*(1.0/S - 1.0) + 2.0);
            b0 = A*((A+1)-(A-1)*cw0+2*sqrtA*alpha);
            b1 = 2*A*((A-1)-(A+1)*cw0);
            b2 = A*((A+1)-(A-1)*cw0-2*sqrtA*alpha);
            a0 = (A+1)+(A-1)*cw0+2*sqrtA*alpha;
            a1 = -2*((A-1)+(A+1)*cw0);
            a2 = (A+1)+(A-1)*cw0-2*sqrtA*alpha;
            break;
        }
        case HighShelf: {
            double sqrtA = std::sqrt(A);
            double S = 1.0;
            alpha = sw0/2.0 * std::sqrt((A+1.0/A)*(1.0/S - 1.0) + 2.0);
            b0 = A*((A+1)+(A-1)*cw0+2*sqrtA*alpha);
            b1 = -2*A*((A-1)+(A+1)*cw0);
            b2 = A*((A+1)+(A-1)*cw0-2*sqrtA*alpha);
            a0 = (A+1)-(A-1)*cw0+2*sqrtA*alpha;
            a1 = 2*((A-1)-(A+1)*cw0);
            a2 = (A+1)-(A-1)*cw0-2*sqrtA*alpha;
            break;
        }
        case Peaking:
            b0 = 1+alpha*A; b1 = -2*cw0; b2 = 1-alpha*A;
            a0 = 1+alpha/A; a1 = -2*cw0; a2 = 1-alpha/A;
            break;
        case Notch:
            b0 = 1; b1 = -2*cw0; b2 = 1;
            a0 = 1+alpha; a1 = -2*cw0; a2 = 1-alpha;
            break;
        case AllPass:
            b0 = 1-alpha; b1 = -2*cw0; b2 = 1+alpha;
            a0 = 1+alpha; a1 = -2*cw0; a2 = 1-alpha;
            break;
        }
        b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0;
    }
};
