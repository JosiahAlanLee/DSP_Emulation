// Portable protocol and state-machine tests; no Rack SDK.
#include "../src/engine/Register.hpp"
#include "../src/engine/Counter.hpp"
#include "../src/engine/Switch.hpp"
#include "../src/engine/Pipeline.hpp"
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

int main() {
    conversions(); reader(); clocking(); reset(); internal(); counting(); runModes(); switching(); pipelining();
    std::cout << "PASS: all 256 words; Schmitt decoding; short cables; clock ownership; raw reclocking; frozen clocks; reset publication/priority; engine mode; internal clocks at 44.1/48/96 kHz; Counter: every word/increment/modulus, INC and BUS IN, reclocking, over-modulus words, reset priority, strobe gaps, carries; run modes: every word/increment/modulus/direction in One-shot and Pendulum, end reflections, held tops, RESET direction, mode changes, envelopes at 44.1/48/96 kHz; Switch: Select, Bits and Mix on every (A, B, C), Follow A, CLOCK jack takeover, Internal and Engine clocks; Pipeline: every DELAY with SPREAD, LOOP periods and release, holds, RESET, strobe gaps, internal clocks\n";
}
