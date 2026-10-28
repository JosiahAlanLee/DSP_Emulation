// SPDX-License-Identifier: BSD-3-Clause
// Two words in, one out, under a control word C: Select (bit BIT of C chooses A or B), Bits
// (each bit of C chooses that bit from A or B) or Mix (A + (B - A) * C / 255). Transparent on
// A's clock, or reclocked on Switch's own (ClockOwner.hpp). Contract: docs/SWITCH.md.
#pragma once
#include "ClockOwner.hpp"

namespace dspemu::byte_switch {
enum class Op { Select, Bits, Mix };
// Auto is the CLOCK jack when patched, else Follow A.
enum class Source { Auto, Jack, FollowA, Internal, Engine };

struct Parameters {
    Op op = Op::Select;
    Source source = Source::Auto;
    int bit = 0;             // Select's control bit, 0-7
    double rateHz = 8;
    uint8_t b = 0, c = 0;    // the words used while B or C is unpatched
};
struct Inputs {
    bus::Sample a, b, c;
    bool clockConnected = false;
    float clock = 0;
};
struct Output {
    uint8_t word = 0;
    bool clocked = false, clk = false, evaluated = false;
    bool owned = false;      // Switch's own clock (Jack, Internal or Engine) is in force
};

// Mix rounds to nearest; (B - A) * C / 255 never lands on a half, so there are no ties.
inline uint8_t apply(Op op, int bit, uint8_t a, uint8_t b, uint8_t c) {
    switch (op) {
        case Op::Bits: return uint8_t((a & ~c) | (b & c));
        case Op::Mix: return uint8_t((a * (255 - c) + b * c + 127) / 255);
        default: return (c >> (bit & 7) & 1) ? b : a;
    }
}

struct Switch {
    clock_owner::ClockOwner owner;
    uint8_t held = 0;
    bool owned = false;
    bool fresh = true;       // no result computed from a clocked A since the reset

    void clear() { owner.clear(); held = 0; owned = false; fresh = true; }
    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        // B and C are read as raw lines at the output's sample instants, whatever their own clocks.
        uint8_t b = in.b.connected ? in.b.rawWord : p.b, c = in.c.connected ? in.c.rawWord : p.c;
        Source source = p.source == Source::Auto ? (in.clockConnected ? Source::Jack : Source::FollowA) : p.source;
        Output out;
        if (source == Source::FollowA) {
            // Transparent: A's clocked/unclocked mode and CLK level pass through unchanged.
            owned = false;
            out.clocked = in.a.connected && in.a.clocked;
            out.clk = out.clocked && in.a.clk;
            // After a reset (a reload or Initialize) the first sample of a clocked A computes the result
            // of the present lines, without an edge, so a stopped A does not leave the output at 00.
            if (!out.clocked || in.a.rising || fresh) { held = apply(p.op, p.bit, in.a.rawWord, b, c); out.evaluated = true; }
            if (out.clocked) fresh = false;
            out.word = held;
            return out;
        }
        // Taking the clock over publishes the current result first, as a load does.
        if (!owned) { owner.clear(); owned = true; }
        fresh = false;
        clock_owner::Inputs clock;
        clock.clockConnected = in.clockConnected; clock.clock = in.clock;
        auto mode = source == Source::Jack ? clock_owner::ClockMode::Jack :
                    source == Source::Internal ? clock_owner::ClockMode::Internal : clock_owner::ClockMode::Engine;
        auto step = owner.tick(mode, p.rateHz, clock, sampleRate);
        if (step.loaded || step.acquire) { held = apply(p.op, p.bit, in.a.rawWord, b, c); out.evaluated = true; }
        out.word = held; out.clocked = step.clocked; out.clk = step.clk; out.owned = true;
        return out;
    }
};
} // namespace dspemu::byte_switch
