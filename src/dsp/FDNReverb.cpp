#include "FDNReverb.h"

// constexpr static member definitions (C++17 inline, but explicit instantiation here)
constexpr std::array<double, FDNReverb::N> FDNReverb::BASE_DELAY_SEC;
constexpr std::array<float,  FDNReverb::N> FDNReverb::MOD_RATE_MUL;
constexpr std::array<float,  FDNReverb::N> FDNReverb::DIFF_MUL;
constexpr std::array<float,  FDNReverb::N> FDNReverb::TAP_L;
constexpr std::array<float,  FDNReverb::N> FDNReverb::TAP_R;
constexpr std::array<float, 4>             FDNReverb::IN_DIFF_COEFFS;
