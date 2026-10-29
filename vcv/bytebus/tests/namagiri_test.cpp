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

static void resetPrevious() {
    // RESET is a fresh start: previous goes back to zero before the hold, so the
    // first publication's DELTA is |R_1| on every reset, not 0 on a repeated reset
    // and not |R_1 - R_n| from another step, and CONVERGED clears. ClockOwner's
    // deferred load publication and priority still decide when the strobe comes.
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.radius = .5; p.depth = 8; p.settle = .02;
    in.clock.clockConnected = true;
    std::array<std::complex<double>, MAX_DEPTH> ref{};
    build(std::complex<double>(.5, 0), p.depth, ref);
    const double r1 = std::abs(ref[0]);
    auto fresh = m.tick(p, in, 48000);                                   // first publication
    assert(fresh.step == 1 && fresh.loaded && fresh.clk);
    assert(std::abs(fresh.c.delta - r1) < 1e-9 && !fresh.converged);
    // RESET at step 1: the previous value returns to silence, so DELTA is |R_1|
    // again. The clock was high, so ClockOwner defers the load strobe; the word
    // and its delta publish now, the CLK edge one sample later.
    in.clock.reset = 10;
    auto out = m.tick(p, in, 48000);
    assert(out.step == 1 && !out.converged && !out.loaded && !out.clk);
    assert(std::abs(out.c.delta - r1) < 1e-9);
    in.clock.reset = 0;
    out = m.tick(p, in, 48000);
    assert(out.step == 1 && out.loaded && out.clk);                      // the deferred publication
    assert(std::abs(out.c.delta - r1) < 1e-9);                           // not re-held to 0
    // A repeated RESET, still on step 1: |R_1| every time.
    in.clock.reset = 10;
    out = m.tick(p, in, 48000);
    assert(out.step == 1 && !out.converged && std::abs(out.c.delta - r1) < 1e-9);
    in.clock.reset = 0; m.tick(p, in, 48000);                            // its deferred publication
    for (int t = 0; t < 13; ++t) {                                       // two edges: step 3
        in.clock.clock = t % 8 == 4 ? 10 : 0;
        m.tick(p, in, 48000);
    }
    assert(m.step == 3);
    // RESET from another step, with a bus word on the same sample: RESET wins,
    // and the delta is the fresh |R_1|, not |R_1 - R_3|.
    in.clock.clock = 0; in.clock.reset = 10;
    in.address.connected = true; in.address.updated = true; in.address.word = 5;
    out = m.tick(p, in, 48000);
    assert(out.step == 1 && !out.advanced && !out.converged);
    assert(std::abs(out.c.delta - r1) < 1e-9);
    in.address.connected = false; in.address.updated = false;
    in.clock.reset = 0;
    out = m.tick(p, in, 48000);
    assert(out.step == 1 && out.loaded && out.clk);
    assert(std::abs(out.c.delta - r1) < 1e-9);
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
    // The word publishes at once, but its strobe obeys the mandatory low sample
    // after the load's high: the update after the first sample is a low, then a
    // high on the next sample, so a downstream clocked reader sees one edge.
    in.address.connected = true; in.address.updated = true; in.address.word = 0x1f;
    auto out = m.tick(p, in, 48000);
    assert(out.step == 32 && !out.clk);                                  // published, strobe deferred
    out = m.tick(p, in, 48000);
    assert(out.step == 32 && out.clk);                                   // the bus strobe's own edge
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

// A one-sample 9-channel cable for the portable tests: the Writer fills the
// module's output and the next sample's Reader reads it, as a Rack cable does.
struct TestCable {
    std::array<float, 9> volts{};
    int channels = 0;
    void setChannels(int n) { channels = n; for (int k = n; k < 9; ++k) volts[k] = 0; }
    int getChannels() const { return channels; }
    void setVoltage(float v, int k) { volts[k] = v; }
    float getVoltage(int k) const { return k < channels ? volts[k] : 0.f; }
};

static void strobeSeparation() {
    // BUS OUT CLK is one high per publication with a mandatory low sample. A
    // changed BUS IN word on the sample after a clock edge publishes its word
    // at once but waits a low sample, then raises its own high, so a bus Reader
    // on a one-sample cable captures both edges instead of losing the second.
    Namagiri m; Parameters p; Inputs in;
    p.mode = ClockMode::Jack; p.radius = .5; p.depth = 8;
    in.clock.clockConnected = true;
    TestCable out, cable;                        // cable is the one-sample delay
    bus::Reader reader;
    auto tick = [&](float clock) {
        in.clock.clock = clock;
        auto o = m.tick(p, in, 48000);
        bus::Writer{}.write(out, o.word, o.clocked, o.clk);
        auto seen = reader.process(cable);
        cable = out;
        return std::make_pair(o, seen);
    };
    auto load = tick(0);                         // sample 0: load, step 1, CLK high
    assert(load.first.loaded && load.first.clk);
    auto loaded = tick(0);                       // sample 1: the reader sees the load edge
    assert(loaded.second.updated && loaded.second.word == load.first.word);
    auto edge = tick(10);                        // sample 2: clock edge, step 2, CLK high
    assert(edge.first.advanced && edge.first.step == 2 && edge.first.clk);
    // A changed BUS IN word on the very next sample publishes step 7 now, with
    // CLK low; downstream sees the clock edge's word, then the bus word's own
    // edge one low sample later.
    in.address.connected = true; in.address.updated = true; in.address.word = 6;
    auto bus = tick(0);                          // sample 3: published, strobe deferred
    assert(bus.first.step == 7 && !bus.first.clk);
    assert(bus.second.updated && bus.second.word == edge.first.word);
    const uint8_t word7 = bus.first.word;
    auto held = tick(0);                         // sample 4: the deferred bus strobe
    assert(held.first.step == 7 && held.first.clk);
    assert(!held.second.updated);                // the mandatory low sample in between
    auto delayed = tick(0);                      // sample 5: the reader sees the bus edge
    assert(delayed.second.updated && delayed.second.word == word7);
    assert(!delayed.first.clk);
    // Adjacent bus updates: each waits for its own low sample, so the Reader
    // captures both words in order rather than merging them into one high.
    in.address.word = 1; auto b1 = tick(0);      // sample 6: step 2, a normal high
    assert(b1.first.step == 2 && b1.first.clk);
    in.address.word = 2; auto b2 = tick(0);      // sample 7: step 3, deferred past the high
    assert(b2.first.step == 3 && !b2.first.clk);
    assert(b2.second.updated && b2.second.word == b1.first.word);
    in.address.updated = false;
    auto b3 = tick(0);                           // sample 8: the deferred high for step 3
    assert(b3.first.step == 3 && b3.first.clk);
    assert(!b3.second.updated);
    auto b4 = tick(0);                           // sample 9: the reader captures step 3
    assert(!b4.first.clk);
    assert(b4.second.updated && b4.second.word == b2.first.word);
    // Simultaneous clock edge and bus word: one publication, one CLK high, the
    // bus word (step 6) downstream once.
    in.address.updated = true; in.address.word = 5;
    auto both = tick(10);                        // sample 10
    assert(both.first.advanced && both.first.step == 6 && both.first.clk);
    in.address.updated = false;
    auto bothLow = tick(0);                      // sample 11: mandatory low
    assert(!bothLow.first.clk);
    assert(bothLow.second.updated && bothLow.second.word == both.first.word);
    auto bothEnd = tick(0);                      // sample 12: no repeated capture
    assert(!bothEnd.second.updated);
    // Engine mode has no CLK channel at all, including a bus publication.
    Namagiri e; Inputs ei; Parameters ep;
    ep.mode = ClockMode::Engine; ep.radius = .5; ep.depth = 8;
    e.tick(ep, ei, 48000);
    ei.address.connected = true; ei.address.updated = true; ei.address.word = 3;
    auto eo = e.tick(ep, ei, 48000);
    assert(eo.advanced && eo.step == 4 && !eo.clocked && !eo.clk);
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
    resetPrevious();
    convergedGate();
    busAddress();
    strobeSeparation();
    depthAndQChanges();
    internalAndEngine();
    std::cout << "PASS: Namagiri convergents, controls and the note pattern against the HydraPy reference grids to 1e-9 (notes and rests exact); q = 0 and the guarded denominators; V/OCT mapping, GATE and VEL; phase-word encoding against spec section 6; clock stepping, wraps, EOC and held outputs; RESET priority and the fresh |R_1| delta on every reset; the CONVERGED latch; BUS IN step words; separate bus and clock strobes through a one-sample cable; DEPTH and q changes; internal and Engine clocks at 44.1/48/96 kHz\n";
}
