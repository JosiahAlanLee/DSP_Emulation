// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "ClockOwner.hpp"

namespace dspemu::byte_register {
using clock_owner::ClockMode;
using clock_owner::ClockSource;
using clock_owner::Inputs;

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    double rateHz = 8;
    uint8_t resetWord = 0;
};
struct Output {
    uint8_t word = 0;
    bool clocked = true, clk = false, captured = false, loaded = false;
    ClockSource source = ClockSource::Internal;
};

// One word of state. Raw data is sampled on THIS register's selected edge;
// upstream CLK is never an additional latch when a local clock is selected.
struct Register : clock_owner::ClockOwner {
    uint8_t word = 0;

    void load(uint8_t value) {
        word = value; restart();
    }
    void clear(uint8_t value = 0) {
        word = value; ClockOwner::clear();
    }
    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        auto step = ClockOwner::tick(p.mode, p.rateHz, in, sampleRate);
        if (step.seed) word = p.resetWord;
        if (step.acquire && in.data.connected) word = in.data.rawWord;
        Output out;
        out.word = word; out.clocked = step.clocked; out.clk = step.clk;
        out.captured = step.acquire; out.loaded = step.loaded; out.source = step.source;
        return out;
    }
};
} // namespace dspemu::byte_register
