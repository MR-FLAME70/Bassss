#pragma once
#include <algorithm>

// Mid/Side stereo width control — exact port of createStereoWidthModule in offscreen.js.
// 0%=mono, 100%=bypass, 200%=double side.
// M = 0.5*(L+R), S = 0.5*(L−R)
// L' = M + side*factor, R' = M − side*factor
class StereoWidth {
public:
    void setWidth(float percent) {
        float factor = std::max(0.f, std::min(200.f, percent)) / 100.f;
        float mid  = 0.5f;
        float side = 0.5f * factor;
        ll = mm = mid + side;
        rr = mid + side;
        lr = rl = mid - side;
    }

    void processStereo(float& l, float& r) const {
        float nl = ll*l + lr*r;
        float nr = rl*l + rr*r;
        l = nl; r = nr;
    }

private:
    // Gain matrix (same as offscreen.js llGain/lrGain/rlGain/rrGain)
    float ll=1, lr=0, rl=0, rr=1;
    float mm=1; // unused separately, merged into ll/rr above
};
