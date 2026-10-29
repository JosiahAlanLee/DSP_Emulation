// Portable Namagiri protocol, math and state-machine tests; no Rack SDK. The convergents and
// their controls match HydraPy's namagiri.py to 1e-9 over the committed reference grid.
#include "../src/engine/Namagiri.hpp"
#include "namagiri_reference.h"
#include "namagiri_pattern_reference.h"
#include <array>
#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
using namespace dspemu;
using namespace dspemu::namagiri;

static void referenceParity() {
    const double* data = namagiri_reference::DATA;
    for (size_t c = 0; c < namagiri_reference::CASES; ++c) {
        double radius = data[(c * namagiri_reference::DEPTH) * 7];
        double turns = data[(c * namagiri_reference::DEPTH) * 7 + 1];
        std::complex<double> q = std::polar(radius, TAU * turns);
        std::array<std::complex<double>, MAX_DEPTH> r{};
        build(q, MAX_DEPTH, r);
        auto ct = controls(r, MAX_DEPTH);
        for (size_t n = 0; n < namagiri_reference::DEPTH; ++n) {
            const double* ref = &data[(c * namagiri_reference::DEPTH + n) * 7];
            assert(std::abs(r[n].real() - ref[2]) < 1e-9);
            assert(std::abs(r[n].imag() - ref[3]) < 1e-9);
            assert(std::abs(ct[n].mag - ref[4]) < 1e-9);
            assert(std::abs(ct[n].phase - ref[5]) < 1e-9);
            assert(std::abs(ct[n].delta - ref[6]) < 1e-9);
        }
    }
}

static void qZeroAndGuards() {
    // q = 0 is defined: R = 0, and every output stays finite.
    std::array<std::complex<double>, MAX_DEPTH> r{};
    build(std::complex<double>(0, 0), MAX_DEPTH, r);
    auto ct = controls(r, MAX_DEPTH);
    for (int n = 0; n < MAX_DEPTH; ++n) {
        assert(r[n] == std::complex<double>(0, 0));
        assert(ct[n].mag == 0 && ct[n].delta == 0);
    }
    // The denominator guard: a q where Python's namagiri raises on an intermediate tail
    // (1 + q is ~1e-13) stays finite and defined, replaced by DENOM_MIN as in ops_namagiri.
    std::complex<double> q(-1 + 1e-13, 0);
    build(q, MAX_DEPTH, r);
    for (int n = 0; n < MAX_DEPTH; ++n) {
        assert(std::isfinite(r[n].real()) && std::isfinite(r[n].imag()));
        assert(std::abs(r[n]) < 2e13);
    }
    // Non-finite parameter voltages never reach the table: the tick clamps them.
    Namagiri m; Parameters p; Inputs in; p.radius = std::numeric_limits<double>::infinity();
    p.turns = std::numeric_limits<double>::quiet_NaN(); p.depth = 0; p.settle = -1;
    auto out = m.tick(p, in, 48000);
    assert(std::isfinite(out.c.real) && std::isfinite(out.c.imag) && std::isfinite(out.c.delta));
    assert(std::isfinite(out.c.mag) && std::isfinite(out.c.phase));
    assert(out.step >= 1 && out.step <= MAX_DEPTH);
}

static void patternParity() {
    // Every step matches HydraPy's namagiri_pattern.pattern: the velocity from HydraPy's delta
    // (0 on step 1), the note from the phase, and a rest exactly where pattern() omits an event.
    const double* data = namagiri_pattern_reference::DATA;
    for (size_t s = 0; s < namagiri_pattern_reference::SETTINGS; ++s) {
        int root = namagiri_pattern_reference::ROOTS[s / 9];
        int span = namagiri_pattern_reference::SPANS[s / 3 % 3];
        double threshold = namagiri_pattern_reference::THRESHOLDS[s % 3];
        for (size_t c = 0; c < namagiri_pattern_reference::CASES; ++c) {
            double radius = namagiri_reference::DATA[(c * namagiri_reference::DEPTH) * 7];
            double turns = namagiri_reference::DATA[(c * namagiri_reference::DEPTH) * 7 + 1];
            std::array<std::complex<double>, MAX_DEPTH> r{};
            build(std::polar(radius, TAU * turns), MAX_DEPTH, r);
            for (size_t n = 0; n < namagiri_pattern_reference::DEPTH; ++n) {
                const double* ref = &data[((s * namagiri_pattern_reference::CASES + c) * namagiri_pattern_reference::DEPTH + n) * 2];
                double change = n >= 1 ? std::abs(r[n] - r[n - 1]) : 0;
                double velocity = patternVelocity(change);
                assert(std::abs(velocity - ref[0]) < 1e-9);
                bool rest = velocity < threshold;
                if (!rest) {
                    int note = patternNote(std::arg(r[n]), root, span);
                    assert(note == int(ref[1]));
                } else {
                    assert(int(ref[1]) == 0);   // the generator writes 0 for rests
                }
            }
        }
    }
}

static void noteGateAndVelocity() {
    // The engine walks a cycle with HydraPy's note mapping: q real positive, so every phase is
    // angle 0 and every note is root + floor(span/2). Step 1 has velocity 0 (HydraPy's delta):
    // an event at velocity 0 keeps its note and VEL, but GATE stays low by design.
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.radius = .5; p.depth = 8;
    p.root = 48; p.span = 24; p.velocityThreshold = 0;
    in.clock.clockConnected = true;
    auto out = m.tick(p, in, 48000);
    assert(out.step == 1 && out.note == 48 + 24 / 2 && out.velocity == 0);
    assert(!out.rest && !out.gate);                                     // velocity-0 event: no gate
    for (int t = 0; t < 8 * 16; ++t) {
        in.clock.clock = t % 8 == 4 ? 10 : 0;
        out = m.tick(p, in, 48000);
        if (out.advanced) {
            assert(!out.rest);
            assert(out.note == 60 && out.velocity >= 0 && out.velocity < 1);
            assert(out.gate == (out.step != 1));                        // step 1 never gates
        }
    }
    // A threshold above every velocity rests every step after the first: the note holds.
    p.velocityThreshold = 1;
    in.clock.clock = 0; m.tick(p, in, 48000);
    in.clock.clock = 10; out = m.tick(p, in, 48000);
    assert(out.rest && !out.gate && out.velocity < 1);
    assert(out.note == 60);                                             // the held note survives rests
    // The pattern controls clamp: root/span/threshold stay in range and every output is finite.
    p.root = 500; p.span = 0; p.velocityThreshold = -1;
    in.clock.clock = 0; m.tick(p, in, 48000);
    in.clock.clock = 10; out = m.tick(p, in, 48000);
    assert(out.note >= 0 && out.note <= 127 && std::isfinite(out.velocity));
    assert(!out.rest && out.gate);                                      // step 3: a real event
}

static void phaseEncoding() {
    // The word is the angle as a signed fraction of pi, offset binary (spec section 6): 0x80 is
    // angle 0; decoding through bus::bipolar reads the angle back to within one step.
    assert(phaseWord(0) == 128);
    assert(phaseWord(PI) == 255);            // just below +pi saturates to 0xFF
    assert(phaseWord(-PI) == 0);
    assert(phaseWord(PI / 2) == 192);
    assert(phaseWord(-PI / 2) == 64);
    int previous = -1;
    for (int k = 0; k < 256; ++k) {
        double phase = (k - 128) * (PI / 128);
        int word = phaseWord(phase);
        assert(word >= previous);            // monotonic in the angle
        previous = word;
        double back = bus::bipolar(uint8_t(word)) * PI;
        assert(std::abs(back - phase) <= PI / 128 + 1e-9);
    }
    assert(phaseWord(std::numeric_limits<double>::quiet_NaN()) == 128);   // non-finite: angle 0
}

static void clockingAndWrap() {
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.radius = .5; p.depth = 4;
    in.clock.clockConnected = true;
    auto out = m.tick(p, in, 48000);
    assert(out.step == 1 && out.loaded && out.clk);                      // first sample publishes
    assert(out.c.delta == out.c.mag);                                    // delta 1 = |R_1|, from silence
    std::array<std::complex<double>, MAX_DEPTH> ref{};
    build(std::complex<double>(.5, 0), p.depth, ref);
    int edges = 0, eocs = 0;
    for (int t = 0; t < 8 * 10; ++t) {                                   // five pulses, one wrap
        in.clock.clock = t % 8 == 4 ? 10 : 0;
        out = m.tick(p, in, 48000);
        if (out.advanced) {
            ++edges; eocs += out.eoc; assert(out.clk);
            if (edges % 4 == 0) {
                assert(out.step == 1 && out.eoc);                        // a wrap publishes step 1
                assert(std::abs(out.c.delta - std::abs(ref[0] - ref[3])) < 1e-9);  // the wrap jump
            } else {
                int step = edges % 4;                                    // 1..3
                assert(out.step == step + 1 && !out.eoc);
                assert(std::abs(out.c.delta - std::abs(ref[step] - ref[step - 1])) < 1e-9);
                assert(std::abs(out.c.real - ref[step].real()) < 1e-9);
            }
        }
    }
    assert(edges == 10 && eocs == 2);
    auto last = out;
    in.clock.clock = 0;                                                  // outputs hold between edges
    out = m.tick(p, in, 48000);
    assert(!out.advanced && !out.clk && out.c.real == last.c.real && out.c.imag == last.c.imag);
}

static void resetPriority() {
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.depth = 6;
    m.tick(p, in, 48000);                                                // publishes step 1
    in.clock.clockConnected = true; in.clock.clock = 10;
    in.clock.reset = 10;                                                       // RESET with the edge
    auto out = m.tick(p, in, 48000);
    assert(out.step == 1 && !out.advanced);                              // RESET wins over the edge
    in.clock.reset = 0; in.clock.clock = 0;
    out = m.tick(p, in, 48000);
    assert(out.step == 1 && out.loaded && out.clk);                      // the publication, one sample on
    in.clock.clock = 0; m.tick(p, in, 48000);                            // the strobe's mandatory low sample
    in.clock.clock = 10; out = m.tick(p, in, 48000);
    assert(out.step == 2 && out.advanced);                               // counting resumes
}

static void convergedGate() {
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.radius = .5; p.depth = 8;
    p.settle = 1e9;                                                   // everything converges
    in.clock.clockConnected = true;
    auto out = m.tick(p, in, 48000);                                     // fresh publish: |R_1| < 1e9
    assert(out.converged);
    p.settle = 0;                                                     // nothing converges...
    Namagiri m2; Inputs in2; in2.clock.clockConnected = true;
    for (int t = 0; t < 8 * 12; ++t) {
        in2.clock.clock = t % 8 == 4 ? 10 : 0;
        out = m2.tick(p, in2, 48000);
        assert(!out.converged);                                          // ...so the gate stays low
        assert(!out.eoc || out.step == 1);
    }
    // A mid-range threshold latches once a step's change falls below it and stays until the
    // wrap, where the gate drops: .02 is below step 2's change (.041) and step 3's (.004) but
    // above the wrap jump (.0377), so the latch lands on step 3 and clears at every wrap.
    Namagiri n; p.settle = .02; p.depth = 16;
    in.clock.clock = 0; n.tick(p, in, 48000);
    bool latched = false; int latchedAt = 0; int drops = 0;
    for (int t = 0; t < 8 * 78; ++t) {
        in.clock.clock = t % 8 == 4 ? 10 : 0;
        auto o = n.tick(p, in, 48000);
        if (o.advanced && !o.eoc) {
            if (o.step >= 3) assert(o.converged);                        // held from step 3 on
            if (!latched && o.converged) { latched = true; latchedAt = o.step; }
        }
        if (o.eoc) { drops++; assert(!o.converged); latched = false; }   // the gate drops at the wrap
    }
    assert(latched && latchedAt == 3 && drops == 4);
}

static void busAddress() {
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.depth = 32;
    m.tick(p, in, 48000);                                                // step 1
    // An unclocked bus sets the step directly every sample: word w -> step (w % 32) + 1.
    in.address.connected = true; in.address.updated = true; in.address.word = 0x1f;
    auto out = m.tick(p, in, 48000);
    assert(out.step == 32 && out.clk);                                   // published with a strobe
    in.address.word = 0; out = m.tick(p, in, 48000);
    assert(out.step == 1);
    in.address.connected = false;                                        // the clock owns n again
    in.clock.clockConnected = true; in.clock.clock = 10;
    out = m.tick(p, in, 48000);
    assert(out.step == 2 && out.advanced);
    // A clocked bus sets the step on its CLK edge (its latched word), like Fan's address.
    Namagiri n; Inputs i2; p.depth = 12;
    n.tick(p, i2, 48000);
    i2.address.connected = true; i2.address.clocked = true; i2.address.updated = true;
    i2.address.word = 3;                                                 // step 4
    out = n.tick(p, i2, 48000);
    assert(out.step == 4);
    i2.address.word = 20; i2.address.updated = false;                    // data changes, no CLK edge
    out = n.tick(p, i2, 48000);
    assert(out.step == 4);
    i2.address.updated = true; out = n.tick(p, i2, 48000);
    assert(out.step == 9);                                               // 20 % 12 + 1
    // A bus set on the same sample as a clock edge lands after the edge.
    i2.clock.clockConnected = true; i2.clock.clock = 10;
    i2.address.word = 0; i2.address.updated = true;
    out = n.tick(p, i2, 48000);
    assert(out.step == 1 && out.advanced);                               // advanced, then overridden
}

static void depthAndQChanges() {
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.depth = 32; p.radius = .9;
    m.tick(p, in, 48000);
    in.clock.clockConnected = true; in.clock.clock = 10;
    for (int t = 0; t < 8 * 20; ++t) {
        in.clock.clock = t % 8 == 4 ? 10 : 0;
        m.tick(p, in, 48000);
    }
    assert(m.step == 21);
    p.depth = 8;                                                         // shrinks below the step
    auto out = m.tick(p, in, 48000);
    assert(out.step == 8 && out.clocked);                                // clamped to the new DEPTH
    p.radius = .05;                                                      // a q change re-holds at once
    out = m.tick(p, in, 48000);
    assert(out.step == 8);
    assert(std::isfinite(out.c.real) && std::abs(out.c.mag) < 1);
}

static void internalAndEngine() {
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Internal; p.rateHz = 48000 / 8; p.depth = 4;
    auto out = m.tick(p, in, 48000);                                     // publishes step 1
    int steps = 0;
    for (int t = 0; t < 128; ++t) {                                      // 16 internal edges at 6000 Hz
        out = m.tick(p, in, 48000);
        steps += out.advanced;
    }
    assert(steps == 16 && out.step == 1);
    Namagiri e; p.mode = ClockMode::Engine; p.depth = 4;                 // Engine: every sample
    p.rateHz = 8;
    e.tick(p, in, 48000);
    steps = 0; int eocs = 0;
    for (int t = 0; t < 4800; ++t) {
        out = e.tick(p, in, 48000);
        steps += out.advanced; eocs += out.eoc;
        assert(!out.clk);                                                // no CLK channel in Engine mode
    }
    assert(steps == 4800 && eocs == 1200);
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        Namagiri r; p.mode = ClockMode::Internal; p.depth = 32; p.rateHz = 8;
        r.tick(p, in, sr);
        int edges = 0;
        for (int t = 0; t < 8 * int(sr); ++t) edges += r.tick(p, in, sr).advanced;
        assert(edges == 64);                                             // 8 Hz for 8 seconds
    }
}

int main() {
    referenceParity();
    patternParity();
    qZeroAndGuards();
    noteGateAndVelocity();
    phaseEncoding();
    clockingAndWrap();
    resetPriority();
    convergedGate();
    busAddress();
    depthAndQChanges();
    internalAndEngine();
    std::cout << "PASS: Namagiri convergents, controls and the note pattern against the HydraPy reference grids to 1e-9 (notes and rests exact); q = 0 and the guarded denominators; V/OCT mapping, GATE and VEL; phase-word encoding against spec section 6; clock stepping, wraps, EOC and held outputs; RESET priority; the CONVERGED latch; BUS IN step words; DEPTH and q changes; internal and Engine clocks at 44.1/48/96 kHz\n";
}
