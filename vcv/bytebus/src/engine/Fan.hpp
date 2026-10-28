// SPDX-License-Identifier: BSD-3-Clause
// A 5-bit address routes COM IN to one of 32 outputs and one of 32 inputs to COM OUT, both at once,
// on two 16-channel cables per direction. The address is an internal up/down counter of LENGTH
// steps (ClockOwner.hpp) or, with BUS IN patched, bits 0-4 of that bus (transparent). GLITCH models
// the original's late inverted enable on the first multiplexer. Contract: docs/FAN.md.
#pragma once
#include "ClockOwner.hpp"
#include <array>

namespace dspemu::fan {
using clock_owner::ClockMode;
using clock_owner::ClockSource;

constexpr int WAYS = 32, HALF = 16;

enum class Unselected { Zero, Hold };

struct Parameters {
    ClockMode mode = ClockMode::Auto;   // Auto, Jack, Internal or Engine; Bus is never selected
    double rateHz = 8;
    int length = 32;                    // 1-32
    bool down = false;                  // the DIR switch; the DIR gate inverts it
    uint8_t seed = 0;                   // 0-31
    Unselected unselected = Unselected::Zero;
    bool glitch = false;
};
struct Inputs {
    clock_owner::Inputs clock;          // CLOCK, RESET, LOAD; its bus sample stays unconnected
    bus::Sample address;                // BUS IN
    float dir = 0;
    bool comConnected = false;
    float com = 0;                      // COM IN; 10 V while unpatched
    std::array<float, WAYS> in{};       // IN 1-16 then IN 17-32; missing channels are 0 V
};
struct Output {
    uint8_t address = 0;
    bool clocked = true, clk = false, eoc = false, loaded = false, advanced = false;
    bool following = false;             // BUS IN owns the address
    int selectedCount = 1;              // 0 in a GLITCH dropout, 2 in a GLITCH overlap
    std::array<int, 2> selected{};
    std::array<float, WAYS> out{};
    float com = 0;
};

// Which channels are connected on a sample where the address moved from previous to address.
// Without GLITCH, or when bit 4 did not change, only the address. With GLITCH, the first
// multiplexer's enable is late: a rising bit 4 leaves it on at the new low four bits (an
// overlap with the second), a falling bit 4 leaves neither on (a dropout).
inline int selection(bool glitch, int previous, int address, std::array<int, 2>& selected) {
    bool before = previous & HALF, after = address & HALF;
    if (!glitch || before == after) { selected[0] = address; return 1; }
    if (after) { selected[0] = address & (HALF - 1); selected[1] = address; return 2; }
    return 0;
}

// One counting edge. An address outside 0 ... length - 1 returns to the range on the next
// edge. EOC marks a wrap: up into 0, down into length - 1.
inline uint8_t step(uint8_t address, int length, bool down, bool& eoc) {
    int top = length - 1;
    if (down) {
        eoc = address == 0 || address > top;
        return uint8_t(eoc ? top : address - 1);
    }
    eoc = address + 1 > top;
    return uint8_t(eoc ? 0 : address + 1);
}

struct Fan {
    clock_owner::ClockOwner owner;
    bus::Gate dirGate;
    uint8_t count = 0;                  // the internal counter
    uint8_t previous = 0;               // the address on the previous sample
    bool owned = true;                  // the internal counter owns the address
    bool lastClk = false;               // BUS OUT's CLK on the previous sample
    bool deferred = false;              // a followed strobe moved past a low sample
    std::array<float, WAYS> held{};

    void clear(uint8_t value = 0) {
        owner.clear(); dirGate.reset();
        count = previous = uint8_t(value & (WAYS - 1)); owned = true; held.fill(0);
        lastClk = deferred = false;
    }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        Output out;
        int length = std::max(1, std::min(WAYS, p.length));
        dirGate.process(in.dir);
        uint8_t address;
        if (in.address.connected) {
            // Transparent: the bus's mode and CLK level pass through; RESET and LOAD wait.
            owned = false; out.following = true;
            address = uint8_t(in.address.word & (WAYS - 1));
            out.clocked = in.address.clocked;
            out.clk = in.address.clocked && in.address.clk;
            // A bus edge right after a high CLK (taking over from Fan's own strobe) would publish a
            // new word under a CLK that never fell: insert the low sample, then the strobe.
            if (out.clocked && in.address.rising && lastClk) { out.clk = false; deferred = true; }
            else if (deferred) { out.clk = out.clocked; deferred = false; }
        } else {
            // Taking the address back publishes the held count first, as a load does: after a low
            // sample if the followed CLK was high.
            if (!owned) { owner.clear(); owner.strobe = lastClk; owned = true; }
            deferred = false;
            clock_owner::Inputs clock = in.clock;
            clock.data = bus::Sample{};
            auto s = owner.tick(p.mode, p.rateHz, clock, sampleRate);
            if (s.seed) count = uint8_t(p.seed & (WAYS - 1));
            if (s.acquire) count = step(count, length, p.down != dirGate.high, out.eoc);
            address = count;
            out.clocked = s.clocked; out.clk = s.clk; out.loaded = s.loaded; out.advanced = s.acquire;
        }
        out.address = address;
        lastClk = out.clocked && out.clk;
        out.selectedCount = selection(p.glitch, previous, address, out.selected);
        previous = address;
        float com = in.comConnected ? in.com : bus::HIGH;
        bool selectedNow[WAYS] = {};
        for (int k = 0; k < out.selectedCount; ++k) selectedNow[out.selected[k]] = true;
        for (int ch = 0; ch < WAYS; ++ch) {
            if (selectedNow[ch]) held[ch] = com;
            out.out[ch] = selectedNow[ch] ? com : p.unselected == Unselected::Hold ? held[ch] : 0.f;
        }
        // The two 470 ohm paths of an overlap join their inputs: COM OUT is the mean.
        if (out.selectedCount == 1) out.com = in.in[out.selected[0]];
        else if (out.selectedCount == 2) out.com = .5f * (in.in[out.selected[0]] + in.in[out.selected[1]]);
        return out;
    }
};
} // namespace dspemu::fan
