// SPDX-License-Identifier: BSD-3-Clause
// Polyphase windowed-sinc converter: the same design as OTIS's OutputConverter
// (64 taps, 1024 phases, Blackman window), copied rather than shared so OTIS's
// bit-exact regression traces stay untouched. Fixed arrays; no audio-thread
// allocation. Constant latency of about taps/2 input samples.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace espish {

struct RateConverter {
    static constexpr int taps = 64, phases = 1024;
    std::array<std::array<float, taps>, phases> kernels{};
    // History stored twice so the newest `taps` samples are always contiguous.
    std::array<float, 2 * taps> history{};
    unsigned head = 0;
    // Cutoff is relative to the input rate and band-limits to the lower of the two rates.
    void configure(double inputRate, double outputRate) {
        const double pi = 3.14159265358979323846;
        double cutoff = .46 * std::min(1.0, outputRate / inputRate);
        for (int p = 0; p < phases; p++) {
            double fraction = double(p) / phases, sum = 0;
            for (int k = 0; k < taps; k++) {
                double d = k - (taps / 2.0 - 1) + fraction;
                double window = .42 - .5 * std::cos(2 * pi * k / (taps - 1)) + .08 * std::cos(4 * pi * k / (taps - 1));
                double h = std::abs(d) < 1e-12 ? 2 * cutoff : std::sin(2 * pi * cutoff * d) / (pi * d);
                kernels[p][k] = float(h * window);
                sum += h * window;
            }
            for (auto& h : kernels[p]) h = float(h / sum);
        }
        reset();
    }
    void reset() { history.fill(0); head = 0; }
    void push(float x) { head = (head + 1) % taps; history[head] = history[head + taps] = x; }
    // Value `fraction` (0..1) of an input period after the reference point.
    float read(double fraction) const {
        int p = std::max(0, std::min(phases - 1, int(fraction * phases)));
        // history[head + taps - k] for k = 0..taps-1, read backwards without wrapping.
        const float* h = &history[head + taps];
        const float* kern = kernels[p].data();
        float x = 0;
        for (int k = 0; k < taps; k++) x += h[-k] * kern[k];
        return x;
    }
};

} // namespace espish
