// SPDX-License-Identifier: BSD-3-Clause
// An 8-bit word that counts: on each acquisition edge, word = (word + increment) mod modulus.
// Clock ownership, RESET/LOAD and strobes are the Clocked Register's (ClockOwner.hpp).
// Contract: docs/COUNTER.md.
#pragma once
#include "ClockOwner.hpp"

namespace dspemu::counter {
using clock_owner::ClockMode;
using clock_owner::ClockSource;
using clock_owner::Inputs;

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    double rateHz = 8;
    uint8_t increment = 1;   // used when BUS IN is unpatched
    int modulus = 256;       // 2-256
    uint8_t seed = 0;
};
struct Output {
    uint8_t word = 0;
    bool clocked = true, clk = false, advanced = false, loaded = false, carry = false;
    ClockSource source = ClockSource::Internal;
};

struct Counter : clock_owner::ClockOwner {
    uint8_t word = 0;

    void clear(uint8_t value = 0) {
        word = value; ClockOwner::clear();
    }
    // A held word at or above the modulus is published as it is and wraps on the next edge.
    static uint8_t next(uint8_t word, uint8_t increment, int modulus, bool& carry) {
        int sum = int(word) + increment;
        carry = sum >= modulus;
        return uint8_t(sum % modulus);
    }
    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        auto step = ClockOwner::tick(p.mode, p.rateHz, in, sampleRate);
        Output out;
        if (step.seed) word = p.seed;
        if (step.acquire) {
            // BUS IN's data lines, sampled on this module's own edge, replace INC.
            uint8_t increment = in.data.connected ? in.data.rawWord : p.increment;
            word = next(word, increment, std::max(2, std::min(256, p.modulus)), out.carry);
        }
        out.word = word; out.clocked = step.clocked; out.clk = step.clk;
        out.advanced = step.acquire; out.loaded = step.loaded; out.source = step.source;
        return out;
    }
};
} // namespace dspemu::counter
