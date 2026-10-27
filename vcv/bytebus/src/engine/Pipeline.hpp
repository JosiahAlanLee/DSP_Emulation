// SPDX-License-Identifier: BSD-3-Clause
// A byte delay line: on each acquisition edge the input word enters a 128-word history and the
// published word is the one that entered DELAY edges ago. LOOP feeds the outgoing word back in
// place of the input, so the last DELAY words repeat; SPREAD delays bit k by k * SPREAD more
// edges, so the lanes of a rhythm word enter one after another. Clock ownership, RESET and
// strobes are the Clocked Register's (ClockOwner.hpp). Contract: docs/PIPELINE.md.
#pragma once
#include "ClockOwner.hpp"
#include <array>

namespace dspemu::pipeline {
using clock_owner::ClockMode;
using clock_owner::ClockSource;
using clock_owner::Inputs;

constexpr int STAGES = 128, MAX_DELAY = 64, MAX_SPREAD = 8;   // 64 + 7 * 8 < 128

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    double rateHz = 8;
    int delay = 16;          // 1-64 edges
    int spread = 0;          // 0-8 more edges per bit
    bool loop = false;
};
struct Output {
    uint8_t word = 0;
    bool clocked = true, clk = false, advanced = false, loaded = false, cleared = false;
    ClockSource source = ClockSource::Internal;
};

struct Pipeline : clock_owner::ClockOwner {
    std::array<uint8_t, STAGES> ring{};
    int head = 0;            // ring[head] entered on the latest edge
    uint8_t word = 0;        // the published word, held between edges

    void clear() {
        ring.fill(0); head = 0; word = 0; ClockOwner::clear();
    }
    // The word that entered k edges ago (0 = the latest).
    uint8_t ago(int k) const { return ring[(head - k) & (STAGES - 1)]; }
    static int delayOf(const Parameters& p) { return std::max(1, std::min(MAX_DELAY, p.delay)); }
    static int spreadOf(const Parameters& p) { return std::max(0, std::min(MAX_SPREAD, p.spread)); }
    // Bit k comes from the word that entered delay + k * spread edges ago.
    uint8_t read(int delay, int spread) const {
        uint8_t out = 0;
        for (int k = 0; k < 8; ++k) out |= uint8_t(ago(delay + k * spread) & (1u << k));
        return out;
    }
    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        auto step = ClockOwner::tick(p.mode, p.rateHz, in, sampleRate);
        int delay = delayOf(p), spread = spreadOf(p);
        Output out;
        if (step.seed) { ring.fill(0); head = 0; out.cleared = true; }
        if (step.acquire) {
            // LOOP re-enters the word now leaving the delay, which is also the word published.
            uint8_t entering = p.loop ? ago(delay - 1) : in.data.connected ? in.data.rawWord : 0;
            head = (head + 1) & (STAGES - 1); ring[head] = entering;
        }
        // DELAY and SPREAD are read when a word is published; between edges the word holds.
        if (step.acquire || step.loaded) word = read(delay, spread);
        out.word = word; out.clocked = step.clocked; out.clk = step.clk;
        out.advanced = step.acquire; out.loaded = step.loaded; out.source = step.source;
        return out;
    }
};
} // namespace dspemu::pipeline
