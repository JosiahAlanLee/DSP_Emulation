// SPDX-License-Identifier: BSD-3-Clause
// An 8-bit word that counts: on each acquisition edge the word moves by the increment,
// wrapping at the modulus (Loop), holding at its top (One-shot) or reflecting at both
// ends (Pendulum). Clock ownership, RESET/LOAD and strobes are the Clocked Register's
// (ClockOwner.hpp). Contract: docs/COUNTER.md.
#pragma once
#include "ClockOwner.hpp"

namespace dspemu::counter {
using clock_owner::ClockMode;
using clock_owner::ClockSource;
using clock_owner::Inputs;

enum class RunMode { Loop, OneShot, Pendulum };

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    RunMode run = RunMode::Loop;
    double rateHz = 8;
    uint8_t increment = 1;   // used when BUS IN is unpatched
    int modulus = 256;       // 2-256
    uint8_t seed = 0;
};
struct Output {
    uint8_t word = 0;
    bool clocked = true, clk = false, advanced = false, loaded = false, carry = false;
    bool down = false;       // Pendulum direction after this sample
    ClockSource source = ClockSource::Internal;
};

struct Counter : clock_owner::ClockOwner {
    uint8_t word = 0;
    bool down = false;       // Pendulum direction; only reflections and RESET/LOAD change it

    void clear(uint8_t value = 0, bool descending = false) {
        word = value; down = descending; ClockOwner::clear();
    }
    // Loop. A held word at or above the modulus is published as it is and wraps on the next edge.
    static uint8_t next(uint8_t word, uint8_t increment, int modulus, bool& carry) {
        int sum = int(word) + increment;
        carry = sum >= modulus;
        return uint8_t(sum % modulus);
    }
    // One-shot: count up and hold at modulus - 1, with CARRY on the edge that arrives there.
    static uint8_t nextOneShot(uint8_t word, uint8_t increment, int modulus, bool& carry) {
        int top = modulus - 1, sum = int(word) + increment;
        carry = sum >= top && word != top;
        return uint8_t(std::min(sum, top));
    }
    // Pendulum: the increment is a magnitude. A step that reaches or passes an end stops on
    // it and reverses, with CARRY. A word above the top goes to the top and turns down.
    static uint8_t nextPendulum(uint8_t word, uint8_t increment, int modulus, bool& down, bool& carry) {
        int top = modulus - 1;
        carry = false;
        if (word > top) { down = carry = true; return uint8_t(top); }
        if (increment == 0) return word;
        int sum = down ? int(word) - increment : int(word) + increment;
        if (!down && sum >= top) { down = carry = true; return uint8_t(top); }
        if (down && sum <= 0) { down = false; carry = true; return 0; }
        return uint8_t(sum);
    }
    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        auto step = ClockOwner::tick(p.mode, p.rateHz, in, sampleRate);
        Output out;
        if (step.seed) { word = p.seed; down = false; }
        if (step.acquire) {
            // BUS IN's data lines, sampled on this module's own edge, replace INC.
            uint8_t increment = in.data.connected ? in.data.rawWord : p.increment;
            int modulus = std::max(2, std::min(256, p.modulus));
            switch (p.run) {
                case RunMode::OneShot: word = nextOneShot(word, increment, modulus, out.carry); break;
                case RunMode::Pendulum: word = nextPendulum(word, increment, modulus, down, out.carry); break;
                default: word = next(word, increment, modulus, out.carry); break;
            }
        }
        out.word = word; out.down = down; out.clocked = step.clocked; out.clk = step.clk;
        out.advanced = step.acquire; out.loaded = step.loaded; out.source = step.source;
        return out;
    }
};
} // namespace dspemu::counter
