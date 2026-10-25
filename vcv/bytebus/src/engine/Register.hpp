// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "../../../shared/ByteBus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dspemu::byte_register {
enum class ClockMode { Auto, Jack, Bus, Internal, Engine };
enum class ClockSource { Jack, Bus, Internal, Engine };

struct Parameters {
    ClockMode mode = ClockMode::Auto;
    double rateHz = 8;
    uint8_t resetWord = 0;
};
struct Inputs {
    bus::Sample data;
    bool clockConnected = false;
    float clock = 0, reset = 0;
    bool resetButton = false;
};
struct Output {
    uint8_t word = 0;
    bool clocked = true, clk = false, captured = false, loaded = false;
    ClockSource source = ClockSource::Internal;
};

// One word of state. Raw data is sampled on THIS register's selected edge;
// upstream CLK is never an additional latch when a local clock is selected.
struct Register {
    uint8_t word = 0;
    bus::Gate clockGate, resetGate;
    bool resetButtonHigh = false, strobe = false, pendingLoad = true;
    ClockSource source = ClockSource::Internal;
    double phase = 0;

    void load(uint8_t value) {
        word = value; phase = 0; pendingLoad = true;
    }
    void clear(uint8_t value = 0) {
        word = value; phase = 0; pendingLoad = true; strobe = false;
        clockGate.reset(); resetGate.reset(); resetButtonHigh = false;
        source = ClockSource::Internal;
    }
    static ClockSource select(ClockMode mode, const Inputs& in) {
        switch (mode) {
            case ClockMode::Jack: return ClockSource::Jack;
            case ClockMode::Bus: return ClockSource::Bus;
            case ClockMode::Internal: return ClockSource::Internal;
            case ClockMode::Engine: return ClockSource::Engine;
            default: return in.clockConnected ? ClockSource::Jack :
                in.data.clocked ? ClockSource::Bus : ClockSource::Internal;
        }
    }

    Output tick(const Parameters& p, const Inputs& in, double sampleRate) {
        // Track every input even when it does not currently own the clock.
        bool jackEdge = clockGate.process(in.clockConnected ? in.clock : 0.f);
        bool resetEdge = resetGate.process(in.reset);
        resetEdge |= in.resetButton && !resetButtonHigh;
        resetButtonHigh = in.resetButton;
        ClockSource next = select(p.mode, in);
        if (next != source) { source = next; phase = 0; }
        bool edge = false;
        switch (source) {
            case ClockSource::Jack: edge = jackEdge; break;
            case ClockSource::Bus: edge = in.data.connected && in.data.clocked && in.data.rising; break;
            case ClockSource::Engine: edge = true; break;
            case ClockSource::Internal: {
                double sr = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000.;
                double hz = std::isfinite(p.rateHz) ? p.rateHz : 8.;
                hz = std::max(0., std::min(hz, sr * .5));
                phase += hz / sr;
                if (phase >= 1.) { phase -= std::floor(phase); edge = true; }
                break;
            }
        }
        if (resetEdge) load(p.resetWord);
        Output out;
        out.source = source; out.clocked = source != ClockSource::Engine;
        if (pendingLoad) {
            // A load publishes the seed, rather than immediately overwriting it
            // with feedback. If CLK was high, insert a low sample before publishing.
            if (!out.clocked || !strobe) {
                strobe = out.clocked; pendingLoad = false; out.loaded = true;
            } else strobe = false;
        } else if (out.clocked && strobe) {
            strobe = false; // Mandatory low sample. RESET wins over nearby edges.
        } else {
            strobe = false;
            if (edge) {
                if (in.data.connected) word = in.data.rawWord;
                strobe = out.clocked; out.captured = true;
            }
        }
        out.word = word; out.clk = strobe;
        return out;
    }
};
} // namespace dspemu::byte_register
