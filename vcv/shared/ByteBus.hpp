// SPDX-License-Identifier: BSD-3-Clause
// Byte-bus primitives. No Rack dependency, allocation, or implicit mono broadcast.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace dspemu::bus {
constexpr int BITS = 8, CLK = 8;
constexpr float HIGH = 10.f, V_HIGH = 1.f, V_LOW = .1f;
constexpr float FULL_SCALE_VOLTS = 5.f;

inline float bipolar(uint8_t n) { return (int(n) - 128) * (1.f / 128); }
inline float phase(uint8_t n) { return n * (1.f / 256); }
inline float level(uint8_t n) { return n * (1.f / 255); }
inline float toVolts(uint8_t n) { return bipolar(n) * FULL_SCALE_VOLTS; }
inline uint8_t fromBipolar(float s) {
    if (!std::isfinite(s)) return 128;
    if (s <= -1) return 0;
    if (s >= 127.f / 128) return 255;
    return uint8_t(std::floor(128.f + 128.f * s + .5f));
}
inline uint8_t fromVolts(float v) { return fromBipolar(v / FULL_SCALE_VOLTS); }

struct Gate {
    bool high = false;
    bool process(float voltage) {
        bool before = high;
        if (!std::isfinite(voltage) || voltage <= V_LOW) high = false;
        else if (voltage >= V_HIGH) high = true;
        return high && !before;
    }
    void reset() { high = false; }
};

struct Sample {
    uint8_t rawWord = 0; // Current Schmitt-decoded data, even between bus clock edges.
    uint8_t word = 0;    // Held on clocked buses; current data on unclocked buses.
    bool connected = false, clocked = false, clk = false, rising = false;
    bool updated = false; // Input word sampled; NOT permission to advance a register.
};

struct Reader {
    std::array<Gate, 9> lines{};
    uint8_t word = 0;
    bool connected = false, clocked = false;

    void reset() {
        for (auto& line : lines) line.reset();
        word = 0; connected = clocked = false;
    }

    // Input needs getChannels() and getVoltage(channel), as Rack's Input does.
    // A new connection or a change between 8/9-channel modes starts fresh:
    // an initially high CLK counts as a rising edge.
    template <class Input> Sample process(Input& input) {
        int channels = input.getChannels();
        bool nextConnected = channels > 0, nextClocked = channels >= 9;
        if (!nextConnected || nextConnected != connected || nextClocked != clocked) reset();
        connected = nextConnected; clocked = nextClocked;
        Sample s;
        s.connected = connected; s.clocked = clocked;
        for (int k = 0; k < BITS; ++k) {
            lines[k].process(k < channels ? input.getVoltage(k) : 0.f);
            if (lines[k].high) s.rawWord |= uint8_t(1u << k);
        }
        s.rising = lines[CLK].process(clocked ? input.getVoltage(CLK) : 0.f);
        s.clk = lines[CLK].high;
        s.updated = connected && (!clocked || s.rising);
        if (s.updated) word = s.rawWord;
        s.word = word;
        return s;
    }
};

struct Writer {
    // Rack keeps a physically disconnected Output at zero channels. A connected
    // Output is normalized to 8 or 9, never the width of a short input cable.
    template <class Output>
    void write(Output& out, uint8_t word, bool clocked, bool clk) const {
        out.setChannels(clocked ? 9 : 8);
        for (int k = 0; k < BITS; ++k) out.setVoltage(word & (1u << k) ? HIGH : 0.f, k);
        out.setVoltage(clocked && clk ? HIGH : 0.f, CLK);
    }
};
} // namespace dspemu::bus
