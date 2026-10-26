// Portable protocol and state-machine tests; no Rack SDK.
#include "../src/engine/Register.hpp"
#include "../src/engine/Counter.hpp"
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

int main() {
    conversions(); reader(); clocking(); reset(); internal(); counting();
    std::cout << "PASS: all 256 words; Schmitt decoding; short cables; clock ownership; raw reclocking; frozen clocks; reset publication/priority; engine mode; internal clocks at 44.1/48/96 kHz; Counter: every word/increment/modulus, INC and BUS IN, reclocking, over-modulus words, reset priority, strobe gaps, carries\n";
}
