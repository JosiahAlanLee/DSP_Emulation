// SPDX-License-Identifier: BSD-3-Clause
// Eight spectral bands <-> one byte. Reuses the row's frame; no FFT or allocation.
#pragma once
#include "Spectral.hpp"
#include <array>

namespace seance {

struct BridgeParams {
    float thresholdDb = -36.f, hysteresisDb = 6.f, slewMs = 20.f;
};

struct SpectralBridge {
    inline static constexpr float EDGES[7] = {125, 250, 500, 1000, 2000, 4000, 8000};
    std::array<uint8_t, MAX_BINS> bands{};
    std::array<float, 8> gain{{1, 1, 1, 1, 1, 1, 1, 1}};
    std::array<double, 8> power{};  // pre-mask, Hann-compensated RMS squared
    uint8_t activity = 0;
    int size = 0;
    float rate = 0;

    static int bandAt(float hz) {
        int band = 0;
        while (band < 7 && hz >= EDGES[band]) ++band;
        return band;
    }
    void clear() {
        gain.fill(1); power.fill(0); activity = 0; size = 0; rate = 0;
    }
    static float finite(float x, float fallback, float lo, float hi) {
        return std::isfinite(x) ? clampf(x, lo, hi) : fallback;
    }
    uint8_t process(FrameV1& f, uint8_t mask, const BridgeParams& p) {
        if (size != f.size || rate != f.sampleRate) {
            clear(); size = f.size; rate = f.sampleRate;
            for (int k = 0; k < f.bins; ++k) bands[k] = uint8_t(bandAt(k * rate / size));
        }
        power.fill(0);
        const double scale = 1. / (double(size) * size * .375);
        for (int k = 0; k < f.bins; ++k) {
            double mag = f.mag[k];
            if (std::isfinite(mag) && mag > 0)
                power[bands[k]] += mag * mag * (k == 0 || k == f.bins - 1 ? 1. : 2.) * scale;
        }
        float threshold = finite(p.thresholdDb, -36, -72, 0);
        float hysteresis = finite(p.hysteresisDb, 6, 0, 24);
        double on = std::pow(10., threshold * .1), off = std::pow(10., (threshold - hysteresis) * .1);
        float slew = finite(p.slewMs, 20, 0, 250);
        float alpha = slew > 0 ? float(-std::expm1(-double(f.hop) / (rate * slew * .001))) : 1.f;
        for (int b = 0; b < 8; ++b) {
            uint8_t bit = uint8_t(1u << b);
            bool wasOn = (activity & bit) != 0;
            bool active = power[b] > 0 && (hysteresis == 0 ? power[b] >= on : wasOn ? power[b] > off : power[b] >= on);
            if (active) activity |= bit; else activity &= uint8_t(~bit);
            float target = mask & bit ? 1.f : 0.f;
            gain[b] += (target - gain[b]) * alpha;
            if (std::abs(gain[b] - target) < 1e-6f) gain[b] = target;
        }
        for (int k = 0; k < f.bins; ++k) {
            float g = gain[bands[k]];
            if (g == 0) f.mag[k] = 0;
            else if (g != 1) f.mag[k] *= g;
        }
        return activity;
    }
};

} // namespace seance
