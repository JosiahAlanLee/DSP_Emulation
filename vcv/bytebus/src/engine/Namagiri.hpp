// SPDX-License-Identifier: BSD-3-Clause
// The Rogers-Ramanujan continued fraction R(q) = q^(1/5) / (1 + q / (1 + q^2 / (1 + ...))),
// played as a sequencer: the module's clock steps n 1..DEPTH and the convergents
// R_1(q)..R_depth(q) come out as held CV, gates and a phase word, so each cycle is a phrase
// built into the math: big moves, then convergence. The convergents follow HydraPy's
// namagiri.convergents (finite definition, principal branch of q^(1/5), denominators guarded
// below 1e-10 by substitution as in ops_namagiri, never a NaN); |q| is clamped to .97 at the
// parameter layer as ops_namagiri does, where the guard cannot fire (the smallest reachable
// denominator magnitude is about 6.5e-3). Clock ownership, RESET and strobes are the Clocked
// Register's (ClockOwner.hpp); a BUS IN word sets n directly, modulo DEPTH. The word on the
// bus is the phase, arg(R_n)/2pi quantized to n/256 (spec section 6, "phase"). Contract:
// docs/NAMAGIRI.md.
#pragma once
#include "ClockOwner.hpp"
#include <array>
#include <cmath>
#include <complex>

namespace dspemu::namagiri {
using clock_owner::ClockMode;
using C = std::complex<double>;

constexpr int MAX_DEPTH = 32;
constexpr double MAX_RADIUS = .97;          // ops_namagiri's open-disk bound
constexpr double DENOM_MIN = 1e-10;         // ops_namagiri's denominator floor
constexpr double PI = 3.1415926535897932384626433832795;
constexpr double TAU = 6.283185307179586476925286766559;

// Python namagiri.convergents parity: r[n-1] = R_n(q), n = 1..depth.
inline void build(const C& q, int depth, std::array<C, MAX_DEPTH>& r) {
    C root = std::pow(q, 1.0 / 5);
    for (int n = 1; n <= depth; ++n) {
        C tail = 1.0;
        for (int k = n; k >= 1; --k) {
            if (std::abs(tail) < DENOM_MIN) tail = C(DENOM_MIN, 0);
            tail = 1.0 + std::pow(q, double(k)) / tail;
        }
        r[n - 1] = root / tail;
    }
}

struct Controls {
    double real = 0, imag = 0, mag = 0, phase = 0, delta = 0;
};

// Python convergent_controls parity: delta[0] is 0, delta[n] = |R_n - R_(n-1)|.
inline std::array<Controls, MAX_DEPTH> controls(const std::array<C, MAX_DEPTH>& r, int depth) {
    std::array<Controls, MAX_DEPTH> c{};
    for (int n = 1; n <= depth; ++n) {
        c[n - 1].real = r[n - 1].real();
        c[n - 1].imag = r[n - 1].imag();
        c[n - 1].mag = std::abs(r[n - 1]);
        c[n - 1].phase = std::arg(r[n - 1]);
        c[n - 1].delta = n >= 2 ? std::abs(r[n - 1] - r[n - 2]) : 0;
    }
    return c;
}

// The phase as a byte word: the angle as a signed fraction of pi in offset binary, exactly
// spec section 6's "bipolar" reading, so 0x80 is angle 0, 0x00 is -pi and 0xFF just below +pi;
// a DAC stage reads (n - 128) / 128 * pi back out.
inline uint8_t phaseWord(double phase) {
    double s = phase / PI;                  // (-1, 1]
    if (!(s > -1)) return s == s ? 0 : 128; // below -pi clamps to 0; NaN reads as angle 0
    if (s >= 127. / 128) return 255;
    return uint8_t(std::floor(128. + 128. * s + .5));
}

struct Parameters {
    ClockMode mode = ClockMode::Auto;       // Auto, Jack, Internal or Engine
    double rateHz = 8;
    double radius = .8;                     // |q|, 0..MAX_RADIUS
    double turns = 0;                       // arg q in turns, [0, 1)
    int depth = 12;                         // 1..MAX_DEPTH, the cycle length
    double threshold = .02;                 // the CONVERGED latch, units of R (the module: volts / 5)
};
struct Inputs {
    clock_owner::Inputs clock;              // CLOCK and RESET
    bus::Sample address;                    // BUS IN: a word whose low bits set n
};
struct Output {
    int step = 0;                           // 1..depth
    Controls c;                             // the held values of this step
    uint8_t word = 0;                       // the phase word of the held step
    bool clocked = true, clk = false;
    bool loaded = false, advanced = false, eoc = false;
    bool converged = false;
};

struct Namagiri {
    clock_owner::ClockOwner owner;
    std::array<C, MAX_DEPTH> r{};
    C previous = 0;                         // the last published value; delta = |R_n - previous|
    C lastQ = std::nan("");                 // table rebuild detector
    int depth = 0, step = 1;
    bool converged = false, built = false, firstPublish = true;
    bool pending = false, strobe = false;
    Controls held{};

    void clear(int savedStep = 1, C savedPrevious = 0, bool savedConverged = false) {
        owner.clear();
        previous = std::isfinite(savedPrevious.real()) && std::isfinite(savedPrevious.imag())
            ? savedPrevious : C(0);
        depth = 0; step = std::max(1, std::min(MAX_DEPTH, savedStep));
        lastQ = std::nan(""); built = false; firstPublish = true;
        converged = savedConverged; pending = strobe = false; held = {};
    }
    // Re-hold the current step: the convergents, this step's change and the latch.
    void hold(double threshold) {
        held.real = r[step - 1].real();
        held.imag = r[step - 1].imag();
        held.mag = std::abs(r[step - 1]);
        held.phase = std::arg(r[step - 1]);
        held.delta = std::abs(r[step - 1] - previous);
        previous = r[step - 1];
        converged = converged || held.delta < threshold;
    }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        double radius = std::isfinite(p.radius) ? std::max(0., std::min(MAX_RADIUS, p.radius)) : .8;
        double turns = std::isfinite(p.turns) ? p.turns - std::floor(p.turns) : 0;        int depthNew = std::max(1, std::min(MAX_DEPTH, p.depth));
        double threshold = std::isfinite(p.threshold) ? std::max(0., p.threshold) : .02;
        C q = std::polar(radius, TAU * turns);

        // The table rebuilds only when q or DEPTH changes, past a tolerance so a slow CV
        // does not rebuild every sample; a knob turn re-holds the current step at once.
        if (depthNew != depth || std::abs(q - lastQ) > 1e-9 * std::max(1., std::abs(q))) {
            build(q, depthNew, r);
            depth = depthNew; lastQ = q;
            if (step > depth) step = depth;
            if (built) hold(threshold);
            built = true;
        }

        auto s = owner.tick(p.mode, p.rateHz, in.clock, sampleRate);
        bool publish = false;
        if (s.seed) { step = 1; converged = false; publish = true; }
        if (s.acquire && !s.seed) {
            if (step + 1 > depth) { step = 1; out.eoc = true; converged = false; }
            else ++step;
            publish = true;
        }
        // A bus word sets n directly (after the edge, so it overrides an advance on the same
        // sample); it publishes with the module's own strobe rules. The low bits index the step
        // modulo DEPTH, so a 5-bit address drives 1..32 at DEPTH 32 as in Fan.
        if (!s.seed && in.address.connected && in.address.updated) {
            int set = int(in.address.word % depth) + 1;
            if (set != step) { step = set; publish = true; pending = true; }
        }
        if (s.loaded && firstPublish) publish = true;
        if (publish) { hold(threshold); firstPublish = false; }

        out.clocked = s.clocked;
        if (!out.clocked) { strobe = false; pending = false; }
        else if (strobe) { strobe = false; }
        else if (pending) { strobe = true; pending = false; }
        out.clk = s.clk || strobe;
        out.loaded = s.loaded; out.advanced = s.acquire;
        out.step = step; out.c = held; out.word = phaseWord(held.phase);
        out.converged = converged;
        return out;
    }
};
} // namespace dspemu::namagiri
