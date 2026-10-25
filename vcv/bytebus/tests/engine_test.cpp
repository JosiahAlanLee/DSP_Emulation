// Portable protocol and state-machine tests; no Rack SDK.
#include "../src/engine/Register.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
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

int main() {
    conversions(); reader(); clocking(); reset(); internal();
    std::cout << "PASS: all 256 words; Schmitt decoding; short cables; clock ownership; raw reclocking; frozen clocks; reset publication/priority; engine mode; internal clocks at 44.1/48/96 kHz\n";
}
