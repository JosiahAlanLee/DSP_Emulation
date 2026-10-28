// SPDX-License-Identifier: BSD-3-Clause
// Two 4-bit up/down counters, H and V, on separate clocks; the step is V's low two bits (row)
// and H's (column) on a 4 x 4 grid. A jump presets one counter as the Bindubba's cross-coupled
// jam wiring does: Follow is the settled result (H's position and upper bits become V's
// position; V's become H's position, swapped for V's position), Load takes the jam values once.
// H's clock source, RESET and LOAD are ClockOwner's, but no H edge is lost to its strobe gap;
// the state word V * 16 + H is published on every change. Contract: docs/LATTICE.md.
#pragma once
#include "ClockOwner.hpp"
#include <cmath>

namespace dspemu::lattice {
using clock_owner::ClockMode;

enum class JumpMode { Follow, Load };

struct Parameters {
    ClockMode mode = ClockMode::Auto;   // H's clock: Auto, Jack, Internal or Engine
    double rateHz = 8;
    JumpMode jump = JumpMode::Follow;
    uint8_t seed = 0;                   // V in bits 4-7, H in bits 0-3
};
struct Inputs {
    clock_owner::Inputs clock;          // H CLOCK, RESET, LOAD; its bus sample stays unconnected
    bool vClockConnected = false;
    float vClock = 0, dirH = 0, dirV = 0, jumpH = 0, jumpV = 0;
    bus::Sample bus;                    // BUS IN: jump targets
};
struct Output {
    uint8_t state = 0;
    int step = 0;                       // 0-15: 4 * (V & 3) + (H & 3)
    bool clocked = true, clk = false;
    bool loaded = false, hAdvanced = false, vAdvanced = false, jumped = false;
};

inline int position(uint8_t counter) { return counter & 3; }
inline int swapped(int pos) { return ((pos & 1) << 1) | (pos >> 1 & 1); }
inline int stepOf(uint8_t h, uint8_t v) { return 4 * position(v) + position(h); }
// The settled jam results: H takes V's position for both halves, V takes H's position for its
// upper bits and the same, bits swapped, for its position.
inline uint8_t followH(uint8_t v) { return uint8_t(5 * position(v)); }
inline uint8_t followV(uint8_t h) { return uint8_t(position(h) << 2 | swapped(position(h))); }
// The jam values taken once: a position from the counter's own upper bits (swapped for V),
// upper bits from the other counter's position.
inline uint8_t loadH(uint8_t h, uint8_t v) { return uint8_t(position(v) << 2 | (h >> 2 & 3)); }
inline uint8_t loadV(uint8_t h, uint8_t v) { return uint8_t(position(h) << 2 | swapped(v >> 2 & 3)); }
// One count, modulo 16. wrap reports the position crossing 3 -> 0 up or 0 -> 3 down.
inline uint8_t count(uint8_t c, bool down, bool& wrap) {
    uint8_t next = uint8_t((c + (down ? 15 : 1)) & 15);
    wrap = down ? position(c) == 0 : position(c) == 3;
    return next;
}

struct Lattice {
    clock_owner::ClockOwner owner;
    bus::Gate vGate, dirHGate, dirVGate, jumpHGate, jumpVGate;
    uint8_t h = 0, v = 0;
    bool strobe = false, pending = true;  // a new instance publishes its state
    bool seedBefore = false;              // RESET or LOAD took the previous sample

    void clear(uint8_t state = 0) {
        owner.clear(); vGate.reset(); dirHGate.reset(); dirVGate.reset(); jumpHGate.reset(); jumpVGate.reset();
        h = state & 15; v = state >> 4 & 15; strobe = false; pending = true; seedBefore = false;
    }
    uint8_t state() const { return uint8_t(v << 4 | h); }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        dirHGate.process(in.dirH); dirVGate.process(in.dirV);
        bool vEdge = vGate.process(in.vClockConnected ? in.vClock : 0.f);
        bool jumpHRise = jumpHGate.process(in.jumpH), jumpVRise = jumpVGate.process(in.jumpV);
        bool holdH = jumpHGate.high, holdV = jumpVGate.high;
        bool follow = p.jump == JumpMode::Follow && !in.bus.connected;
        uint8_t before = state();

        clock_owner::Inputs clock = in.clock;
        clock.data = bus::Sample{};
        bool jackBefore = owner.clockGate.high;
        clock_owner::ClockSource sourceBefore = owner.source;
        double phaseBefore = owner.phase;
        auto s = owner.tick(p.mode, p.rateHz, clock, sampleRate);
        out.clocked = s.clocked;
        // H's edge from the clock ClockOwner selected, read from its state rather than from
        // s.acquire: ClockOwner drops an edge on its mandatory low sample and on a load's
        // publication, but NLC §3 lets no Lattice change be lost (only its strobe waits).
        bool hEdge = false;
        switch (s.source) {
            case clock_owner::ClockSource::Jack: hEdge = !jackBefore && owner.clockGate.high; break;
            // Unclocked, with no strobe gap; its first sample shows the seed, as for every module.
            case clock_owner::ClockSource::Engine: hEdge = s.acquire; break;
            // The phase wraps backwards on an edge; a source change restarts it below one step.
            case clock_owner::ClockSource::Internal: hEdge = s.source == sourceBefore && owner.phase < phaseBefore; break;
            case clock_owner::ClockSource::Bus: break;  // BUS IN never clocks H
        }
        // A load published on the sample after RESET or LOAD (a deferred seed) counts this
        // sample's edges, but the held state's own publication after clear() (a new instance,
        // a restore, Initialize) shows the held state: an H or V CLOCK already high there does
        // not count, as on every ClockOwner module.
        if (s.loaded && !s.seed && !seedBefore) hEdge = vEdge = false;
        seedBefore = s.seed;
        if (s.seed) { h = p.seed & 15; v = p.seed >> 4 & 15; }
        if (s.loaded) { out.loaded = true; pending = true; }

        // Clocks. A held Follow jump preset ignores its counter's edges. RESET or LOAD takes
        // priority over H's and V's edges and the jumps on its sample.
        bool wrapH = false, wrapV = false;
        if (hEdge && !s.seed && !(follow && holdH)) { h = count(h, dirHGate.high, wrapH); out.hAdvanced = true; }
        if (!in.vClockConnected) vEdge = out.hAdvanced && wrapH;
        if (vEdge && !s.seed && !(follow && holdV)) { v = count(v, dirVGate.high, wrapV); out.vAdvanced = true; }

        // Jumps, both evaluated from the state the clocks left.
        uint8_t h0 = h, v0 = v;
        if (s.seed) {
        } else if (in.bus.connected) {
            if (jumpHRise) h = in.bus.rawWord & 15;
            if (jumpVRise) v = in.bus.rawWord >> 4 & 15;
        } else if (follow) {
            // While both are held, each keeps what it took when the second rose.
            bool both = holdH && holdV;
            if (holdH && (!both || jumpHRise || jumpVRise)) h = followH(v0);
            if (holdV && (!both || jumpHRise || jumpVRise)) v = followV(h0);
        } else {
            if (jumpHRise) h = loadH(h0, v0);
            if (jumpVRise) v = loadV(h0, v0);
        }
        out.jumped = h != h0 || v != v0;

        // Every change, edge or load publishes the state; a strobe never follows a strobe.
        if (state() != before || out.hAdvanced || out.vAdvanced) pending = true;
        if (!out.clocked) { strobe = false; pending = false; }
        else if (strobe) strobe = false;
        else if (pending) { strobe = true; pending = false; }
        out.clk = strobe;
        out.state = state(); out.step = stepOf(h, v);
        return out;
    }
};

// A one-pole lag whose time constant a control voltage shortens, one octave per volt, within the
// knob's range (the vactrol's lit and dark limits); the control itself passes a vactrol-like lag:
// quick as the light rises, slow as it falls.
struct Slew {
    double y = 0, control = 0;
    bool started = false;
    static constexpr double MIN_TAU = 5e-4, MAX_TAU = .5, CONTROL_RISE = .005, CONTROL_FALL = .05;
    static double tau(double knob) { return MIN_TAU * std::pow(MAX_TAU / MIN_TAU, std::max(0., std::min(1., knob))); }
    static double effective(double knob, double control) {
        return std::max(MIN_TAU, std::min(MAX_TAU, tau(std::isfinite(knob) ? knob : 0) * std::exp2(-control)));
    }
    float process(float x, float knob, float cv, double sampleTime) {
        if (!std::isfinite(x)) x = 0;
        if (!std::isfinite(cv)) cv = 0;
        double dt = std::isfinite(sampleTime) && sampleTime > 0 ? sampleTime : 1. / 48000;
        double target = std::max(-10., std::min(10., double(cv)));
        double lag = target > control ? CONTROL_RISE : CONTROL_FALL;
        control += (target - control) * (1 - std::exp(-dt / lag));
        if (!started) { y = x; started = true; return x; }
        y += (x - y) * (1 - std::exp(-dt / effective(knob, control)));
        return float(y);
    }
    void reset() { y = 0; control = 0; started = false; }
};
} // namespace dspemu::lattice
