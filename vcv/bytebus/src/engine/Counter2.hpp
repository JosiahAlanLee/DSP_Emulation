// SPDX-License-Identifier: BSD-3-Clause
// A phase word at a ratio of its clock: each acquisition edge adds N/D of a cycle, kept exactly
// with an integer accumulator, so after n edges the word is floor(256 * frac(n * N / D)) and never
// drifts. Clock ownership, RESET/LOAD and strobes are the Clocked Register's (ClockOwner.hpp).
// Contract: docs/COUNTER2.md.
#pragma once
#include "ClockOwner.hpp"

namespace dspemu::counter2 {
using clock_owner::ClockMode;
using clock_owner::ClockSource;
using clock_owner::Inputs;

constexpr int MAX_N = 16, MAX_D = 64;

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    double rateHz = 8;
    int n = 1, d = 16;       // cycles per edge = n / d
    uint8_t seed = 0;        // the phase word RESET/LOAD starts from
};
struct Output {
    uint8_t word = 0;
    bool clocked = true, clk = false, advanced = false, loaded = false, carry = false;
    ClockSource source = ClockSource::Internal;
};

struct Counter2 : clock_owner::ClockOwner {
    // The phase in units of 1 / (256 * d): word = acc / d.
    int64_t acc = 0;
    int d = 16;

    void clear(uint8_t word = 0, int denominator = 16) {
        d = denominator; acc = int64_t(word) * d; ClockOwner::clear();
    }
    uint8_t word() const { return uint8_t(acc / d); }
    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        int n = std::max(1, std::min(MAX_N, p.n)), dNew = std::max(1, std::min(MAX_D, p.d));
        // A new D keeps the current word; the fraction below it restarts.
        if (dNew != d) { acc = int64_t(word()) * dNew; d = dNew; }
        auto step = ClockOwner::tick(p.mode, p.rateHz, in, sampleRate);
        Output out;
        if (step.seed) acc = int64_t(p.seed) * d;
        if (step.acquire) {
            acc += int64_t(256) * n;
            out.carry = acc >= int64_t(256) * d;      // the phase passed the end of a cycle
            acc %= int64_t(256) * d;
        }
        out.word = word(); out.clocked = step.clocked; out.clk = step.clk;
        out.advanced = step.acquire; out.loaded = step.loaded; out.source = step.source;
        return out;
    }
};
} // namespace dspemu::counter2
