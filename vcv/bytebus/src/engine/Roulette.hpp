// SPDX-License-Identifier: BSD-3-Clause
// A phase-locked loop and a 12-bit counter, after the Wangernumb (Nonlinearcircuits): a CD4046's
// XOR comparator (or its edge comparator), a lead-lag loop filter integrated exactly per sample,
// and a VCO whose maximum a vactrol-like control sets, linear from zero and stopping below 1 % of
// it. ROULETTE replaces the comparator, so a gate spins the VCO up and the filter lets it coast
// to rest; the count at rest is published once on RESULT. The VCO, or DIVIDE, clocks the counter
// through ClockOwner as a jack clock, so every count gets a strobe. Contract: docs/ROULETTE.md.
#pragma once
#include "ClockOwner.hpp"
#include <cmath>

namespace dspemu::roulette {
using clock_owner::ClockMode;

constexpr double R2 = 47e3, R_COMPARATOR = 1e3, C_FARADS = 2e-6;
constexpr double STOP = .01;                         // VCOIN below which the VCO stops
constexpr double SUPPLY = 12;                        // ROULETTE volts per unit of VCOIN
constexpr float TRACK_THRESHOLD = 1.2f, TRACK_HYSTERESIS = .01f;
constexpr double CONTROL_RISE = .005, CONTROL_FALL = .05;
constexpr int BITS = 12;
constexpr unsigned COUNT_MASK = (1u << BITS) - 1;

enum class Source { Auto, Divide, Vco };
enum class Comparator { Xor, Edge };

struct Parameters {
    Source source = Source::Auto;
    double rangeHz = 2000;                           // F_MAX before RANGE CV, 1 Hz - 20 kHz
    double filterOhms = 50e3;                        // the FILTER pot, 0 - 100 kOhm
    double scale = 1;                                // on the capacitor: 0.1, 1 or 10
    Comparator comparator = Comparator::Xor;
};
struct Inputs {
    bool trackConnected = false; float track = 0;
    bool rouletteConnected = false; float roulette = 0;
    bool divideConnected = false; float divide = 0;
    float rangeCv = 0;
    float reset = 0;
};
struct Output {
    unsigned count = 0;
    uint8_t lo = 0, hi = 0, result = 0;
    bool clk = false, loaded = false, advanced = false;
    bool resultClk = false, stopped = false;
    bool vcoBit = false, vcoRise = false;
    double vcoin = 0, frequency = 0, fMax = 0;
    float vcoOut = 0;
};

inline uint8_t lo(unsigned count) { return uint8_t(count & 255); }
inline uint8_t hi(unsigned count) { return uint8_t(count >> 4 & 255); }

// The 4046's edge comparator: +1 drives the filter high (the input leads), -1 low (the VCO
// leads), 0 leaves it floating.
struct EdgeComparator {
    int state = 0;
    int process(bool inputRise, bool vcoRise) {
        if (inputRise && !vcoRise) state = std::min(1, state + 1);
        else if (vcoRise && !inputRise) state = std::max(-1, state - 1);
        return state;
    }
};

inline double polyBlep(double t, double dt) {
    if (dt <= 0) return 0;
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

struct Roulette {
    clock_owner::ClockOwner owner;
    bus::Gate trackGate;
    EdgeComparator edge;
    double vC = 0;                                   // the capacitor, 0-1 of the supply
    double phase = .5;                               // the VCO: high in [0, .5), low in [.5, 1)
    double control = 0;                              // RANGE CV after the lag
    unsigned count = 0;
    uint8_t result = 0;
    bool trackHigh = false, running = false, resultPending = false, resultStrobe = false;

    void clear(unsigned savedCount = 0, uint8_t savedResult = 0, double savedVc = 0, double savedPhase = .5) {
        owner.clear(); edge = {}; trackHigh = false;
        vC = std::isfinite(savedVc) ? std::max(0., std::min(1., savedVc)) : 0;
        phase = std::isfinite(savedPhase) ? savedPhase - std::floor(savedPhase) : .5;
        control = 0; count = savedCount & COUNT_MASK; result = savedResult;
        running = false; resultPending = false; resultStrobe = false;
    }
    bool track(float v) {
        if (!std::isfinite(v)) v = 0;
        if (v >= TRACK_THRESHOLD + TRACK_HYSTERESIS) trackHigh = true;
        else if (v <= TRACK_THRESHOLD - TRACK_HYSTERESIS) trackHigh = false;
        return trackHigh;
    }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        double sr = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000.;
        double dt = 1 / sr;
        // RANGE CV through the vactrol-like lag.
        double target = std::isfinite(in.rangeCv) ? std::max(-10., std::min(10., double(in.rangeCv))) : 0;
        control += (target - control) * (1 - std::exp(-dt / (target > control ? CONTROL_RISE : CONTROL_FALL)));
        double range = std::isfinite(p.rangeHz) ? std::max(1., std::min(20000., p.rangeHz)) : 2000;
        out.fMax = range * std::exp2(control);

        // The comparator, or ROULETTE in its place.
        bool trackBefore = trackHigh;
        bool trackBit = in.trackConnected ? track(in.track) : (trackHigh = false);
        bool trackRise = trackBit && !trackBefore;
        bool vcoBit = phase < .5;
        double rs = std::max(0., std::min(100e3, std::isfinite(p.filterOhms) ? p.filterOhms : 50e3));
        double u = 0; bool floating = false;
        if (in.rouletteConnected) {
            double v = std::isfinite(in.roulette) ? in.roulette : 0;
            u = std::max(0., std::min(1., v / SUPPLY));
        } else {
            rs += R_COMPARATOR;
            if (p.comparator == Comparator::Xor) u = trackBit != vcoBit ? 1 : 0;
            else {
                int s = edge.state;
                floating = s == 0; u = s > 0 ? 1 : 0;
            }
        }
        double c = C_FARADS * (std::isfinite(p.scale) && p.scale > 0 ? p.scale : 1);
        double vcoin = floating ? vC : vC + (u - vC) * R2 / (rs + R2);
        out.vcoin = vcoin;

        // The VCO: linear from zero, stopped below STOP, at most half the engine rate.
        double f = vcoin >= STOP ? std::min(out.fMax * vcoin, sr / 2) : 0;
        out.frequency = f;
        double inc = f * dt;
        phase += inc;
        bool rise = phase >= 1;
        if (rise) phase -= std::floor(phase);
        bool newBit = phase < .5;

        // The counter: DIVIDE or the VCO as a jack clock. RESET restarts the VCO low on its
        // own sample, so the outputs, the edge comparator and the count all see the restart.
        bool divide = p.source == Source::Divide || (p.source == Source::Auto && in.divideConnected);
        clock_owner::Inputs clock;
        clock.clockConnected = true;
        clock.clock = divide ? (in.divideConnected && std::isfinite(in.divide) ? in.divide : 0.f) : (newBit ? bus::HIGH : 0.f);
        clock.reset = in.reset;
        auto s = owner.tick(ClockMode::Jack, 0, clock, sr);
        if (s.seed) { count = 0; phase = .5; newBit = false; rise = false; }
        if (s.acquire) { count = (count + 1) & COUNT_MASK; out.advanced = true; }

        // The edge comparator sees this sample's edges; it drives the filter from the next.
        if (!in.rouletteConnected && p.comparator == Comparator::Edge) edge.process(trackRise, rise);
        else edge = {};
        out.vcoBit = newBit; out.vcoRise = rise;
        // VCO OUT: a band-limited square, +5 V in the high half. A restart is not an edge at a
        // known point within the sample, so the RESET sample is the plain low level.
        double naive = newBit ? 5 : -5;
        double blep = 5 * (polyBlep(phase, inc) - polyBlep(std::fmod(phase + .5, 1.), inc));
        out.vcoOut = float(inc > 0 && !s.seed ? naive + blep : naive);

        // The filter, an exact exponential step over the sample.
        if (!floating) vC = u + (vC - u) * std::exp(-dt / ((rs + R2) * c));

        // RESULT: the count as the VCO comes to rest, once per stop.
        bool nowRunning = f > 0;
        if (running && !nowRunning) { result = lo(count); resultPending = true; out.stopped = true; }
        running = nowRunning;
        if (resultStrobe) resultStrobe = false;
        else if (resultPending) { resultStrobe = true; resultPending = false; }

        out.count = count; out.lo = lo(count); out.hi = hi(count); out.result = result;
        out.clk = s.clk; out.loaded = s.loaded; out.resultClk = resultStrobe;
        return out;
    }
};
} // namespace dspemu::roulette
