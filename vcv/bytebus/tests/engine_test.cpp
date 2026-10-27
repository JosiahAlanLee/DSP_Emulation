// Portable protocol and state-machine tests; no Rack SDK.
#include "../src/engine/Register.hpp"
#include "../src/engine/Counter.hpp"
#include "../src/engine/Switch.hpp"
#include "../src/engine/Pipeline.hpp"
#include "../src/engine/Counter2.hpp"
#include "../src/engine/Fan.hpp"
#include "../src/engine/Lattice.hpp"
#include "../src/engine/ParallelSum.hpp"
#include "../src/engine/Wheel.hpp"
#include "../src/engine/Roulette.hpp"
#include <set>
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
using namespace dspemu;
using namespace dspemu::byte_register;

struct Port {
    int channels = 0;
    std::array<float, 16> voltage{};
    int getChannels() { return channels; }
    float getVoltage(int k) { return voltage[k]; }
    void setChannels(int n) {
        for (int i = n; i < channels; ++i) voltage[i] = 0;
        channels = n;
    }
    void setVoltage(float v, int k = 0) { voltage[k] = v; }
};
static void set(Port& port, unsigned word, bool clocked = false, bool clk = false) {
    bus::Writer{}.write(port, uint8_t(word), clocked, clk);
}

static void conversions() {
    for (int n = 0; n < 256; ++n) {
        assert(bus::fromBipolar(bus::bipolar(n)) == n);
        assert(bus::fromVolts(bus::toVolts(n)) == n);
        assert(int(std::round(bus::phase(n) * 256)) == n);
        assert(int(std::round(bus::level(n) * 255)) == n);
        Port p; set(p, n);
        bus::Reader r; auto s = r.process(p);
        assert(s.word == n && s.rawWord == n && s.updated && !s.clocked);
        assert(p.channels == 8);
        for (int k = 0; k < 8; ++k) assert(p.voltage[k] == (n & (1 << k) ? 10.f : 0.f));
    }
    assert(bus::fromVolts(-100) == 0 && bus::fromVolts(100) == 255);
    assert(bus::fromVolts(0) == 128 && bus::fromBipolar(std::numeric_limits<float>::infinity()) == 128);
    assert(bus::fromVolts(5.f / 256) == 129); // half-up at the midpoint
    int previous = 0;
    for (int k = -10000; k < 10000; ++k) {
        int word = bus::fromVolts(k * .001f); assert(word >= previous); previous = word;
    }
}

static void reader() {
    Port p; bus::Reader r;
    set(p, 0xa5, true); auto s = r.process(p);
    assert(s.connected && s.clocked && !s.updated && s.rawWord == 0xa5 && s.word == 0);
    set(p, 0xa5, true, true); s = r.process(p);
    assert(s.rising && s.word == 0xa5);
    set(p, 0x5a, true, true); s = r.process(p);
    assert(!s.rising && !s.updated && s.word == 0xa5 && s.rawWord == 0x5a);
    set(p, 0x5a, true, false); r.process(p);
    set(p, 0x5a, true, true); assert(r.process(p).word == 0x5a);
    // Mid-threshold voltage retains each line's state; NaN clears it.
    p.voltage[1] = .5f; assert(r.process(p).rawWord & 2);
    p.voltage[1] = std::numeric_limits<float>::quiet_NaN(); assert(!(r.process(p).rawWord & 2));
    // Missing channels are low, including on a mono gate; no poly broadcast.
    p.channels = 1; p.voltage[0] = 10; s = r.process(p);
    assert(s.word == 1 && !s.clocked && s.updated);
    p.channels = 0; s = r.process(p); assert(!s.connected && s.word == 0 && !s.clk);
    // Connecting an already-high clock is explicitly the first edge.
    set(p, 0xc3, true, true); s = r.process(p); assert(s.rising && s.word == 0xc3);
    p.channels = 16; p.voltage[9] = p.voltage[15] = 10;
    s = r.process(p); assert(!s.rising && s.word == 0xc3);
    bus::Writer{}.write(p, 0xff, false, false);
    assert(p.channels == 8 && p.voltage[8] == 0 && p.voltage[15] == 0);
    bus::Gate gate;
    assert(!gate.process(.5f)); assert(gate.process(1.f));
    assert(!gate.process(.5f) && gate.high); assert(!gate.process(.1f) && !gate.high);
}

static void clocking() {
    Register r; Parameters p; Inputs in; in.data.connected = true; in.data.rawWord = 0xa5;
    p.mode = ClockMode::Jack;
    auto out = r.tick(p, in, 48000); assert(out.loaded && out.word == 0 && out.clk);
    r.tick(p, in, 48000); // low sample after initial publication
    for (int k = 0; k < 100; ++k) assert(!r.tick(p, in, 48000).captured);
    in.clockConnected = true; in.clock = 10;
    out = r.tick(p, in, 48000); assert(out.captured && out.word == 0xa5 && out.clk);
    in.data.rawWord = 0x5a;
    for (int k = 0; k < 10; ++k) assert(r.tick(p, in, 48000).word == 0xa5);
    in.clock = 0; r.tick(p, in, 48000); in.clock = 10;
    out = r.tick(p, in, 48000); assert(out.word == 0x5a && out.captured);
    // Local clock samples raw data, not the last word latched by upstream CLK.
    in.data.clocked = true; in.data.word = 3; in.data.rawWord = 0xf0;
    in.clock = 0; r.tick(p, in, 48000); in.clock = 10;
    assert(r.tick(p, in, 48000).word == 0xf0);
    // AUTO selects by connection, never by whether edges happen to arrive.
    p.mode = ClockMode::Auto;
    for (int k = 0; k < 10000; ++k) {
        out = r.tick(p, in, 48000);
        assert(out.source == ClockSource::Jack && !out.captured);
    }
    in.clockConnected = false; in.data.rising = false;
    for (int k = 0; k < 10000; ++k) {
        out = r.tick(p, in, 48000);
        assert(out.source == ClockSource::Bus && !out.captured);
    }
    in.data.rising = true; out = r.tick(p, in, 48000);
    assert(out.captured && out.clk); in.data.rising = false;
    r.tick(p, in, 48000);
    // A selected but absent bus clock stays stopped.
    p.mode = ClockMode::Bus; in.data.clocked = false;
    for (int k = 0; k < 100; ++k) assert(!r.tick(p, in, 48000).captured);
    // Engine-rate updates cannot be represented as rising edges every sample.
    p.mode = ClockMode::Engine;
    for (int k = 0; k < 256; ++k) {
        in.data.rawWord = k; out = r.tick(p, in, 48000);
        assert(out.captured && out.word == k && !out.clocked && !out.clk);
    }
}

static void reset() {
    Register r; Parameters p; Inputs in; p.mode = ClockMode::Jack; p.resetWord = 0xa5;
    in.clockConnected = true; in.data.connected = true; in.data.rawWord = 255;
    r.tick(p, in, 48000); r.tick(p, in, 48000);
    in.clock = 10; in.reset = 10;
    auto out = r.tick(p, in, 48000);
    assert(out.loaded && !out.captured && out.word == 0xa5 && out.clk);
    // RESET is an edge, not a level; held reset and held CLK do not retrigger.
    for (int k = 0; k < 100; ++k) assert(!r.tick(p, in, 48000).loaded);
    in.clock = in.reset = 0; r.tick(p, in, 48000);
    in.clock = 10; out = r.tick(p, in, 48000); assert(out.captured && out.clk && out.word == 255);
    // Reset immediately after a strobe must insert low before publishing its seed.
    in.reset = 10; out = r.tick(p, in, 48000);
    assert(out.word == 0xa5 && !out.clk && !out.loaded);
    out = r.tick(p, in, 48000); assert(out.clk && out.loaded && !out.captured);
    assert(!r.tick(p, in, 48000).clk);
    // An unpublished seed beats nearby clocks and is visible to a clocked receiver.
    Port output; bus::Reader receiver;
    bus::Writer{}.write(output, 0, true, false); receiver.process(output);
    r.clear(); in = {}; in.data.connected = true; in.data.rawWord = 255;
    in.resetButton = true;
    out = r.tick(p, in, 48000); bus::Writer{}.write(output, out.word, out.clocked, out.clk);
    assert(receiver.process(output).word == 0xa5);
    // No data cable: every selected edge republishes the held word.
    r.clear(0x3c); in = {}; r.tick(p, in, 48000); r.tick(p, in, 48000);
    in.clockConnected = true; in.clock = 10;
    assert(r.tick(p, in, 48000).word == 0x3c);
}

static void internal() {
    for (double sr : {44100., 48000., 96000.}) {
        Register r; Parameters p; Inputs in; p.mode = ClockMode::Internal; p.rateHz = 16;
        in.data.connected = true; in.data.rawWord = 0x42;
        int captures = 0; bool lastHigh = false;
        std::vector<int> edges;
        for (int t = 0; t < int(sr * 2); ++t) {
            auto out = r.tick(p, in, sr);
            assert(!(lastHigh && out.clk)); lastHigh = out.clk;
            if (out.captured) { ++captures; edges.push_back(t); assert(out.word == 0x42); }
        }
        assert(captures >= 31 && captures <= 32);
        for (size_t k = 1; k < edges.size(); ++k) assert(std::abs((edges[k] - edges[k-1]) - sr / 16) <= 1);
        // Reset restarts the phase: no old fractional period leaks into replay.
        in.reset = 10; r.tick(p, in, sr); in.reset = 0;
        int first = -1;
        for (int t = 1; t < int(sr / 8); ++t) if (r.tick(p, in, sr).captured) { first = t; break; }
        assert(std::abs(first - sr / 16) <= 1);
    }
    Register r; Parameters p; Inputs in; p.rateHz = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 100; ++i) r.tick(p, in, std::numeric_limits<double>::quiet_NaN());
    assert(std::isfinite(r.phase));
}

// Counter (docs/COUNTER.md): the same clock rules as Register, with (word + increment) mod modulus.
static void counting() {
    namespace ct = dspemu::counter;
    // Every (word, increment, modulus) through tick: Engine mode makes every sample an edge,
    // and an unclocked BUS IN supplies increments 0-255 (INC alone cannot be 0).
    {
        ct::Parameters p; p.mode = ClockMode::Engine; ct::Inputs in; in.data.connected = true;
        uint64_t carries = 0;
        for (int mod = 2; mod <= 256; ++mod) {
            p.modulus = mod;
            for (int w = 0; w < 256; ++w) for (int inc = 0; inc < 256; ++inc) {
                ct::Counter c; c.clear(uint8_t(w)); in.data.rawWord = uint8_t(inc);
                auto first = c.tick(p, in, 48000); assert(first.loaded && !first.advanced && first.word == w && !first.carry);
                auto out = c.tick(p, in, 48000);
                assert(out.advanced && !out.clocked && !out.clk);
                assert(out.word == (w + inc) % mod && out.carry == (w + inc >= mod));
                carries += out.carry;
            }
        }
        assert(carries > 0);
    }
    // INC counts when BUS IN is unpatched. A patched BUS IN replaces it; 00 holds the count.
    {
        ct::Parameters p; p.mode = ClockMode::Engine; p.increment = 3; p.modulus = 10;
        ct::Counter c; ct::Inputs none; c.tick(p, none, 48000);
        std::vector<int> seen; std::vector<bool> carry;
        for (int k = 0; k < 5; ++k) { auto o = c.tick(p, none, 48000); seen.push_back(o.word); carry.push_back(o.carry); }
        assert((seen == std::vector<int>{3, 6, 9, 2, 5}) && (carry == std::vector<bool>{false, false, false, true, false}));
        ct::Inputs zero; zero.data.connected = true;
        for (int k = 0; k < 10; ++k) assert(c.tick(p, zero, 48000).word == 5);
        // modulus - k counts down by k.
        p.modulus = 256; p.increment = 255; c.clear(2); c.tick(p, none, 48000);
        assert(c.tick(p, none, 48000).word == 1 && c.tick(p, none, 48000).word == 0);
        auto under = c.tick(p, none, 48000); assert(under.word == 255 && under.carry == false);
        // In Engine mode CARRY reports every wrapping sample, so it can stay high.
        p.modulus = 2; p.increment = 1; c.clear(0); c.tick(p, none, 48000);
        for (int k = 0; k < 8; ++k) assert(c.tick(p, none, 48000).carry == (k % 2 == 1));
    }
    // A local CLOCK reclocks BUS IN's raw data lines, not the bus's own latched word.
    {
        ct::Parameters p; p.mode = ClockMode::Jack; ct::Counter c; ct::Inputs in; in.clockConnected = true;
        in.data.connected = in.data.clocked = true; in.data.word = 1; in.data.rawWord = 7;
        auto seed = c.tick(p, in, 48000); assert(seed.loaded && seed.clk);
        assert(!c.tick(p, in, 48000).clk);
        in.clock = 10; auto o = c.tick(p, in, 48000); assert(o.advanced && o.word == 7 && o.clk);
        in.clock = 0; for (int k = 0; k < 50; ++k) { o = c.tick(p, in, 48000); assert(!o.advanced && o.word == 7); } // frozen
    }
    // Bus CLK (Auto with a clocked BUS IN): advances on the bus's rising edge only.
    {
        ct::Parameters p; ct::Counter c; ct::Inputs in; in.data.connected = in.data.clocked = true; in.data.rawWord = 2;
        c.tick(p, in, 48000); c.tick(p, in, 48000);
        for (int k = 0; k < 6; ++k) {
            in.data.rising = k % 3 == 0;
            auto o = c.tick(p, in, 48000); assert(o.source == ClockSource::Bus && o.advanced == in.data.rising);
        }
        assert(c.word == 4);
    }
    // A SEED at or above MOD is published as it is and wraps on the next edge; lowering MOD
    // between edges changes nothing until the next edge. CARRY coincides with the strobe.
    {
        ct::Parameters p; p.mode = ClockMode::Jack; p.modulus = 16; p.seed = 200;
        ct::Counter c; ct::Inputs in; in.clockConnected = true;
        in.reset = 10; auto o = c.tick(p, in, 48000); assert(o.loaded && o.word == 200 && o.clk && !o.carry);
        in.reset = 0; c.tick(p, in, 48000);
        in.clock = 10; o = c.tick(p, in, 48000); assert(o.advanced && o.word == 201 % 16 && o.carry && o.clk);
        in.clock = 0; o = c.tick(p, in, 48000); assert(!o.carry && !o.clk);
        p.modulus = 4; o = c.tick(p, in, 48000); assert(o.word == 9);
        in.clock = 10; o = c.tick(p, in, 48000); assert(o.word == 2 && o.carry);
        // RESET wins over a simultaneous edge: the seed loads and nothing is added.
        in.clock = 0; c.tick(p, in, 48000); c.tick(p, in, 48000);
        in.clock = 10; in.reset = 10; o = c.tick(p, in, 48000); assert(o.loaded && !o.advanced && o.word == 200 && !o.carry);
        // Acquisition is suppressed for the seed strobe and the low sample after it.
        in.clock = 0; in.reset = 0; o = c.tick(p, in, 48000); assert(!o.clk);
        in.clock = 10; o = c.tick(p, in, 48000); assert(o.advanced && o.word == 201 % 4);
    }
    // Strobes always have a low sample between them, under random clocks and resets.
    {
        std::mt19937 rng(5); ct::Parameters p; p.mode = ClockMode::Jack; p.modulus = 7; p.increment = 3;
        ct::Counter c; ct::Inputs in; in.clockConnected = true; bool last = false; int advances = 0;
        for (int t = 0; t < 20000; ++t) {
            in.clock = rng() % 2 ? 10.f : 0.f; in.reset = rng() % 97 == 0 ? 10.f : 0.f;
            auto o = c.tick(p, in, 48000);
            assert(!(last && o.clk) && o.word < 7); last = o.clk; advances += o.advanced;
            assert(!o.carry || o.advanced);
        }
        assert(advances > 1000);
    }
    // Internal clock: 16 Hz at 44.1/48/96 kHz, and MOD 16 carries once per sixteen edges.
    for (double sr : {44100., 48000., 96000.}) {
        ct::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 16; p.modulus = 16;
        ct::Counter c; ct::Inputs in; int advances = 0, carries = 0; std::vector<int> edges;
        for (int t = 0; t < int(sr * 4); ++t) {
            auto o = c.tick(p, in, sr);
            if (o.advanced) { ++advances; edges.push_back(t); }
            carries += o.carry;
        }
        assert(advances >= 63 && advances <= 64 && carries == advances / 16);
        for (size_t k = 1; k < edges.size(); ++k) assert(std::abs((edges[k] - edges[k-1]) - sr / 16) <= 1);
    }
}

// Run modes (docs/COUNTER.md, "Run modes"): One-shot holds at MOD - 1; Pendulum reflects at
// 0 and MOD - 1, clamping an overshoot, with CARRY on each reflection.
static void runModes() {
    namespace ct = dspemu::counter;
    using ct::RunMode;
    // Every (word, increment, modulus, direction) through tick, against the contract's cases.
    {
        ct::Parameters p; p.mode = ClockMode::Engine; ct::Inputs in; in.data.connected = true;
        uint64_t holds = 0, reflections = 0;
        for (int mod = 2; mod <= 256; ++mod) {
            p.modulus = mod; int top = mod - 1;
            for (int w = 0; w < 256; ++w) for (int inc = 0; inc < 256; ++inc) {
                in.data.rawWord = uint8_t(inc);
                p.run = RunMode::OneShot;
                {
                    ct::Counter c; c.clear(uint8_t(w)); c.tick(p, in, 48000);
                    auto o = c.tick(p, in, 48000);
                    int want = w + inc < top ? w + inc : top;
                    assert(o.advanced && o.word == want && o.carry == (want == top && w != top) && !o.down);
                    holds += o.carry;
                }
                p.run = RunMode::Pendulum;
                for (bool down : {false, true}) {
                    ct::Counter c; c.clear(uint8_t(w), down); c.tick(p, in, 48000);
                    auto o = c.tick(p, in, 48000);
                    int want = w; bool wantDown = down, wantCarry = false;
                    if (w > top) { want = top; wantDown = wantCarry = true; }
                    else if (inc == 0) {}
                    else if (!down && w + inc >= top) { want = top; wantDown = wantCarry = true; }
                    else if (down && w - inc <= 0) { want = 0; wantDown = false; wantCarry = true; }
                    else want = down ? w - inc : w + inc;
                    assert(o.advanced && o.word == want && o.down == wantDown && o.carry == wantCarry);
                    assert(o.word <= top);
                    reflections += o.carry;
                }
            }
        }
        assert(holds > 0 && reflections > 0);
    }
    // The roadmap's sequence: MOD 8, increment 3. Pendulum shows each end for one edge.
    {
        ct::Parameters p; p.mode = ClockMode::Engine; p.run = RunMode::Pendulum; p.modulus = 8; p.increment = 3;
        ct::Counter c; ct::Inputs none; c.tick(p, none, 48000);
        std::vector<int> seen; std::vector<bool> carry;
        for (int k = 0; k < 9; ++k) { auto o = c.tick(p, none, 48000); seen.push_back(o.word); carry.push_back(o.carry); }
        assert((seen == std::vector<int>{3, 6, 7, 4, 1, 0, 3, 6, 7}));
        assert((carry == std::vector<bool>{false, false, true, false, false, true, false, false, true}));
        // One-shot from the same start: up only, one CARRY, then held whatever the increment.
        p.run = RunMode::OneShot; c.clear(); c.tick(p, none, 48000); seen.clear(); carry.clear();
        for (int k = 0; k < 5; ++k) { auto o = c.tick(p, none, 48000); seen.push_back(o.word); carry.push_back(o.carry); }
        assert((seen == std::vector<int>{3, 6, 7, 7, 7}) && (carry == std::vector<bool>{false, false, true, false, false}));
        // MOD - k is a large step up, not "down by k".
        p.modulus = 256; p.increment = 255; c.clear(2); c.tick(p, none, 48000);
        auto o = c.tick(p, none, 48000); assert(o.word == 255 && o.carry);
        o = c.tick(p, none, 48000); assert(o.word == 255 && !o.carry);
    }
    // Every swing reaches both ends, for every modulus and every increment that fits.
    for (int mod = 2; mod <= 256; mod += 7) for (int inc = 1; inc < mod; inc += 3) {
        ct::Parameters p; p.mode = ClockMode::Engine; p.run = RunMode::Pendulum; p.modulus = mod; p.increment = uint8_t(inc);
        ct::Counter c; ct::Inputs none; c.tick(p, none, 48000);
        int last = 0, tops = 0, bottoms = 0;
        for (int k = 0; k < 4 * mod; ++k) {
            auto o = c.tick(p, none, 48000);
            assert(o.word != last || mod == 2 || inc == 0);           // no end is shown twice
            assert(std::abs(int(o.word) - last) <= inc);               // never more than one step
            assert(o.carry == (o.word == 0 || o.word == mod - 1));     // CARRY exactly at the ends
            tops += o.word == mod - 1; bottoms += o.word == 0; last = o.word;
        }
        assert(tops >= 2 && bottoms >= 1 && std::abs(tops - bottoms) <= 1);
    }
    // RESET sets the direction up. MODE changes act on the next edge, from the current word, and
    // keep the direction: only reflections and RESET/LOAD change it.
    {
        ct::Parameters p; p.mode = ClockMode::Jack; p.run = RunMode::Pendulum; p.modulus = 10; p.increment = 4;
        ct::Counter c; ct::Inputs in; in.clockConnected = true;
        auto edge = [&] { in.clock = 10; auto o = c.tick(p, in, 48000); in.clock = 0; c.tick(p, in, 48000); return o; };
        c.tick(p, in, 48000); c.tick(p, in, 48000);
        assert(edge().word == 4 && edge().word == 8);
        auto o = edge(); assert(o.word == 9 && o.carry && o.down);
        p.run = RunMode::Loop; o = edge(); assert(o.word == 3 && o.carry && o.down);   // 13 mod 10
        p.run = RunMode::Pendulum; o = edge(); assert(o.word == 0 && o.carry && !o.down);
        o = edge(); assert(o.word == 4 && !o.down);
        o = edge(); assert(o.word == 8); o = edge(); assert(o.word == 9 && o.down);
        o = edge(); assert(o.word == 5 && o.down);
        in.reset = 10; o = c.tick(p, in, 48000); assert(o.loaded && o.word == 0 && !o.down);
        in.reset = 0; c.tick(p, in, 48000);
        o = edge(); assert(o.word == 4 && !o.down);
        // A SEED at the top: the first edge reflects there.
        p.seed = 9; in.reset = 10; c.tick(p, in, 48000); in.reset = 0; c.tick(p, in, 48000);
        o = edge(); assert(o.word == 9 && o.carry && o.down); assert(edge().word == 5);
        // A held word above the top goes to it and turns down, in either direction; with a
        // 00 increment too. One-shot clamps it the same way; Loop wraps it.
        for (RunMode run : {RunMode::Pendulum, RunMode::OneShot, RunMode::Loop}) for (bool down : {false, true}) {
            p.run = run; p.seed = 200; in.reset = 10; c.tick(p, in, 48000); in.reset = 0; c.tick(p, in, 48000);
            c.down = down; ct::Inputs zero = in; zero.data.connected = true;
            zero.clock = 10; o = c.tick(p, zero, 48000); c.tick(p, in, 48000);
            assert(o.carry && o.word == (run == RunMode::Loop ? 200 % 10 : 9));
            if (run == RunMode::Pendulum) assert(o.down); else assert(o.down == down);
        }
    }
    // A 00 increment holds in both modes, including at an end, and never reflects.
    {
        ct::Parameters p; p.mode = ClockMode::Engine; p.modulus = 16; ct::Inputs zero; zero.data.connected = true;
        for (RunMode run : {RunMode::OneShot, RunMode::Pendulum}) for (int w : {0, 7, 15}) for (bool down : {false, true}) {
            p.run = run; ct::Counter c; c.clear(uint8_t(w), down); c.tick(p, zero, 48000);
            for (int k = 0; k < 20; ++k) { auto o = c.tick(p, zero, 48000); assert(o.word == w && !o.carry && o.down == down); }
        }
    }
    // Engine rate follows the same rules every sample: MOD 2 Pendulum carries on every sample.
    {
        ct::Parameters p; p.mode = ClockMode::Engine; p.run = RunMode::Pendulum; p.modulus = 2;
        ct::Counter c; ct::Inputs none; c.tick(p, none, 48000);
        for (int k = 0; k < 8; ++k) { auto o = c.tick(p, none, 48000); assert(o.carry && o.word == (k % 2 == 0 ? 1 : 0)); }
    }
    // One-shot as an envelope: RESET restarts it; each run carries once, at the top, and holds.
    for (double sr : {44100., 48000., 96000.}) {
        ct::Parameters p; p.mode = ClockMode::Internal; p.run = RunMode::OneShot; p.rateHz = 1000; p.increment = 16;
        ct::Counter c; ct::Inputs in; int carries = 0, loads = 0; uint8_t last = 0;
        for (int t = 0; t < int(sr); ++t) {
            in.reset = t % int(sr / 10) == 0 ? 10.f : 0.f;
            auto o = c.tick(p, in, sr);
            if (o.loaded) { ++loads; last = 0; assert(o.word == 0); }
            else assert(o.word >= last);
            carries += o.carry; last = o.word;
        }
        assert(loads == 10 && carries == 10);
    }
    // Strobe gaps and range under random clocks, resets, increments and mode changes.
    {
        std::mt19937 rng(9); ct::Parameters p; p.mode = ClockMode::Jack; p.modulus = 11;
        ct::Counter c; ct::Inputs in; in.clockConnected = true; bool last = false; int advances = 0, carries = 0;
        for (int t = 0; t < 40000; ++t) {
            in.clock = rng() % 2 ? 10.f : 0.f; in.reset = rng() % 211 == 0 ? 10.f : 0.f;
            if (rng() % 500 == 0) p.run = RunMode(rng() % 3);
            p.increment = uint8_t(1 + rng() % 255);
            auto o = c.tick(p, in, 48000);
            assert(!(last && o.clk) && o.word < 11 && (!o.carry || o.advanced));
            last = o.clk; advances += o.advanced; carries += o.carry;
        }
        assert(advances > 2000 && carries > 200);
    }
}

// Switch (docs/SWITCH.md): Select, Bits and Mix on every (A, B, C), and its two clock regimes.
static void switching() {
    namespace sw = dspemu::byte_switch;
    using sw::Op; using sw::Source;
    // Every (a, b, c): Bits takes each bit from B where C has it; Select is Bits with C's chosen bit
    // spread to all eight; Mix is the nearest byte to a + (b - a) c / 255, which is never a tie.
    for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b) for (int c = 0; c < 256; ++c) {
        uint8_t bits = sw::apply(Op::Bits, 0, uint8_t(a), uint8_t(b), uint8_t(c));
        for (int k = 0; k < 8; ++k) assert((bits >> k & 1) == ((c >> k & 1) ? (b >> k & 1) : (a >> k & 1)));
        int bit = c & 7;
        assert(sw::apply(Op::Select, bit, uint8_t(a), uint8_t(b), uint8_t(c)) ==
               sw::apply(Op::Bits, 0, uint8_t(a), uint8_t(b), uint8_t((c >> bit & 1) ? 255 : 0)));
        double exact = a + (b - a) * c / 255.;
        int mix = sw::apply(Op::Mix, 0, uint8_t(a), uint8_t(b), uint8_t(c));
        assert(std::abs(mix - exact) < .5 && std::abs(std::abs(mix - exact) - .5) > 1e-9);
    }
    for (int a = 0; a < 256; a += 5) for (int b = 0; b < 256; b += 7) {
        assert(sw::apply(Op::Mix, 0, uint8_t(a), uint8_t(b), 0) == a && sw::apply(Op::Mix, 0, uint8_t(a), uint8_t(b), 255) == b);
        int last = a;                                            // Mix moves monotonically from A to B
        for (int c = 1; c < 256; ++c) {
            int m = sw::apply(Op::Mix, 0, uint8_t(a), uint8_t(b), uint8_t(c));
            assert(b >= a ? m >= last : m <= last); last = m;
        }
    }
    auto bus = [](uint8_t word, bool clocked = false, bool clk = false, bool rising = false) {
        bus::Sample s; s.connected = true; s.clocked = clocked; s.clk = clk; s.rising = rising;
        s.rawWord = word; s.updated = !clocked || rising; return s;
    };
    // Follow A, clocked: evaluated on A's CLK rising edge with B and C's raw lines on that sample,
    // held between, and A's CLK level passed through unchanged, however long it is high.
    {
        sw::Switch s; sw::Parameters p; p.op = Op::Bits; sw::Inputs in;
        in.a = bus(0x0f, true, true, true); in.b = bus(0xf0); in.c = bus(0x3c);
        auto o = s.tick(p, in, 48000); assert(o.evaluated && o.word == 0x33 && o.clocked && o.clk && !o.owned);
        in.a = bus(0xff, true, true, false); in.c = bus(0x00);
        for (int k = 0; k < 5; ++k) { o = s.tick(p, in, 48000); assert(!o.evaluated && o.word == 0x33 && o.clk); }
        in.a = bus(0xff, true, false, false); o = s.tick(p, in, 48000); assert(o.word == 0x33 && !o.clk && o.clocked);
        in.a = bus(0xff, true, true, true); o = s.tick(p, in, 48000); assert(o.word == 0xff && o.clk);
        // Unclocked A: every sample, no clock. Unpatched A reads 00; unpatched B and C read their words.
        in.a = bus(0x81); in.c = bus(0x01); p.op = Op::Select;
        o = s.tick(p, in, 48000); assert(o.word == 0xf0 && !o.clocked);
        in.c = bus(0x00); o = s.tick(p, in, 48000); assert(o.word == 0x81);
        in.a = bus::Sample{}; in.b = bus::Sample{}; in.c = bus::Sample{}; p.b = 0x5a; p.c = 0x02; p.bit = 1;
        o = s.tick(p, in, 48000); assert(o.word == 0x5a && !o.clocked);
        p.bit = 0; o = s.tick(p, in, 48000); assert(o.word == 0x00);
        p.op = Op::Mix; p.c = 128; in.a = bus(0); p.b = 255; o = s.tick(p, in, 48000); assert(o.word == 128);
    }
    // Auto: the CLOCK jack when patched, else Follow A. Taking the clock over publishes the current
    // result first; then each jack edge evaluates and strobes for one sample with a low gap.
    {
        sw::Switch s; sw::Parameters p; p.op = Op::Select; p.bit = 7; sw::Inputs in;
        in.a = bus(0x11, true, false, false); in.b = bus(0x22); in.c = bus(0x00);
        auto o = s.tick(p, in, 48000); assert(!o.owned && !o.evaluated);
        in.clockConnected = true;
        o = s.tick(p, in, 48000); assert(o.owned && o.evaluated && o.word == 0x11 && o.clk && o.clocked);
        o = s.tick(p, in, 48000); assert(!o.clk && !o.evaluated);
        in.c = bus(0x80); o = s.tick(p, in, 48000); assert(o.word == 0x11);    // no edge: held
        in.clock = 10; o = s.tick(p, in, 48000); assert(o.evaluated && o.word == 0x22 && o.clk);
        o = s.tick(p, in, 48000); assert(!o.clk && !o.evaluated);             // held high: one strobe
        in.clock = 0; s.tick(p, in, 48000);
        // A's own clock means nothing while Switch owns the clock.
        in.a = bus(0x33, true, true, true); in.c = bus(0x00); o = s.tick(p, in, 48000); assert(!o.evaluated && o.word == 0x22);
        in.clock = 10; o = s.tick(p, in, 48000); assert(o.evaluated && o.word == 0x33);
        // Unpatching the jack returns to Follow A.
        in.clockConnected = false; in.a = bus(0x44, true, true, true); o = s.tick(p, in, 48000);
        assert(!o.owned && o.word == 0x44);
    }
    // Internal at 16 Hz and Engine rate.
    for (double sr : {44100., 48000., 96000.}) {
        sw::Switch s; sw::Parameters p; p.source = Source::Internal; p.rateHz = 16; sw::Inputs in; in.a = bus(7);
        int strobes = 0; std::vector<int> edges;
        for (int t = 0; t < int(sr); ++t) { auto o = s.tick(p, in, sr); if (o.clk) { ++strobes; edges.push_back(t); } }
        assert(strobes >= 16 && strobes <= 17);                         // the takeover publication, then 15-16
        for (size_t k = 2; k < edges.size(); ++k) assert(std::abs((edges[k] - edges[k-1]) - sr / 16) <= 1);
        p.source = Source::Engine;
        for (int t = 0; t < 10; ++t) { in.a = bus(uint8_t(t)); auto o = s.tick(p, in, sr); assert(o.word == t && !o.clocked && !o.clk); }
    }
}

// Pipeline (docs/PIPELINE.md): a word published DELAY edges after it entered, bit k a further
// k * SPREAD edges later; LOOP repeats the last DELAY words; RESET clears to 00.
static void pipelining() {
    namespace pl = dspemu::pipeline;
    std::mt19937 rng(21);
    // Every DELAY and SPREAD against a reference history, at Engine rate with a random input.
    for (int delay = 1; delay <= 64; ++delay) for (int spread : {0, 1, 3, 8}) {
        pl::Parameters p; p.mode = ClockMode::Engine; p.delay = delay; p.spread = spread;
        pl::Pipeline line; pl::Inputs in; in.data.connected = true;
        std::vector<int> entered;
        auto first = line.tick(p, in, 48000); assert(first.loaded && !first.advanced && first.word == 0);
        for (int t = 0; t < 300; ++t) {
            in.data.rawWord = uint8_t(rng()); entered.push_back(in.data.rawWord);
            auto o = line.tick(p, in, 48000); assert(o.advanced && !o.clocked);
            int want = 0, n = int(entered.size()) - 1;
            for (int k = 0; k < 8; ++k) { int i = n - delay - k * spread; if (i >= 0 && entered[i] >> k & 1) want |= 1 << k; }
            assert(o.word == want);
        }
    }
    // LOOP repeats the last DELAY words: those already in the delay come out when due, then again
    // every DELAY edges; opening it lets the input through DELAY edges later.
    for (int delay : {1, 3, 16, 64}) {
        pl::Parameters p; p.mode = ClockMode::Engine; p.delay = delay;
        pl::Pipeline line; pl::Inputs in; in.data.connected = true; line.tick(p, in, 48000);
        std::vector<int> out;
        for (int t = 0; t < 200; ++t) { in.data.rawWord = uint8_t(t + 1); out.push_back(line.tick(p, in, 48000).word); }
        p.loop = true;
        for (int t = 0; t < 4 * delay; ++t) { in.data.rawWord = 0xee; out.push_back(line.tick(p, in, 48000).word); }
        for (size_t t = 200; t < out.size(); ++t) assert(out[t] != 0xee && (t < size_t(200 + delay) || out[t] == out[t - delay]));
        assert(out[200] == 200 - delay + 1);                       // the next word that was due
        p.loop = false;
        for (int t = 0; t < delay; ++t) { in.data.rawWord = 0x11; auto w = line.tick(p, in, 48000).word; assert(w == out[out.size() - delay]); out.push_back(w); }
        assert(line.tick(p, in, 48000).word == 0x11);
    }
    // Clocked: the word holds between edges, DELAY changes act on the next edge, RESET clears
    // (and wins over a simultaneous edge), and strobes keep their low gap.
    {
        pl::Parameters p; p.mode = ClockMode::Jack; p.delay = 2; pl::Pipeline line; pl::Inputs in;
        in.clockConnected = true; in.data.connected = true;
        line.tick(p, in, 48000); line.tick(p, in, 48000);
        auto edge = [&](int w) { in.data.rawWord = uint8_t(w); in.clock = 10; auto o = line.tick(p, in, 48000); in.clock = 0; line.tick(p, in, 48000); return o; };
        edge(5); edge(6); assert(edge(7).word == 5);
        p.delay = 1; in.data.rawWord = 99; assert(line.tick(p, in, 48000).word == 5);   // no edge: held
        assert(edge(8).word == 7 && edge(9).word == 8);                                // now one edge late
        in.reset = 10; in.clock = 10; auto o = line.tick(p, in, 48000); assert(o.loaded && o.cleared && o.word == 0 && !o.advanced);
        in.reset = 0; in.clock = 0; line.tick(p, in, 48000);
        assert(edge(1).word == 0 && edge(2).word == 1);
        std::mt19937 r(3); bool last = false;
        for (int t = 0; t < 20000; ++t) {
            in.clock = r() % 2 ? 10.f : 0.f; in.reset = r() % 151 == 0 ? 10.f : 0.f; in.data.rawWord = uint8_t(r());
            auto q = line.tick(p, in, 48000); assert(!(last && q.clk)); last = q.clk;
        }
    }
    // Internal clock: 16 Hz at 44.1/48/96 kHz, one word per edge.
    for (double sr : {44100., 48000., 96000.}) {
        pl::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 16; p.delay = 4;
        pl::Pipeline line; pl::Inputs in; in.data.connected = true; int advances = 0;
        for (int t = 0; t < int(sr * 2); ++t) { in.data.rawWord = uint8_t(t / 100); advances += line.tick(p, in, sr).advanced; }
        assert(advances >= 31 && advances <= 32);
    }
}

// Counter 2 (docs/COUNTER2.md): after n edges the word is floor(256 * frac(n * N / D)), exactly,
// with CARRY on the edges that complete a cycle.
static void ratios() {
    namespace c2 = dspemu::counter2;
    for (int n = 1; n <= 16; ++n) for (int d = 1; d <= 64; ++d) for (int seed : {0, 77}) {
        c2::Parameters p; p.mode = ClockMode::Engine; p.n = n; p.d = d; p.seed = uint8_t(seed);
        c2::Counter2 c; c.clear(uint8_t(seed), d); c2::Inputs in;
        auto first = c.tick(p, in, 48000); assert(first.loaded && first.word == seed && !first.carry);
        int64_t start = int64_t(seed) * d;                       // in units of 1 / (256 d)
        for (int e = 1; e <= 3 * d + 5; ++e) {
            auto o = c.tick(p, in, 48000);
            int64_t now = start + int64_t(256) * n * e, before = now - int64_t(256) * n;
            assert(o.advanced && o.word == (now % (256 * d)) / d);
            assert(o.carry == (now / (256 * d) > before / (256 * d)));
        }
    }
    // Polymeter: 1/12 and 1/16 on one clock carry 4 and 3 times in 48 edges and are both at 00 there.
    {
        c2::Parameters a, b; a.mode = b.mode = ClockMode::Engine; a.d = 12; b.d = 16;
        c2::Counter2 ca, cb; ca.clear(0, 12); cb.clear(0, 16); c2::Inputs in; ca.tick(a, in, 48000); cb.tick(b, in, 48000);
        int carriesA = 0, carriesB = 0; c2::Output oa, ob;
        for (int e = 0; e < 48; ++e) { oa = ca.tick(a, in, 48000); ob = cb.tick(b, in, 48000); carriesA += oa.carry; carriesB += ob.carry; }
        assert(carriesA == 4 && carriesB == 3 && oa.word == 0 && ob.word == 0);
    }
    // A new D keeps the current word; RESET loads SEED and wins over a simultaneous edge.
    {
        c2::Parameters p; p.mode = ClockMode::Jack; p.n = 3; p.d = 16; p.seed = 9;
        c2::Counter2 c; c.clear(0, 16); c2::Inputs in; in.clockConnected = true;
        c.tick(p, in, 48000); c.tick(p, in, 48000);
        auto edge = [&] { in.clock = 10; auto o = c.tick(p, in, 48000); in.clock = 0; c.tick(p, in, 48000); return o; };
        assert(edge().word == 48 && edge().word == 96);
        p.d = 7; auto held = c.tick(p, in, 48000); assert(held.word == 96 && !held.advanced);
        assert(edge().word == (96 * 7 + 256 * 3) % (256 * 7) / 7);
        in.reset = 10; in.clock = 10; auto o = c.tick(p, in, 48000); assert(o.loaded && !o.advanced && o.word == 9);
        in.reset = 0; in.clock = 0; c.tick(p, in, 48000);
        assert(edge().word == (9 * 7 + 768) % (256 * 7) / 7);
        std::mt19937 r(4); bool last = false;
        for (int t = 0; t < 20000; ++t) {
            in.clock = r() % 2 ? 10.f : 0.f; in.reset = r() % 173 == 0 ? 10.f : 0.f; p.d = 1 + int(r() % 64);
            auto q = c.tick(p, in, 48000); assert(!(last && q.clk)); last = q.clk;
        }
    }
    // Internal clock at 44.1/48/96 kHz: 16 Hz with 1/16 carries once a second.
    for (double sr : {44100., 48000., 96000.}) {
        c2::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 16; p.n = 1; p.d = 16;
        c2::Counter2 c; c.clear(0, 16); c2::Inputs in; int carries = 0;
        for (int t = 0; t < int(sr * 3); ++t) carries += c.tick(p, in, sr).carry;
        assert(carries >= 2 && carries <= 3);
    }
}

// Fan (docs/FAN.md): a 5-bit address, counted or taken from BUS IN, routing COM IN to one of 32
// outputs and one of 32 inputs to COM OUT; GLITCH's overlap and dropout on bit-4 changes.
static void fanning() {
    namespace fn = dspemu::fan;
    // step(): every address, length and direction. In range it walks the cycle; out of range it
    // returns to the range; EOC is exactly the wrap.
    for (int length = 1; length <= 32; ++length) for (int a = 0; a < 32; ++a) for (bool down : {false, true}) {
        bool eoc = false; int next = fn::step(uint8_t(a), length, down, eoc);
        int top = length - 1;
        if (!down) {
            int expect = a + 1 > top ? 0 : a + 1;
            assert(next == expect && eoc == (a + 1 > top));
        } else {
            int expect = (a == 0 || a > top) ? top : a - 1;
            assert(next == expect && eoc == (a == 0 || a > top));
        }
        assert(next >= 0 && next <= top);
    }
    // selection(): one channel unless GLITCH is on and bit 4 changed.
    for (int before = 0; before < 32; ++before) for (int after = 0; after < 32; ++after) {
        std::array<int, 2> sel{};
        assert(fn::selection(false, before, after, sel) == 1 && sel[0] == after);
        int n = fn::selection(true, before, after, sel);
        if ((before & 16) == (after & 16)) assert(n == 1 && sel[0] == after);
        else if (after & 16) assert(n == 2 && sel[0] == (after & 15) && sel[1] == after);
        else assert(n == 0);
    }
    // Engine rate counts every sample: the cycle, EOC on the wraps, the DIR gate inverting the switch.
    for (int length : {1, 2, 5, 16, 17, 32}) for (bool down : {false, true}) for (bool gate : {false, true}) {
        fn::Fan f; f.clear(); fn::Parameters p; p.mode = ClockMode::Engine; p.length = length; p.down = down;
        fn::Inputs in; in.dir = gate ? 10.f : 0.f;
        auto o = f.tick(p, in, 48000); assert(o.loaded && o.address == 0 && !o.clocked && !o.eoc);
        bool counting = down != gate; int expect = 0, eocs = 0;
        for (int t = 1; t <= 3 * length; ++t) {
            o = f.tick(p, in, 48000);
            bool wrap = counting ? expect == 0 : expect == length - 1;
            expect = counting ? (expect == 0 ? length - 1 : expect - 1) : (expect + 1) % length;
            assert(o.advanced && o.address == expect && o.eoc == wrap); eocs += o.eoc;
        }
        assert(eocs == 3);
    }
    // Routing at every address: COM IN on exactly one output, the selected input on COM OUT;
    // unpatched COM IN is a 10 V one-hot gate; Hold keeps what each output last carried.
    {
        fn::Fan f; f.clear(); fn::Parameters p; p.mode = ClockMode::Engine; fn::Inputs in;
        for (int ch = 0; ch < 32; ++ch) in.in[ch] = 0.25f * ch - 4;
        f.tick(p, in, 48000);
        for (int t = 1; t <= 64; ++t) {
            auto o = f.tick(p, in, 48000); int a = t % 32;
            assert(o.address == a && o.selectedCount == 1 && o.com == in.in[a]);
            for (int ch = 0; ch < 32; ++ch) assert(o.out[ch] == (ch == a ? 10.f : 0.f));
        }
        p.unselected = fn::Unselected::Hold; in.comConnected = true;
        f.clear(); f.tick(p, in, 48000);
        for (int t = 1; t <= 32; ++t) { in.com = float(t); f.tick(p, in, 48000); }
        in.com = -1; auto o = f.tick(p, in, 48000);              // address 1 again
        assert(o.address == 1 && o.out[1] == -1);
        for (int ch = 0; ch < 32; ++ch) if (ch != 1) assert(o.out[ch] == float(ch == 0 ? 32 : ch));
        p.unselected = fn::Unselected::Zero; o = f.tick(p, in, 48000);
        for (int ch = 0; ch < 32; ++ch) assert(o.out[ch] == (ch == 2 ? -1.f : 0.f));
    }
    // GLITCH: counting up, 16 -> 17 overlaps out 1 with out 17 and 32 -> 1 drops out for one
    // sample; counting down, 1 -> 32 overlaps out 16 with out 32 and 17 -> 16 drops out.
    for (bool down : {false, true}) {
        fn::Fan f; f.clear(); fn::Parameters p; p.mode = ClockMode::Engine; p.glitch = true; p.down = down;
        fn::Inputs in; in.comConnected = true; in.com = 3;
        for (int ch = 0; ch < 32; ++ch) in.in[ch] = float(ch);
        f.tick(p, in, 48000);
        int overlaps = 0, dropouts = 0, prev = 0;
        for (int t = 1; t <= 64; ++t) {
            auto o = f.tick(p, in, 48000); int a = o.address;
            bool rise = !(prev & 16) && (a & 16), fall = (prev & 16) && !(a & 16);
            if (rise) {
                ++overlaps; assert(o.selectedCount == 2 && o.out[a & 15] == 3 && o.out[a] == 3);
                assert(o.com == .5f * ((a & 15) + a));
                assert(down ? (prev == 0 && a == 31) : (prev == 15 && a == 16));
            } else if (fall) {
                ++dropouts; assert(o.selectedCount == 0 && o.com == 0);
                for (float v : o.out) assert(v == 0);
                assert(down ? (prev == 16 && a == 15) : (prev == 31 && a == 0));
            } else {
                assert(o.selectedCount == 1 && o.out[a] == 3 && o.com == a);
            }
            prev = a;
        }
        assert(overlaps == 2 && dropouts == 2);
        p.glitch = false;
        for (int t = 0; t < 64; ++t) assert(f.tick(p, in, 48000).selectedCount == 1);
    }
    // BUS IN owns the address: bits 0-4 of its word, its mode and CLK passed through, RESET
    // ignored; unpatching it publishes the held count.
    {
        auto bus = [](uint8_t word, bool clocked = false, bool clk = false, bool rising = false) {
            bus::Sample s; s.connected = true; s.clocked = clocked; s.clk = clk; s.rising = rising;
            s.rawWord = word; s.word = word; s.updated = !clocked || rising; return s;
        };
        fn::Fan f; f.clear(); fn::Parameters p; p.mode = ClockMode::Engine; fn::Inputs in;
        f.tick(p, in, 48000); f.tick(p, in, 48000); f.tick(p, in, 48000);          // count 2
        for (int w = 0; w < 256; ++w) {
            in.address = bus(uint8_t(w)); auto o = f.tick(p, in, 48000);
            assert(o.following && o.address == (w & 31) && !o.clocked && !o.clk && o.out[w & 31] == 10);
        }
        in.address = bus(0xe7, true, true, true); auto o = f.tick(p, in, 48000);
        assert(o.address == 7 && o.clocked && o.clk);
        in.address = bus(0xe7, true, false, false); o = f.tick(p, in, 48000); assert(o.clocked && !o.clk);
        in.clock.reset = 10; o = f.tick(p, in, 48000); assert(!o.loaded && o.address == 7);
        in.clock.reset = 0; in.address = bus::Sample{};
        o = f.tick(p, in, 48000); assert(!o.following && o.loaded && o.address == 2);
    }
    // RESET loads SEED and publishes it; a seed past LENGTH wraps on the next edge, with EOC.
    {
        fn::Fan f; f.clear(); fn::Parameters p; p.mode = ClockMode::Jack; p.length = 8; p.seed = 20;
        fn::Inputs in; in.clock.clockConnected = true;
        f.tick(p, in, 48000); f.tick(p, in, 48000);
        in.clock.reset = 10; auto o = f.tick(p, in, 48000); assert(o.loaded && o.address == 20 && o.clk);
        in.clock.reset = 0; f.tick(p, in, 48000);
        in.clock.clock = 10; o = f.tick(p, in, 48000); assert(o.advanced && o.eoc && o.address == 0);
        in.clock.clock = 0; f.tick(p, in, 48000);
        p.down = true; in.clock.clock = 10; o = f.tick(p, in, 48000); assert(o.eoc && o.address == 7);
        // Strobes keep their low gap under random clocks, resets and BUS IN changes.
        std::mt19937 r(9); bool last = false;
        for (int t = 0; t < 40000; ++t) {
            in.clock.clock = r() % 2 ? 10.f : 0.f; in.clock.reset = r() % 211 == 0 ? 10.f : 0.f;
            p.length = 1 + int(r() % 32); in.dir = r() % 97 == 0 ? 10.f : 0.f;
            auto q = f.tick(p, in, 48000); assert(!(last && q.clk) && q.address < 32); last = q.clk;
        }
    }
    // The internal clock at 44.1/48/96 kHz: 32 Hz with LENGTH 16 wraps twice a second.
    for (double sr : {44100., 48000., 96000.}) {
        fn::Fan f; f.clear(); fn::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 32; p.length = 16;
        fn::Inputs in; int eocs = 0, edges = 0; std::vector<int> times;
        for (int t = 0; t < int(sr * 2); ++t) { auto o = f.tick(p, in, sr); eocs += o.eoc; if (o.advanced) { ++edges; times.push_back(t); } }
        assert(edges >= 63 && edges <= 64 && eocs == edges / 16);
        for (size_t k = 1; k < times.size(); ++k) assert(std::abs((times[k] - times[k-1]) - sr / 32) <= 1);
    }
}

// Lattice (docs/LATTICE.md): two 4-bit counters on a 4 x 4 grid; jumps against the Bindubba's
// jam wiring settled (Follow) and taken once (Load); V normalled to H's wrap; strobes.
static void latticing() {
    namespace lt = dspemu::lattice;
    // The schematic's jam wiring as bit equations. Main (H): J1 <- own Q3, J2 <- own Q4,
    // J3 <- V.Q1, J4 <- V.Q2. Second (V): J1 <- own Q4, J2 <- own Q3, J3 <- H.Q1, J4 <- H.Q2.
    auto jamH = [](int h, int v) { return (h >> 2 & 1) | (h >> 3 & 1) << 1 | (v & 1) << 2 | (v >> 1 & 1) << 3; };
    auto jamV = [](int h, int v) { return (v >> 3 & 1) | (v >> 2 & 1) << 1 | (h & 1) << 2 | (h >> 1 & 1) << 3; };
    for (int h = 0; h < 16; ++h) for (int v = 0; v < 16; ++v) {
        // A transparent preset held: iterate Q = J until nothing changes.
        int hs = h; for (int k = 0; k < 8; ++k) hs = jamH(hs, v);
        int vs = v; for (int k = 0; k < 8; ++k) vs = jamV(h, vs);
        assert(jamH(hs, v) == hs && jamV(h, vs) == vs);
        assert(lt::followH(uint8_t(v)) == hs && lt::followV(uint8_t(h)) == vs);
        assert(lt::loadH(uint8_t(h), uint8_t(v)) == jamH(h, v) && lt::loadV(uint8_t(h), uint8_t(v)) == jamV(h, v));
        assert(lt::stepOf(uint8_t(lt::followH(uint8_t(v))), uint8_t(v)) % 5 == 0);     // the diagonal
    }
    // One tick from every state under every input combination, against a reference written
    // from the contract: H's edge, then V's edge or H's wrap, then the jumps from what the
    // clocks left, Follow presets holding their counter, Load and BUS IN on rising edges only.
    {
        auto settle = [](auto jam, int self, int other, bool isH) {
            int x = self; for (int k = 0; k < 8; ++k) x = isH ? jam(x, other) : jam(other, x);
            return x;
        };
        long cases = 0;
        for (int state = 0; state < 256; ++state)
        for (int mode = 0; mode < 3; ++mode)            // Follow, Load, BUS IN
        for (int bits = 0; bits < 32; ++bits)           // H edge, V patched, V edge, DIR H, DIR V
        for (int jh = 0; jh < 4; ++jh) for (int jv = 0; jv < 4; ++jv) {   // previous and present levels
            bool hEdge = bits & 1, vPatched = bits & 2, vEdge = bits & 4, dirH = bits & 8, dirV = bits & 16;
            if (!vPatched && vEdge) continue;
            bool jhBefore = jh & 1, jhNow = jh & 2, jvBefore = jv & 1, jvNow = jv & 2;
            const uint8_t busWord = uint8_t(state * 37 + 101);
            lt::Lattice l; l.clear(uint8_t(state));
            lt::Parameters p; p.mode = ClockMode::Jack; p.jump = mode == 1 ? lt::JumpMode::Load : lt::JumpMode::Follow;
            lt::Inputs in; in.clock.clockConnected = true; in.vClockConnected = vPatched;
            in.bus.connected = mode == 2; in.bus.rawWord = busWord;
            l.tick(p, in, 48000); l.tick(p, in, 48000);            // publish, then the low sample
            assert(l.state() == state);
            l.jumpHGate.high = jhBefore; l.jumpVGate.high = jvBefore;
            in.clock.clock = hEdge ? 10.f : 0.f; in.vClock = vEdge ? 10.f : 0.f;
            in.dirH = dirH ? 10.f : 0.f; in.dirV = dirV ? 10.f : 0.f;
            in.jumpH = jhNow ? 10.f : 0.f; in.jumpV = jvNow ? 10.f : 0.f;
            auto o = l.tick(p, in, 48000);
            // Reference.
            int h = state & 15, v = state >> 4;
            bool follow = mode == 0, holdH = jhNow, holdV = jvNow;
            bool riseH = jhNow && !jhBefore, riseV = jvNow && !jvBefore;
            bool hMoved = false, wrapH = false;
            if (hEdge && !(follow && holdH)) {
                wrapH = dirH ? (h & 3) == 0 : (h & 3) == 3; h = (h + (dirH ? 15 : 1)) & 15; hMoved = true;
            }
            bool vStep = vPatched ? vEdge : hMoved && wrapH;
            if (vStep && !(follow && holdV)) v = (v + (dirV ? 15 : 1)) & 15;
            int h0 = h, v0 = v;
            if (mode == 2) {
                if (riseH) h = busWord & 15;
                if (riseV) v = busWord >> 4;
            } else if (mode == 1) {
                if (riseH) h = jamH(h0, v0);
                if (riseV) v = jamV(h0, v0);
            } else {
                bool both = holdH && holdV, now = !both || riseH || riseV;
                if (holdH && now) h = settle(jamH, h0, v0, true);
                if (holdV && now) v = settle(jamV, v0, h0, false);
            }
            assert(o.state == (v << 4 | h) && o.step == 4 * (v & 3) + (h & 3));
            ++cases;
        }
        assert(cases == 256L * 3 * 24 * 16);
    }
    for (int c = 0; c < 16; ++c) for (bool down : {false, true}) {
        bool wrap = false; int next = lt::count(uint8_t(c), down, wrap);
        assert(next == (down ? (c + 15) % 16 : (c + 1) % 16) && wrap == (down ? (c & 3) == 0 : (c & 3) == 3));
    }
    // With only H clocked, V follows H's position wrap: the plain sequence 0 ... 15.
    {
        lt::Lattice l; l.clear(); lt::Parameters p; p.mode = ClockMode::Engine; lt::Inputs in;
        auto o = l.tick(p, in, 48000); assert(o.loaded && o.step == 0 && !o.clocked);
        for (int t = 1; t <= 64; ++t) { o = l.tick(p, in, 48000); assert(o.step == t % 16 && o.state == ((t / 4 % 16) << 4 | t % 16)); }
        // DIR H high: H counts down, its wrap still moves V (up).
        in.dirH = 10; int h = 0, v = 0; l.clear(); l.tick(p, in, 48000);
        for (int t = 1; t <= 64; ++t) {
            bool wrap = (h & 3) == 0; h = (h + 15) % 16; if (wrap) v = (v + 1) % 16;
            o = l.tick(p, in, 48000); assert(o.state == (v << 4 | h));
        }
    }
    // V CLOCK patched: V moves on its own edges only, down while DIR V is high.
    {
        lt::Lattice l; l.clear(); lt::Parameters p; p.mode = ClockMode::Engine; lt::Inputs in; in.vClockConnected = true;
        l.tick(p, in, 48000);
        for (int t = 1; t <= 40; ++t) {
            in.vClock = t % 4 == 0 ? 10.f : 0.f; in.dirV = t > 20 ? 10.f : 0.f;
            auto o = l.tick(p, in, 48000);
            int ups = std::min(t, 20) / 4, downs = t > 20 ? t / 4 - 5 : 0;
            assert((o.state & 15) == t % 16 && (o.state >> 4) == ((ups - downs) % 16 + 16) % 16);
        }
    }
    // Follow: a held JUMP H keeps H on V's position while V clocks, and ignores H's clock; on
    // release H counts on from there. JUMP V likewise; both held freeze what they took.
    {
        lt::Lattice l; l.clear(0x20); lt::Parameters p; p.mode = ClockMode::Jack; lt::Inputs in;
        in.clock.clockConnected = true; in.vClockConnected = true; l.tick(p, in, 48000); l.tick(p, in, 48000);
        auto pulse = [&](float lt::Inputs::* line) { in.*line = 10; auto o = l.tick(p, in, 48000); in.*line = 0; l.tick(p, in, 48000); return o; };
        in.jumpH = 10; auto o = l.tick(p, in, 48000);
        assert(o.jumped && (o.state & 15) == lt::followH(2) && o.step == 10);
        for (int k = 0; k < 6; ++k) {
            in.clock.clock = 10; l.tick(p, in, 48000); in.clock.clock = 0; l.tick(p, in, 48000);   // H's edges: ignored
            o = pulse(&lt::Inputs::vClock);
            assert((o.state & 15) == lt::followH(uint8_t(o.state >> 4)) && o.step % 5 == 0);
        }
        in.jumpH = 0; uint8_t h = l.h; o = pulse(&lt::Inputs::vClock);
        in.clock.clock = 10; o = l.tick(p, in, 48000); in.clock.clock = 0; l.tick(p, in, 48000);
        assert((o.state & 15) == ((h + 1) & 15));
        in.jumpV = 10; o = l.tick(p, in, 48000); assert((o.state >> 4) == lt::followV(uint8_t(o.state & 15)));
        uint8_t hBefore = l.h, vBefore = l.v;
        in.jumpH = 10; o = l.tick(p, in, 48000);
        assert(l.h == lt::followH(vBefore) && l.v == lt::followV(hBefore));
        uint8_t frozen = l.state();
        for (int k = 0; k < 20; ++k) {
            in.clock.clock = k % 2 ? 10.f : 0.f; in.vClock = k % 3 ? 0.f : 10.f;
            assert(l.tick(p, in, 48000).state == frozen);
        }
    }
    // Load: rising edges only, the jam values taken once; clocks keep counting.
    {
        lt::Lattice l; l.clear(0x9b); lt::Parameters p; p.mode = ClockMode::Jack; p.jump = lt::JumpMode::Load; lt::Inputs in;
        in.clock.clockConnected = true; l.tick(p, in, 48000); l.tick(p, in, 48000);
        in.jumpH = 10; auto o = l.tick(p, in, 48000); assert(l.h == lt::loadH(0x0b, 0x09) && l.v == 9);
        for (int k = 0; k < 5; ++k) assert(l.tick(p, in, 48000).state == o.state);                // held: once
        in.clock.clock = 10; o = l.tick(p, in, 48000); assert(o.hAdvanced);                         // still counting
        in.jumpH = 0; in.clock.clock = 0; l.tick(p, in, 48000);
        uint8_t h = l.h, v = l.v; in.jumpV = 10; in.jumpH = 10; l.tick(p, in, 48000);
        assert(l.h == lt::loadH(h, v) && l.v == lt::loadV(h, v));
    }
    // BUS IN: JUMP H loads bits 0-3 of the raw word, JUMP V bits 4-7, on rising edges.
    {
        lt::Lattice l; l.clear(); lt::Parameters p; p.mode = ClockMode::Jack; lt::Inputs in;
        in.bus.connected = true; in.bus.rawWord = 0xc5; l.tick(p, in, 48000);
        in.jumpH = 10; l.tick(p, in, 48000); assert(l.h == 5 && l.v == 0);
        in.bus.rawWord = 0x3a; for (int k = 0; k < 4; ++k) l.tick(p, in, 48000); assert(l.h == 5);
        in.jumpV = 10; l.tick(p, in, 48000); assert(l.v == 3 && l.h == 5);
    }
    // RESET loads SEED (V high nibble, H low) and publishes it; every change is published once,
    // a strobe never follows a strobe, and close changes are deferred, not lost.
    {
        lt::Lattice l; l.clear(); lt::Parameters p; p.mode = ClockMode::Jack; p.seed = 0x6d; lt::Inputs in;
        in.clock.clockConnected = true; in.vClockConnected = true;
        l.tick(p, in, 48000); l.tick(p, in, 48000);
        // RESET wins over a V edge and a jump on its sample; a held Follow jump acts on the next.
        in.clock.reset = 10; in.vClock = 10; in.jumpH = 10;
        auto o = l.tick(p, in, 48000); assert(o.loaded && o.state == 0x6d && o.clk && !o.vAdvanced && !o.jumped);
        in.clock.reset = 0; in.vClock = 0; o = l.tick(p, in, 48000); assert(o.state == (0x60 | lt::followH(6)));
        in.jumpH = 0; l.clear(0x6d); l.tick(p, in, 48000); l.tick(p, in, 48000);
        in.clock.clock = 10; o = l.tick(p, in, 48000); assert(o.clk && o.state == 0x6e);
        in.vClock = 10; o = l.tick(p, in, 48000); assert(!o.clk && o.state == 0x7e);               // deferred
        o = l.tick(p, in, 48000); assert(o.clk && o.state == 0x7e);
        o = l.tick(p, in, 48000); assert(!o.clk);
        std::mt19937 r(5); bool last = false; uint8_t published = l.state();
        for (int t = 0; t < 50000; ++t) {
            in.clock.clock = r() % 3 ? 0.f : 10.f; in.vClock = r() % 4 ? 0.f : 10.f;
            in.jumpH = r() % 50 == 0 ? 10.f : 0.f; in.jumpV = r() % 70 == 0 ? 10.f : 0.f;
            in.clock.reset = r() % 300 == 0 ? 10.f : 0.f; in.dirH = r() % 9 ? 0.f : 10.f;
            p.jump = r() % 2 ? lt::JumpMode::Follow : lt::JumpMode::Load;
            o = l.tick(p, in, 48000); assert(!(last && o.clk)); last = o.clk;
            if (o.clk) published = o.state;
        }
        for (int k = 0; k < 3; ++k) { in = {}; in.clock.clockConnected = true; in.vClockConnected = true; o = l.tick(p, in, 48000); if (o.clk) published = o.state; }
        assert(published == l.state());
    }
    // The internal clock at 44.1/48/96 kHz: 16 Hz steps through all 16 cells once a second.
    for (double sr : {44100., 48000., 96000.}) {
        lt::Lattice l; l.clear(); lt::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 16; lt::Inputs in;
        int edges = 0, strobes = 0;
        for (int t = 0; t < int(sr); ++t) { auto o = l.tick(p, in, sr); edges += o.hAdvanced; strobes += o.clk; }
        assert(edges >= 15 && edges <= 16 && strobes == edges + 1);
    }
    // Slew: the time constant at a knob, halved by each volt of CV, held within 0.5-500 ms; the
    // control's rise is quick, its fall slow.
    assert(lt::Slew::effective(0, 3) == lt::Slew::MIN_TAU && lt::Slew::effective(1, -3) == lt::Slew::MAX_TAU);
    assert(std::abs(lt::Slew::effective(.5, 2) - lt::Slew::tau(.5) / 4) < 1e-12);
    for (double sr : {44100., 48000., 96000.}) {
        for (double knob : {0., .5, 1.}) for (float cv : {-2.f, 0.f, 1.f, 2.f}) {
            lt::Slew s; s.process(0, float(knob), cv, 1 / sr);
            for (int t = 0; t < int(sr * .6); ++t) s.process(0, float(knob), cv, 1 / sr);       // settle the control
            double tau = std::max(lt::Slew::MIN_TAU, std::min(lt::Slew::MAX_TAU, lt::Slew::tau(knob) * std::exp2(-cv)));
            int n = int(std::round(tau * sr)); float y = 0;
            for (int t = 0; t < n; ++t) y = s.process(5, float(knob), cv, 1 / sr);
            assert(n > 0 && std::abs(y - 5 * (1 - std::exp(-n / (tau * sr)))) < .01);
        }
        lt::Slew s; s.process(0, .5f, 0, 1 / sr);
        for (int t = 0; t < int(sr * .02); ++t) s.process(0, .5f, 5, 1 / sr);
        assert(s.control > 4.8);                                  // 20 ms: four rise constants
        for (int t = 0; t < int(sr * .02); ++t) s.process(0, .5f, 0, 1 / sr);
        assert(s.control > 3.2 && s.control < 3.4);              // 20 ms: under half a fall constant
    }
}

static void parallelSumming() {
    namespace ps = dspemu::parallel_sum;
    // The step against a reference written tap by tap, for every word, mask, sense and inject.
    for (int w = 0; w < 256; ++w) for (int m = 0; m < 256; ++m) for (int x = 0; x < 2; ++x) for (int i = 0; i < 2; ++i) {
        int bit = x ^ i;
        for (int k = 0; k < 8; ++k) if (m >> k & 1) bit ^= w >> k & 1;
        assert(ps::step(uint8_t(w), uint8_t(m), x ? ps::Sense::Xnor : ps::Sense::Xor, i) == ((w << 1 | bit) & 255));
    }
    auto period = [](uint8_t mask, ps::Sense sense, uint8_t start) {
        uint8_t w = start; int n = 0;
        do { w = ps::step(w, mask, sense, false); ++n; } while (w != start && n < 1000);
        return w == start ? n : -1;
    };
    // The sixteen masks of period 255, and only those; under XNOR their lockup is FF.
    std::vector<int> maximal;
    for (int m = 0; m < 256; ++m) if (period(uint8_t(m), ps::Sense::Xor, 1) == 255) maximal.push_back(m);
    assert((maximal == std::vector<int>{0x8e, 0x95, 0x96, 0xa6, 0xaf, 0xb1, 0xb2, 0xb4, 0xb8, 0xc3, 0xc6, 0xd4, 0xe1, 0xe7, 0xf3, 0xfa}));
    for (int m : maximal) {
        assert(ps::step(0, uint8_t(m), ps::Sense::Xor, false) == 0 && ps::step(255, uint8_t(m), ps::Sense::Xnor, false) == 255);
        assert(period(uint8_t(m), ps::Sense::Xnor, 0) == 255);
    }
    // The six-stage hardware: masks within 3F. Period 63 in the low six bits for exactly six masks.
    std::vector<int> six;
    for (int m = 1; m < 64; ++m) {
        uint8_t w = 1; int n = 0;
        do { w = ps::step(w, uint8_t(m), ps::Sense::Xor, false); ++n; } while ((w & 63) != 1 && n < 200);
        bool full = n == 63;
        if (full) { std::set<int> seen; uint8_t v = 1; for (int k = 0; k < 63; ++k) { seen.insert(v & 63); v = ps::step(v, uint8_t(m), ps::Sense::Xor, false); } full = seen.size() == 63; }
        if (full) six.push_back(m);
    }
    assert((six == std::vector<int>{0x21, 0x2d, 0x30, 0x33, 0x36, 0x39}));
    // Masks tapping stage 7 are invertible: from 01 under XOR, 27 different cycle lengths, 8 to 255.
    std::set<int> lengths;
    for (int m = 128; m < 256; ++m) { int n = period(uint8_t(m), ps::Sense::Xor, 1); assert(n > 0); lengths.insert(n); }
    assert(lengths.size() == 27 && *lengths.begin() == 8 && *lengths.rbegin() == 255);
    // The hardware's six three-way switches (Q, off, not-Q) and SIGNAL, against MASK and SENSE:
    // for every setting and start, the first six bits match for 100 random edges.
    {
        std::mt19937 r(17);
        for (int setting = 0; setting < 729; ++setting) {
            int sw[6], m = 0, inverted = 0;
            for (int k = 0, c = setting; k < 6; ++k, c /= 3) { sw[k] = c % 3; if (sw[k]) m |= 1 << k; inverted += sw[k] == 2; }
            ps::Sense sense = inverted % 2 ? ps::Sense::Xnor : ps::Sense::Xor;
            for (int start = 0; start < 64; start += 7) {
                int hw = start; uint8_t w = uint8_t(start);
                for (int n = 0; n < 100; ++n) {
                    int signal = r() & 1, d = signal;
                    for (int k = 0; k < 6; ++k) if (sw[k]) d ^= (hw >> k & 1) ^ (sw[k] == 2);
                    hw = (hw << 1 | d) & 63;
                    w = ps::step(w, uint8_t(m), sense, signal);
                    assert((w & 63) == hw);
                }
            }
        }
    }
    // Weights: the detent, W CV at 5 V per unit, clamped.
    assert(ps::weight(.01f, 0) == 0 && ps::weight(-.019f, 0) == 0 && ps::weight(.5f, 0) == .5f);
    assert(ps::weight(.5f, 2.5f) == 1 && ps::weight(.5f, 10) == 1 && ps::weight(-.5f, -10) == -1 && ps::weight(0, -2.5f) == -.5f);
    assert(ps::weight(NAN, 1) == .2f && ps::weight(1, INFINITY) == 1);
    // CV and AUDIO are the weighted sums of the register's serial stream: direct convolution,
    // at Engine rate, random weights and W CV, random INJECT.
    {
        ps::ParallelSum s; s.clear(); ps::Parameters p; p.mode = ClockMode::Engine; p.mask = 0x96; p.sense = ps::Sense::Xor;
        ps::Inputs in; in.injectConnected = true;
        std::mt19937 r(3); std::uniform_real_distribution<float> u(-1, 1);
        std::vector<int> stream(8, 0);
        s.tick(p, in, 48000);
        for (int n = 0; n < 4000; ++n) {
            for (int k = 0; k < 8; ++k) { p.weights[k] = u(r); in.weightCv[k] = 3 * u(r); }
            bool inject = r() & 1; in.inject = inject ? 5.f : -5.f;
            uint8_t before = s.word;
            auto o = s.tick(p, in, 48000);
            assert(o.advanced && o.word == ps::step(before, 0x96, ps::Sense::Xor, inject) && o.inject == inject);
            stream.push_back(o.word & 1);
            double cv = 0, audio = 0;
            for (int k = 0; k < 8; ++k) {
                double w = ps::weight(p.weights[k], in.weightCv[k]);
                int x = stream[stream.size() - 1 - k];
                cv += w * x; audio += w * (2 * x - 1);
            }
            assert(std::abs(o.cv - 1.25 * cv) < 1e-4 && std::abs(o.audio - 1.25 * audio) < 1e-4 && o.gate == (o.word & 1));
        }
    }
    // Levels: all eight at +1 is 10 V on CV and +/-10 V on AUDIO; HOT is 6 V a stage, clipped.
    {
        ps::ParallelSum s; ps::Parameters p; p.mode = ClockMode::Jack; ps::Inputs in; in.clock.clockConnected = true;
        for (int w : {0x00, 0xff, 0x0f, 0x01}) {
            for (bool hot : {false, true}) {
                s.clear(uint8_t(w)); p.hot = hot; auto o = s.tick(p, in, 48000);
                int ones = __builtin_popcount(unsigned(w));
                float v = hot ? 6.f : 1.25f;
                float cv = std::min(hot ? 10.5f : 99.f, v * ones), audio = std::max(hot ? -10.5f : -99.f, std::min(hot ? 10.5f : 99.f, v * (2 * ones - 8)));
                assert(o.word == w && std::abs(o.cv - cv) < 1e-5 && std::abs(o.audio - audio) < 1e-5);
            }
        }
        p.hot = false; p.weights = {1, -1, 1, -1, 1, -1, 1, -1}; s.clear(0x55);
        auto o = s.tick(p, in, 48000); assert(o.cv == 5 && o.audio == 10);
    }
    // INJECT's comparator: threshold and hysteresis; MASK 00 is a delay line for the injected bits,
    // inverted under XNOR; unpatched, nothing is injected.
    {
        ps::Comparator c;
        assert(!c.process(1.2f, 1.2f) && !c.process(1.24f, 1.2f) && c.process(1.26f, 1.2f) && c.process(1.16f, 1.2f) && !c.process(1.14f, 1.2f));
        assert(c.process(-4.9f, -5) && !c.process(-5.06f, -5) && c.process(5.06f, 9) && !c.process(4.94f, 9));   // clamped to +/-5 V
        ps::ParallelSum s; s.clear(); ps::Parameters p; p.mode = ClockMode::Engine; p.mask = 0; ps::Inputs in; in.injectConnected = true;
        s.tick(p, in, 48000);
        const int bits[] = {1, 0, 1, 1, 0, 0, 0, 1, 1, 1};
        for (ps::Sense sense : {ps::Sense::Xor, ps::Sense::Xnor}) {
            p.sense = sense; uint8_t expect = s.word;
            for (int b : bits) {
                in.inject = b ? 3.f : 0.f; auto o = s.tick(p, in, 48000);
                expect = uint8_t(expect << 1 | (b ^ (sense == ps::Sense::Xnor)));
                assert(o.word == expect);
            }
        }
        in.injectConnected = false; in.inject = 10; p.sense = ps::Sense::Xor;
        for (int k = 0; k < 8; ++k) s.tick(p, in, 48000);
        assert(s.word == 0);
    }
    // The MASK bus: its data lines sampled on this module's edge, its CLK the Bus source in Auto.
    {
        ps::ParallelSum s; s.clear(); ps::Parameters p; p.sense = ps::Sense::Xor; p.mask = 0xff; ps::Inputs in;
        in.clock.data.connected = true; in.clock.data.clocked = true; in.clock.data.rawWord = 0x00;
        p.seed = 0x80; in.clock.reset = 10; s.tick(p, in, 48000); in.clock.reset = 0; s.tick(p, in, 48000);
        in.clock.data.rawWord = 0x80; auto o = s.tick(p, in, 48000); assert(!o.advanced && o.mask == 0xb8);
        in.clock.data.rising = true; in.clock.data.clk = true; o = s.tick(p, in, 48000);
        assert(o.advanced && o.mask == 0x80 && o.word == 0x01 && o.clk);
        in.clock.data.rising = false; in.clock.data.rawWord = 0x00; o = s.tick(p, in, 48000);
        assert(!o.advanced && o.mask == 0x80 && !o.clk);
        in.clock.data.connected = false; in.clock.data.clocked = false; o = s.tick(p, in, 48000);
        assert(o.mask == 0xff);
    }
    // RESET loads SEED and publishes it; strobes are one sample with a low sample between, RESET
    // wins over a simultaneous edge, and the published words replay the reference under random
    // clocks, resets and masks.
    {
        ps::ParallelSum s; s.clear(); ps::Parameters p; p.mode = ClockMode::Jack; p.seed = 0x5a; ps::Inputs in;
        in.clock.clockConnected = true; std::mt19937 r(9); bool last = false; uint8_t model = 0; bool clockHigh = false, resetHigh = false;
        int strobes = 0;
        for (int t = 0; t < 60000; ++t) {
            in.clock.clock = r() % 3 ? 0.f : 10.f; in.clock.reset = r() % 400 ? 0.f : 10.f;
            if (r() % 500 == 0) p.mask = uint8_t(r());
            bool clockRise = in.clock.clock > 1 && !clockHigh, resetRise = in.clock.reset > 1 && !resetHigh;
            clockHigh = in.clock.clock > 1; resetHigh = in.clock.reset > 1;
            auto o = s.tick(p, in, 48000);
            assert(!(last && o.clk)); last = o.clk; strobes += o.clk;
            if (resetRise) { model = 0x5a; assert(!o.advanced); }
            if (o.advanced) { assert(clockRise); model = ps::step(model, p.mask, p.sense, false); }
            assert(o.word == model);
        }
        assert(strobes > 5000);
    }
    // The internal clock at 44.1/48/96 kHz.
    for (double sr : {44100., 48000., 96000.}) {
        ps::ParallelSum s; s.clear(); ps::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 100; ps::Inputs in;
        int edges = 0;
        for (int t = 0; t < int(sr); ++t) edges += s.tick(p, in, sr).advanced;
        assert(edges >= 99 && edges <= 100);
    }
}

static void wheeling() {
    namespace wh = dspemu::wheel;
    // FREE: every 24-bit state once in 2^24 steps; without the escape, the same taps are maximal.
    {
        std::vector<bool> seen(1u << 24);
        uint32_t r = 0; std::array<uint32_t, 9> gauss{};
        for (uint32_t n = 0; n < (1u << 24); ++n) {
            assert(!seen[r]); seen[r] = true;
            ++gauss[__builtin_popcount(wh::window(r, 5, 3))];
            r = wh::stepFree(r);
        }
        assert(r == 0);
        // Any eight distinct stages take every byte equally often: G is exactly binomial.
        const uint32_t binomial[] = {1, 8, 28, 56, 70, 56, 28, 8, 1};
        for (int k = 0; k <= 8; ++k) assert(gauss[k] == binomial[k] << 16);
        auto plain = [](uint32_t x) { return (x << 1 | ((x >> 23 ^ x >> 22 ^ x >> 21 ^ x >> 16) & 1)) & wh::ALL; };
        uint32_t x = 1, n = 0;
        do { x = plain(x); ++n; } while (x != 1);
        assert(n == (1u << 24) - 1);
        for (uint32_t y = 1; y < 4096; ++y) assert(((y & 0x7fffff) == 0) || wh::stepFree(y) == plain(y));
        assert(wh::stepFree(0x800000) == 0 && wh::stepFree(0) == 1);
    }
    // The window, for every OFFSET and STRIDE, against stage-by-stage reads.
    {
        std::mt19937 r(4);
        for (int t = 0; t < 200; ++t) {
            uint32_t x = r() & wh::ALL;
            for (int offset = 0; offset < 24; ++offset) for (int stride = 1; stride <= 3; ++stride) {
                uint8_t w = wh::window(x, offset, stride);
                for (int k = 0; k < 8; ++k) assert((w >> k & 1) == (x >> ((offset + stride * k) % 24) & 1));
            }
        }
        assert(wh::dac(0) == -5 && wh::dac(128) == 0 && wh::dac(255) == 127 * 5.f / 128 && wh::gauss(0) == -5 && wh::gauss(255) == 5 && wh::gauss(0x0f) == 0);
    }
    // LOOP: LENGTH L recirculates; a single bit gives LOOP GATE a period of exactly L, and after 24
    // edges the whole register repeats every L. Off continues FREE: one 48-stage line.
    for (int length = 0; length <= 24; ++length) {
        wh::Wheel w; w.clear(0x123456, 1); wh::Parameters p; p.mode = ClockMode::Engine; p.length = length; wh::Inputs in;
        w.tick(p, in, 48000);
        std::vector<uint32_t> loops, frees{w.free};
        std::vector<int> gates;
        for (int t = 0; t < 200; ++t) { auto o = w.tick(p, in, 48000); loops.push_back(o.loop); frees.push_back(o.free); gates.push_back(o.loopGate); }
        if (length) {
            int period = 1; while (period < 200 && !std::equal(gates.begin() + period, gates.end(), gates.begin())) ++period;
            assert(period == length);
            for (size_t t = 24; t + length < loops.size(); ++t) assert(loops[t + length] == loops[t]);
        } else {
            // After t + 1 edges, LOOP stage j holds FREE's stage 23 from j edges earlier: the line's
            // stage 24 + j.
            for (size_t t = 30; t < loops.size(); ++t)
                for (int j = 0; j < 24; ++j) assert((loops[t] >> j & 1) == (frees[t - j] >> 23 & 1));
        }
    }
    // CAPTURE arms; the next acquisition edge shifts FREE, then copies it into LOOP. RESET wins
    // over an armed capture's edge, which then lands on the next acquisition.
    {
        wh::Wheel w; w.clear(); wh::Parameters p; p.mode = ClockMode::Jack; p.seed = 0x3c; wh::Inputs in;
        in.clock.clockConnected = true;
        in.clock.reset = 10; auto o = w.tick(p, in, 48000);
        assert(o.loaded && o.free == 0x3c3c3c && o.loop == 0x3c3c3c && o.loopWord == 0x3c);
        in.clock.reset = 0; w.tick(p, in, 48000);
        for (int k = 0; k < 40; ++k) { in.clock.clock = k % 2 ? 0.f : 10.f; w.tick(p, in, 48000); }
        assert(w.loop != w.free);
        in.capture = 10; o = w.tick(p, in, 48000); assert(o.armed && !o.captured && w.loop != w.free);
        in.capture = 0; for (int k = 0; k < 3; ++k) assert(w.tick(p, in, 48000).armed);
        uint32_t before = w.free;
        in.clock.clock = 10; o = w.tick(p, in, 48000);
        assert(o.captured && !o.armed && o.free == wh::stepFree(before) && o.loop == o.free);
        in.clock.clock = 0; w.tick(p, in, 48000);
        in.clock.clock = 10; o = w.tick(p, in, 48000); uint32_t once = wh::stepFree(before);
        assert(!o.captured && o.free == wh::stepFree(once) && o.loop == wh::stepLoop(once, once, 16));
        in.clock.clock = 0; w.tick(p, in, 48000);
        in.captureButton = true; w.tick(p, in, 48000); in.captureButton = false;
        in.clock.reset = 10; in.clock.clock = 10; o = w.tick(p, in, 48000);
        assert(o.loaded && !o.advanced && o.armed && o.loop == 0x3c3c3c);
        in.clock.reset = 0; in.clock.clock = 0; w.tick(p, in, 48000);
        in.clock.clock = 10; o = w.tick(p, in, 48000);
        assert(o.captured && o.loop == wh::stepFree(0x3c3c3c));
    }
    // RESET replays: the same words after every reset, under random clocks; one-sample strobes,
    // never two in a row.
    {
        wh::Wheel w; w.clear(); wh::Parameters p; p.mode = ClockMode::Jack; p.seed = 0xa7; p.length = 11; p.stride = 2; p.offset = 3;
        wh::Inputs in; in.clock.clockConnected = true;
        std::vector<std::vector<uint8_t>> runs;
        std::mt19937 r(12); bool last = false;
        for (int run = 0; run < 4; ++run) {
            in.clock.reset = 10; in.clock.clock = 0;
            auto o = w.tick(p, in, 48000); assert(!(last && o.clk)); last = o.clk; in.clock.reset = 0;
            std::vector<uint8_t> words;
            for (int t = 0; t < 5000; ++t) {
                in.clock.clock = r() % 3 ? 0.f : 10.f;
                o = w.tick(p, in, 48000); assert(!(last && o.clk)); last = o.clk;
                if (o.advanced) { words.push_back(o.loopWord); words.push_back(o.freeWord); }
            }
            assert(words.size() >= 1200); words.resize(1200); runs.push_back(words);
        }
        for (auto& run : runs) assert(run == runs[0]);
    }
    // The internal clock at 44.1/48/96 kHz.
    for (double sr : {44100., 48000., 96000.}) {
        wh::Wheel w; w.clear(); wh::Parameters p; p.mode = ClockMode::Internal; p.rateHz = 200; wh::Inputs in;
        int edges = 0; for (int t = 0; t < int(sr); ++t) edges += w.tick(p, in, sr).advanced;
        assert(edges >= 199 && edges <= 200);
    }
}

static void rouletting() {
    namespace rl = dspemu::roulette;
    // The count: each DIVIDE rising edge adds one; bit k is the input divided by 2^(k + 1); LO is
    // bits 0-7 and HI bits 4-11.
    {
        rl::Roulette r; r.clear(); rl::Parameters p; rl::Inputs in; in.divideConnected = true;
        std::array<int, 12> toggles{}; unsigned last = 0; int edges = 0;
        for (int t = 0; t < 4 * 8192 + 8; ++t) {
            in.divide = t % 4 < 2 ? 10.f : 0.f;
            auto o = r.tick(p, in, 48000);
            if (o.advanced) {
                ++edges; assert(o.count == unsigned(edges) % 4096 && o.clk);
                for (int k = 0; k < 12; ++k) toggles[k] += ((o.count ^ last) >> k) & 1;
                last = o.count;
            }
            assert(o.lo == (o.count & 255) && o.hi == (o.count >> 4 & 255));
        }
        assert(edges >= 8192);
        for (int k = 0; k < 12; ++k) assert(std::abs(toggles[k] - edges / (1 << k)) <= 1);
    }
    // The resting state: nothing patched, the VCO stays stopped with its output low. Stopped high,
    // the XOR's own output moves it on to low, where it rests. Once running from TRACK, the XOR's
    // own square keeps it going after TRACK is removed when FILTER leaves enough of the
    // capacitor's charge at VCOIN; at FILTER's minimum it falls back to rest. (Predictions; the
    // contract lists them for measurement.)
    {
        rl::Roulette r; r.clear(); rl::Parameters p; rl::Inputs in;
        for (int t = 0; t < 48000; ++t) { auto o = r.tick(p, in, 48000); assert(o.frequency == 0 && !o.advanced && o.vcoOut == -5); }
        r.clear(0, 0, 0, .25); int rises = 0; rl::Output o;
        for (int t = 0; t < 48000; ++t) { o = r.tick(p, in, 48000); rises += o.vcoRise; }
        assert(rises == 0 && o.frequency == 0 && !o.vcoBit && r.phase >= .5);
        for (double filter : {0., 100e3}) {
            rl::Roulette q; q.clear(); p.filterOhms = filter; rl::Inputs track; track.trackConnected = true;
            for (int t = 0; t < 48000; ++t) { track.track = t % 160 < 80 ? 5.f : -5.f; q.tick(p, track, 48000); }
            rises = 0; for (int t = 0; t < 96000; ++t) rises += q.tick(p, in, 48000).vcoRise;
            assert(filter == 0 ? rises == 0 : rises > 500);
        }
    }
    // The spin and the coast: ROULETTE at 10 V for one second, then 0 V. VCOIN follows the closed
    // form of the lead-lag filter, and the VCO stops when the closed form says, at every rate;
    // RESULT publishes the count at rest once.
    for (double filter : {0., 50e3, 100e3}) {
        std::vector<double> stops;
        for (double sr : {44100., 48000., 96000.}) {
            rl::Roulette r; r.clear(); rl::Parameters p; p.filterOhms = filter; rl::Inputs in; in.rouletteConnected = true;
            const double u = 10 / 12., tau = (filter + rl::R2) * rl::C_FARADS, lead = rl::R2 / (filter + rl::R2);
            int n1 = int(sr), stopAt = -1, results = 0, counts = 0; uint8_t resultWord = 0; unsigned countAtStop = 0;
            bool lastResult = false;
            for (int t = 0; t < int(3 * sr); ++t) {
                in.roulette = t < n1 ? 10.f : 0.f;
                auto o = r.tick(p, in, sr);
                double time = t / sr, vC, vcoin;
                if (t < n1) { vC = u * (1 - std::exp(-time / tau)); vcoin = vC + (u - vC) * lead; }
                else { double v1 = u * (1 - std::exp(-1. / tau)); vC = v1 * std::exp(-(time - 1) / tau); vcoin = vC * (1 - lead); }
                assert(std::abs(o.vcoin - vcoin) < 1e-9);
                counts += o.advanced;
                if (o.stopped) { assert(stopAt < 0); stopAt = t; countAtStop = o.count; }
                if (o.resultClk) { assert(!lastResult); ++results; resultWord = o.result; }
                lastResult = o.resultClk;
            }
            assert(stopAt >= n1 && results == 1 && resultWord == (countAtStop & 255) && counts > 1000);
            double v1 = u * (1 - std::exp(-1. / tau));
            double expected = filter == 0 ? 0 : tau * std::log(v1 * (1 - lead) / rl::STOP);
            assert(std::abs((stopAt - n1) / sr - expected) <= 1.5 / sr);
            stops.push_back((stopAt - n1) / sr);
        }
        if (filter == 100e3) assert(stops[0] > 1.15 && stops[0] < 1.2);          // about 1.2 s
    }
    // The filter against a fine integration of the same equations under a continuously varying
    // ROULETTE CV, at 44.1/48/96 kHz.
    for (double sr : {44100., 48000., 96000.}) {
        rl::Roulette r; r.clear(); rl::Parameters p; p.filterOhms = 30e3; p.scale = .1; rl::Inputs in; in.rouletteConnected = true;
        const double rs = 30e3, tau = (rs + rl::R2) * rl::C_FARADS * .1, lead = rl::R2 / (rs + rl::R2);
        double vC = 0, worst = 0; const int fine = 64;
        for (int t = 0; t < int(sr / 2); ++t) {
            double time = t / sr;
            in.roulette = float(6 + 5 * std::sin(2 * M_PI * 7 * time));
            auto o = r.tick(p, in, sr);
            double u = in.roulette / 12.;
            worst = std::max(worst, std::abs(o.vcoin - (vC + (u - vC) * lead)));
            for (int k = 0; k < fine; ++k) {
                double uk = (6 + 5 * std::sin(2 * M_PI * 7 * (time + k / (sr * fine)))) / 12;
                vC += (uk - vC) / tau / (sr * fine);
            }
        }
        assert(worst < 2e-3);
    }
    // The VCO at its fastest: half the engine rate, every count strobed.
    for (double sr : {44100., 48000., 96000.}) {
        rl::Roulette r; r.clear(); rl::Parameters p; p.rangeHz = 20000; p.filterOhms = 0; rl::Inputs in;
        in.rouletteConnected = true; in.roulette = 12; in.rangeCv = 10;
        int counts = 0, strobes = 0; bool last = false;
        for (int t = 0; t < int(sr); ++t) {
            auto o = r.tick(p, in, sr); assert(!(last && o.clk)); last = o.clk;
            if (t > sr / 2) { counts += o.advanced; strobes += o.clk; assert(o.frequency == sr / 2); }
        }
        assert(counts == strobes && std::abs(counts - sr / 4) <= 2);
    }
    // ROULETTE replaces the comparator: TRACK does nothing while it is patched.
    {
        rl::Roulette a, b; a.clear(); b.clear(); rl::Parameters p; rl::Inputs ia, ib;
        ia.rouletteConnected = ib.rouletteConnected = true; ib.trackConnected = true;
        for (int t = 0; t < 48000; ++t) {
            ia.roulette = ib.roulette = t % 9000 < 4000 ? 8.f : 0.f; ib.track = t % 37 < 18 ? 5.f : -5.f;
            auto oa = a.tick(p, ia, 48000), ob = b.tick(p, ib, 48000);
            assert(oa.vcoin == ob.vcoin && oa.count == ob.count && oa.vcoOut == ob.vcoOut);
        }
    }
    // Tracking: both comparators lock the VCO to TRACK's frequency well inside the range; the
    // edge comparator also near its top, where the XOR's ripple loses lock.
    for (double sr : {44100., 48000., 96000.}) for (int cmp = 0; cmp < 2; ++cmp) for (double fin : {50., 300., 1000., 1900.}) {
        if (cmp == 0 && fin == 1900.) continue;
        rl::Roulette r; r.clear(); rl::Parameters p; p.comparator = cmp ? rl::Comparator::Edge : rl::Comparator::Xor;
        rl::Inputs in; in.trackConnected = true; double phase = 0; int rises = 0;
        for (int t = 0; t < int(sr * 3); ++t) {
            phase += fin / sr; if (phase >= 1) phase -= 1;
            in.track = phase < .5 ? 5.f : -5.f;
            auto o = r.tick(p, in, sr);
            if (t >= sr) rises += o.vcoRise;
        }
        assert(std::abs(rises / 2. - fin) <= 1);
    }
    // RANGE CV: one volt an octave on F_MAX after the lag, quick to rise and slow to fall.
    {
        rl::Roulette r; r.clear(); rl::Parameters p; p.rangeHz = 1000; rl::Inputs in;
        in.rangeCv = 2; for (int t = 0; t < 960; ++t) r.tick(p, in, 48000);          // 20 ms: four rise constants
        auto o = r.tick(p, in, 48000); assert(o.fMax > 3900 && o.fMax < 4000);
        for (int t = 0; t < 48000; ++t) o = r.tick(p, in, 48000);
        assert(std::abs(o.fMax - 4000) < 1e-6);
        in.rangeCv = 0; for (int t = 0; t < 960; ++t) o = r.tick(p, in, 48000);
        assert(o.fMax > 2450 && o.fMax < 2600);                                         // 2^(2 e^-0.4) kHz
    }
    // RESET: count 0 and the VCO restarted low, published once; RESET wins over a DIVIDE edge;
    // strobes keep their gaps under random DIVIDE edges and resets.
    {
        rl::Roulette r; r.clear(77); rl::Parameters p; rl::Inputs in; in.divideConnected = true;
        r.tick(p, in, 48000); r.tick(p, in, 48000);
        in.reset = 10; in.divide = 10; auto o = r.tick(p, in, 48000);
        assert(o.loaded && o.count == 0 && !o.advanced && o.clk);
        std::mt19937 g(21); bool last = false; unsigned model = 0; bool divideHigh = true, resetHigh = true;
        for (int t = 0; t < 50000; ++t) {
            in.divide = g() % 3 ? 0.f : 10.f; in.reset = g() % 500 ? 0.f : 10.f;
            bool divideRise = in.divide > 1 && !divideHigh, resetRise = in.reset > 1 && !resetHigh;
            divideHigh = in.divide > 1; resetHigh = in.reset > 1;
            o = r.tick(p, in, 48000); assert(!(last && o.clk)); last = o.clk;
            if (resetRise) { model = 0; assert(!o.advanced); }
            if (o.advanced) { assert(divideRise); model = (model + 1) & 4095; }
            assert(o.count == model);
        }
    }
}

int main() {
    conversions(); reader(); clocking(); reset(); internal(); counting(); runModes(); switching(); pipelining(); ratios(); fanning(); latticing(); parallelSumming(); wheeling(); rouletting();
    std::cout << "PASS: all 256 words; Schmitt decoding; short cables; clock ownership; raw reclocking; frozen clocks; reset publication/priority; engine mode; internal clocks at 44.1/48/96 kHz; Counter: every word/increment/modulus, INC and BUS IN, reclocking, over-modulus words, reset priority, strobe gaps, carries; run modes: every word/increment/modulus/direction in One-shot and Pendulum, end reflections, held tops, RESET direction, mode changes, envelopes at 44.1/48/96 kHz; Switch: Select, Bits and Mix on every (A, B, C), Follow A, CLOCK jack takeover, Internal and Engine clocks; Pipeline: every DELAY with SPREAD, LOOP periods and release, holds, RESET, strobe gaps, internal clocks; Counter 2: every N/D exactly with carries, polymeter realignment, D changes, RESET, strobe gaps, internal clocks; Fan: every address/length/direction, routing and Hold, GLITCH overlaps and dropouts, BUS IN addressing, RESET and seeds past LENGTH, strobe gaps, internal clocks; Lattice: one tick from every state under every input combination against a reference, Follow and Load against the settled and one-pass jam wiring for every state, counting and wraps, V normalled to H, V CLOCK and DIR, held and simultaneous jumps, BUS IN jumps, RESET, deferred strobes, internal clocks, slew time constants and the control lag; Parallel Sum: every word/mask/sense/inject against a tap-by-tap reference, the sixteen maximal masks, the six six-stage masks, 27 cycle lengths, the hardware's 729 switch settings, convolution of the serial stream, HOT clipping, the INJECT comparator, MASK bus reclocking, RESET and strobes under random clocks, internal clocks; Wheel: the full 2^24 cycle visiting every state once and exactly binomial G, the plain taps maximal, every window, every LENGTH's loop period and the 48-stage Off line, capture on the next edge and under RESET, reset replay, strobes, internal clocks; Roulette: counts as exact divisions, the resting state, the spin and coast against the closed form with one RESULT per stop at 44.1/48/96 kHz, the filter against a fine integration, the VCO capped at half the engine rate with every count strobed, ROULETTE replacing the comparator, XOR and edge tracking, RANGE CV and its lag, RESET and strobes under random edges\n";
}
