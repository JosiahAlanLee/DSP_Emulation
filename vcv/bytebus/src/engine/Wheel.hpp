// SPDX-License-Identifier: BSD-3-Clause
// Two 24-stage shift registers on one clock. FREE is a maximal-length LFSR with the all-zero
// state inserted, so its cycle is every 24-bit state once, 2^24 steps. LOOP recirculates its
// first LENGTH stages, or with LENGTH Off continues FREE as one 48-stage line; CAPTURE copies
// FREE into LOOP on the next edge. Both are read eight stages at a time through one window.
// After the Pseudo (Nonlinearcircuits). Clock ownership, RESET and LOAD are ClockOwner's.
// Contract: docs/WHEEL.md.
#pragma once
#include "ClockOwner.hpp"
#include <cmath>

namespace dspemu::wheel {
using clock_owner::ClockMode;

constexpr int STAGES = 24;
constexpr uint32_t ALL = 0xffffff;

struct Parameters {
    ClockMode mode = ClockMode::Auto;   // Auto, Jack, Internal or Engine; there is no bus input
    double rateHz = 8;
    int length = 16;                    // 0 is Off; 1-24 recirculate that many stages
    int offset = 0;                     // 0-23
    int stride = 1;                     // 1-3
    uint8_t seed = 0;
};
struct Inputs {
    clock_owner::Inputs clock;          // CLOCK, RESET, LOAD; its bus sample stays unconnected
    float capture = 0;
    bool captureButton = false;
};
struct Output {
    uint32_t free = 0, loop = 0;
    uint8_t freeWord = 0, loopWord = 0;
    bool clocked = true, clk = false, loaded = false, advanced = false;
    bool armed = false, captured = false;
    float u = 0, g = 0;
    bool loopGate = false, noiseGate = false;
};

inline int bit(uint32_t r, int stage) { return int(r >> stage & 1); }
// FREE's step: Fibonacci feedback from stages 23, 22, 21 and 16 (x^24 + x^23 + x^22 + x^17 + 1),
// inverted while stages 0-22 are all zero, which inserts the all-zero state into the cycle.
inline uint32_t stepFree(uint32_t r) {
    int feedback = bit(r, 23) ^ bit(r, 22) ^ bit(r, 21) ^ bit(r, 16);
    if ((r & 0x7fffff) == 0) feedback ^= 1;
    return (r << 1 | uint32_t(feedback)) & ALL;
}
// LOOP's step: its stage LENGTH - 1 recirculates; Off takes the bit leaving FREE.
inline uint32_t stepLoop(uint32_t loop, uint32_t freeBefore, int length) {
    int in = length <= 0 ? bit(freeBefore, 23) : bit(loop, std::min(length, STAGES) - 1);
    return (loop << 1 | uint32_t(in)) & ALL;
}
// Eight stages through the window: bit k is stage (offset + stride * k) mod 24.
inline uint8_t window(uint32_t r, int offset, int stride) {
    uint8_t w = 0;
    for (int k = 0; k < 8; ++k) w |= uint8_t(bit(r, ((offset + stride * k) % STAGES + STAGES) % STAGES) << k);
    return w;
}
inline uint32_t seeded(uint8_t seed) { return uint32_t(seed) * 0x010101u; }
inline float dac(uint8_t w) { return (int(w) - 128) * 5.f / 128.f; }
inline float gauss(uint8_t w) {
    int ones = 0; for (int k = 0; k < 8; ++k) ones += w >> k & 1;
    return (ones - 4) * 1.25f;
}

struct Wheel {
    clock_owner::ClockOwner owner;
    bus::Gate captureGate;
    bool captureButtonHigh = false, armed = false;
    uint32_t free = 0, loop = 0;

    void clear(uint32_t freeState = 0, uint32_t loopState = 0) {
        owner.clear(); captureGate.reset(); captureButtonHigh = false; armed = false;
        free = freeState & ALL; loop = loopState & ALL;
    }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        bool captureEdge = captureGate.process(in.capture);
        captureEdge |= in.captureButton && !captureButtonHigh;
        captureButtonHigh = in.captureButton;
        if (captureEdge) armed = true;
        clock_owner::Inputs clock = in.clock;
        clock.data = bus::Sample{};
        auto s = owner.tick(p.mode, p.rateHz, clock, sampleRate);
        if (s.seed) { free = loop = seeded(p.seed); }
        if (s.acquire) {
            uint32_t before = free;
            free = stepFree(free);
            if (armed) { loop = free; armed = false; out.captured = true; }
            else loop = stepLoop(loop, before, std::max(0, std::min(STAGES, p.length)));
            out.advanced = true;
        }
        int offset = ((p.offset % STAGES) + STAGES) % STAGES, stride = std::max(1, std::min(3, p.stride));
        out.free = free; out.loop = loop;
        out.freeWord = window(free, offset, stride); out.loopWord = window(loop, offset, stride);
        out.u = dac(out.loopWord); out.g = gauss(out.loopWord);
        out.loopGate = loop & 1; out.noiseGate = free & 1;
        out.clocked = s.clocked; out.clk = s.clk; out.loaded = s.loaded; out.armed = armed;
        return out;
    }
};
} // namespace dspemu::wheel
