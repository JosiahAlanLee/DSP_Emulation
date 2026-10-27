// SPDX-License-Identifier: BSD-3-Clause
// An eight-stage shift register whose new bit is the parity of the stages MASK taps, XOR or XNOR,
// XOR a comparator on INJECT; the stages, weighted, sum to a CV and a zero-mean AUDIO output: an
// FIR filter on the register's serial stream at the clock rate. After the Burhans Timbre
// (Nonlinearcircuits), its Q / off / not-Q taps reduced to MASK and SENSE. Clock ownership,
// RESET and LOAD are ClockOwner's; the MASK bus is its data input. Contract: docs/PARALLEL_SUM.md.
#pragma once
#include "ClockOwner.hpp"
#include <array>
#include <cmath>

namespace dspemu::parallel_sum {
using clock_owner::ClockMode;

constexpr int STAGES = 8;
constexpr float VOLTS = 1.25f, HOT_VOLTS = 6.f, HOT_CLIP = 10.5f;
constexpr float THRESHOLD = 1.2f, HYSTERESIS = .05f, DETENT = .02f;

enum class Sense { Xor, Xnor };

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    double rateHz = 8;
    uint8_t mask = 0xb8;
    Sense sense = Sense::Xnor;
    float threshold = THRESHOLD;        // INJECT's comparator, -5 ... +5 V
    std::array<float, STAGES> weights{1, 1, 1, 1, 1, 1, 1, 1};   // knobs, -1 ... +1
    bool hot = false;
    uint8_t seed = 0;
};
struct Inputs {
    clock_owner::Inputs clock;          // CLOCK, RESET, LOAD; data is the MASK bus
    bool injectConnected = false;
    float inject = 0;
    std::array<float, STAGES> weightCv{};   // W CV channel k, volts; 0 when absent
};
struct Output {
    uint8_t word = 0, mask = 0;
    bool clocked = true, clk = false, loaded = false, advanced = false;
    bool inject = false;                // the comparator's state
    float cv = 0, audio = 0;
    bool gate = false;                  // stage 0
};

inline int parity(unsigned x) { x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; return int(x & 1); }
// One shift: the new stage 0 is the parity of the tapped stages, inverted by XNOR, XOR inject.
inline uint8_t step(uint8_t word, uint8_t mask, Sense sense, bool inject) {
    int bit = parity(word & mask) ^ (sense == Sense::Xnor ? 1 : 0) ^ (inject ? 1 : 0);
    return uint8_t((word << 1 | bit) & 0xff);
}
// A stage's weight: the knob, with a small dead zone at its centre, plus W CV at 5 V per unit.
inline float weight(float knob, float cv) {
    if (!std::isfinite(knob)) knob = 0;
    if (!std::isfinite(cv)) cv = 0;
    if (std::abs(knob) < DETENT) knob = 0;
    return std::max(-1.f, std::min(1.f, knob + cv / 5.f));
}

struct Comparator {
    bool high = false;
    bool process(float v, float threshold) {
        if (!std::isfinite(v)) v = 0;
        if (!std::isfinite(threshold)) threshold = THRESHOLD;
        threshold = std::max(-5.f, std::min(5.f, threshold));
        if (v >= threshold + HYSTERESIS) high = true;
        else if (v <= threshold - HYSTERESIS) high = false;
        return high;
    }
};

struct ParallelSum {
    clock_owner::ClockOwner owner;
    Comparator comparator;
    uint8_t word = 0;
    uint8_t mask = 0xb8;                // the MASK bus as last sampled, or the knob

    void clear(uint8_t value = 0) { owner.clear(); comparator = {}; word = value; mask = 0xb8; }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        bool injected = false;
        if (in.injectConnected) injected = comparator.process(in.inject, p.threshold);
        else comparator = {};
        bool busMask = in.clock.data.connected;
        if (!busMask) mask = p.mask;
        auto s = owner.tick(p.mode, p.rateHz, in.clock, sampleRate);
        if (s.seed) word = p.seed;
        if (s.acquire) {
            // The MASK bus's data lines, sampled on this module's edge, as Counter reclocks BUS IN.
            if (busMask) mask = in.clock.data.rawWord;
            word = step(word, mask, p.sense, injected);
            out.advanced = true;
        }
        float cv = 0, audio = 0;
        for (int k = 0; k < STAGES; ++k) {
            float w = weight(p.weights[k], in.weightCv[k]);
            bool bit = word >> k & 1;
            cv += bit ? w : 0.f;
            audio += bit ? w : -w;
        }
        float volts = p.hot ? HOT_VOLTS : VOLTS;
        out.cv = volts * cv; out.audio = volts * audio;
        if (p.hot) {
            out.cv = std::max(-HOT_CLIP, std::min(HOT_CLIP, out.cv));
            out.audio = std::max(-HOT_CLIP, std::min(HOT_CLIP, out.audio));
        }
        out.word = word; out.mask = mask; out.gate = word & 1; out.inject = injected;
        out.clocked = s.clocked; out.clk = s.clk; out.loaded = s.loaded;
        return out;
    }
};
} // namespace dspemu::parallel_sum
