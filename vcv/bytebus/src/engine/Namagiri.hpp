// SPDX-License-Identifier: BSD-3-Clause
// The Rogers-Ramanujan continued fraction R(q) = q^(1/5) / (1 + q / (1 + q^2 / (1 + ...))),
// played as a sequencer: the module's clock steps n 1..DEPTH and the convergents
// R_1(q)..R_depth(q) come out as held CV, gates and a phase word, so each cycle is a phrase
// built into the math: big moves, then convergence. The convergents follow HydraPy's
// namagiri.convergents (finite definition, principal branch of q^(1/5), denominators guarded
// below 1e-10 by substitution as in ops_namagiri, never a NaN); |q| is clamped to .97 at the
// parameter layer as ops_namagiri does, where the guard cannot fire (the smallest reachable
// denominator magnitude is about 6.5e-3). Each step also maps to a note event as HydraPy's
// namagiri_pattern (phase 3, PR #94): velocity = change/(1+change) with HydraPy's per-cycle
// delta (0 on step 1), note = root + floor((phase+pi)/2pi * span), a rest below the velocity
// threshold. Clock ownership, RESET and strobes are the Clocked Register's (ClockOwner.hpp);
// a BUS IN word sets n directly, modulo DEPTH. The word on the bus is the phase, arg(R_n)/2pi
// as a signed fraction of pi in offset binary (spec section 6). Contract: docs/NAMAGIRI.md.
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

// Python namagiri.convergents parity: r[n-1] = R_n(q), n = 1..depth. The
// nested evaluation, with the DENOM_MIN guard, is the reference: used outside
// the parameter layer's |q| <= MAX_RADIUS disk, where the guard can matter.
inline void buildNested(const C& q, int depth, std::array<C, MAX_DEPTH>& r) {
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

// The same finite continued fractions through the standard convergent
// recurrence: with tail_n = 1 + q/(1 + q^2/(... + q^n/1)),
//     A_k = A_(k-1) + q^k A_(k-2),  B_k = B_(k-1) + q^k B_(k-2),
//     A_(-1) = 1, B_(-1) = 0, A_0 = B_0 = 1,
// tail_k = A_k / B_k and R_k = root / tail_k = root B_k / A_k. One pass gives
// every truncated fraction, replacing the O(depth^2) complex pow/division
// nested loop with O(depth) complex multiplies (a rotating ANGLE CV rebuilds
// the table every sample; see tests/namagiri_bench.cpp). Over the committed
// HydraPy grid the two agree to 5e-15; the recurrence keeps a relative error
// around 3e-11 on the worst-conditioned |q| < .97 tables. If a coefficient
// loses the denominator, or q is outside the guarded disk, fall back to the
// reference nested evaluation so the guard behaves exactly as before.
inline void build(const C& q, int depth, std::array<C, MAX_DEPTH>& r) {
    double radius = std::abs(q);
    if (!(radius <= MAX_RADIUS) || !std::isfinite(q.real()) || !std::isfinite(q.imag())) {
        buildNested(q, depth, r);
        return;
    }
    C root = std::pow(q, 1.0 / 5);
    C power = 1;                       // q^k
    C a2 = 1, b2 = 0;                  // A_(k-2), B_(k-2): A_(-1), B_(-1)
    C a1 = 1, b1 = 1;                  // A_(k-1), B_(k-1): A_0, B_0
    for (int k = 1; k <= depth; ++k) {
        power *= q;
        C a = a1 + power * a2;
        C b = b1 + power * b2;
        bool guarded = std::abs(a) < DENOM_MIN * std::abs(b);
        if (!std::isfinite(a.real()) || !std::isfinite(a.imag()) ||
            !std::isfinite(b.real()) || !std::isfinite(b.imag()) || guarded) {
            buildNested(q, depth, r);
            return;
        }
        r[k - 1] = root * b / a;
        a2 = a1; b2 = b1; a1 = a; b1 = b;
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

// HydraPy namagiri_pattern.pattern parity (phase 3, PR #94): the velocity is the convergent
// change clipped to 0..1, and the note maps the phase into root..root+span semitones, clipped
// to MIDI. A step is an event when its velocity reaches the threshold; unlike the DELTA jack,
// this change is HydraPy's delta (0 on step 1 of every cycle, |R_n - R_(n-1)| after).
inline double patternVelocity(double change) { return change / (1.0 + change); }
inline int patternNote(double phase, int root, int span) {
    return std::max(0, std::min(127, int(std::floor((phase + PI) / TAU * span)) + root));
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
    double settle = .02;                    // the CONVERGED latch, units of R (the module: volts / 5)
    int root = 48;                          // MIDI root note, 0..127
    int span = 24;                          // semitones above ROOT, 1..96
    double velocityThreshold = 0;           // a step's velocity below this is a rest, 0..1
};
struct Inputs {
    clock_owner::Inputs clock;              // CLOCK and RESET
    bus::Sample address;                    // BUS IN: a word whose low bits set n
};
struct Output {
    int step = 0;                           // 1..depth
    Controls c;                             // the held values of this step
    uint8_t word = 0;                       // the phase word of the held step
    int note = 0;                           // the held MIDI note (V/OCT), or the last event's on a rest
    double velocity = 0;                    // the step's velocity, 0..1 (HydraPy's delta)
    bool gate = false;                      // high for the step when it is an event above velocity 0
    bool rest = false;                      // this step's velocity is below the threshold
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
    bool pending = false;               // a bus change awaits its own strobe
    bool deferred = false;              // that strobe is held past a high CLK sample
    bool lastClk = false;               // BUS OUT CLK on the previous sample
    Controls held{};
    int note = 0;                           // the note in force (held across rests)
    double velocity = 0;
    bool gate = false, rest = false;

    void clear(int savedStep = 1, C savedPrevious = 0, bool savedConverged = false,
               int savedNote = 0) {
        owner.clear();
        previous = std::isfinite(savedPrevious.real()) && std::isfinite(savedPrevious.imag())
            ? savedPrevious : C(0);
        depth = 0; step = std::max(1, std::min(MAX_DEPTH, savedStep));
        lastQ = std::nan(""); built = false; firstPublish = true;
        converged = savedConverged; pending = deferred = lastClk = false; held = {};
        // The held note survives rests, so a restore during a rest has to bring
        // it back: the first load publication re-holds the saved step, and on a
        // rest that keeps this note instead of falling back to MIDI 0.
        note = std::max(0, std::min(127, savedNote));
        velocity = 0; gate = rest = false;
    }
    // Re-hold the current step: the convergents, this step's change and the latch, plus the
    // note pattern. The pattern's velocity uses HydraPy's delta (0 on step 1), separately from
    // the DELTA jack's |R_n - previous|; the note holds across rests, the velocity does not.
    void hold(double settle, int root, int span, double velocityThreshold) {
        held.real = r[step - 1].real();
        held.imag = r[step - 1].imag();
        held.mag = std::abs(r[step - 1]);
        held.phase = std::arg(r[step - 1]);
        held.delta = std::abs(r[step - 1] - previous);
        previous = r[step - 1];
        converged = converged || held.delta < settle;
        double change = step >= 2 ? std::abs(r[step - 1] - r[step - 2]) : 0;
        velocity = patternVelocity(change);
        rest = velocity < velocityThreshold;
        if (!rest) note = patternNote(held.phase, root, span);
        gate = !rest && velocity > 0;       // a velocity-0 event keeps its note but does not gate
    }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        double radius = std::isfinite(p.radius) ? std::max(0., std::min(MAX_RADIUS, p.radius)) : .8;
        double turns = std::isfinite(p.turns) ? p.turns - std::floor(p.turns) : 0;
        int depthNew = std::max(1, std::min(MAX_DEPTH, p.depth));
        double settle = std::isfinite(p.settle) ? std::max(0., p.settle) : .02;
        int root = std::max(0, std::min(127, p.root));
        int span = std::max(1, std::min(96, p.span));
        double velocityThreshold = std::isfinite(p.velocityThreshold)
            ? std::max(0., std::min(1., p.velocityThreshold)) : 0;
        C q = std::polar(radius, TAU * turns);

        // The table rebuilds only when q or DEPTH changes, past a tolerance so a slow CV
        // does not rebuild every sample; a knob turn re-holds the current step at once.
        if (depthNew != depth || std::abs(q - lastQ) > 1e-9 * std::max(1., std::abs(q))) {
            build(q, depthNew, r);
            depth = depthNew; lastQ = q;
            if (step > depth) step = depth;
            if (built) hold(settle, root, span, velocityThreshold);
            built = true;
        }

        auto s = owner.tick(p.mode, p.rateHz, in.clock, sampleRate);
        bool publish = false;
        // RESET is a fresh start: previous = 0 before the hold, so the first
        // publication moves out of silence, |R_1|, on every reset, including a
        // repeated reset already on step 1 and a reset from any other step.
        if (s.seed) { step = 1; previous = 0; converged = false; publish = true; }
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
        if (publish) { hold(settle, root, span, velocityThreshold); firstPublish = false; }

        out.clocked = s.clocked;
        if (!out.clocked) {
            pending = deferred = lastClk = false;   // Engine mode: no CLK channel
            out.clk = false;
        } else {
            // One CLK high per publication, never two in a row: a strobe that
            // would follow a high CLK sample is held back one sample, the
            // Clocked Register's mandatory low. A bus update right after a
            // clock edge (or a load) is therefore a separate edge downstream.
            bool want = s.clk || pending;
            pending = false;
            if (deferred) { out.clk = true; deferred = false; }
            else if (want && lastClk) { out.clk = false; deferred = true; }
            else out.clk = want;
            lastClk = out.clk;
        }
        out.loaded = s.loaded; out.advanced = s.acquire;
        out.step = step; out.c = held; out.word = phaseWord(held.phase);
        out.note = note; out.velocity = velocity; out.gate = gate; out.rest = rest;
        out.converged = converged;
        return out;
    }
};
} // namespace dspemu::namagiri
