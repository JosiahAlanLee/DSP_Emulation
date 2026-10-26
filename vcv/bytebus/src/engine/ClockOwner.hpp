// SPDX-License-Identifier: BSD-3-Clause
// Clock ownership and word-clock strobes for latching byte-bus modules: which clock
// a module follows, its RESET/LOAD handling and the one-sample strobes it publishes.
// The behaviour is the Clocked Register's (docs/MANUAL.md, "Clock ownership" and
// "Clock and reset timing"); a module adds its own word and what an edge does to it.
#pragma once
#include "../../../shared/ByteBus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dspemu::clock_owner {
enum class ClockMode { Auto, Jack, Bus, Internal, Engine };
enum class ClockSource { Jack, Bus, Internal, Engine };

struct Inputs {
    bus::Sample data;
    bool clockConnected = false;
    float clock = 0, reset = 0;
    bool resetButton = false;
};
// What the owning module does on this sample. seed and acquire are never both set.
struct Step {
    ClockSource source = ClockSource::Internal;
    bool clocked = true, clk = false;
    bool seed = false;     // RESET or LOAD: load the seed word now
    bool loaded = false;   // the seed word is published on this sample
    bool acquire = false;  // an acquisition edge: update the word on this sample
};

struct ClockOwner {
    bus::Gate clockGate, resetGate;
    bool resetButtonHigh = false, strobe = false, pendingLoad = true;
    ClockSource source = ClockSource::Internal;
    double phase = 0;

    // A newly loaded word is published next, and the internal clock restarts.
    void restart() {
        phase = 0; pendingLoad = true;
    }
    void clear() {
        restart(); strobe = false;
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

    Step tick(ClockMode mode, double rateHz, const Inputs& in, double sampleRate) {
        // Track every input even when it does not currently own the clock.
        bool jackEdge = clockGate.process(in.clockConnected ? in.clock : 0.f);
        bool resetEdge = resetGate.process(in.reset);
        resetEdge |= in.resetButton && !resetButtonHigh;
        resetButtonHigh = in.resetButton;
        ClockSource next = select(mode, in);
        if (next != source) { source = next; phase = 0; }
        bool edge = false;
        switch (source) {
            case ClockSource::Jack: edge = jackEdge; break;
            case ClockSource::Bus: edge = in.data.connected && in.data.clocked && in.data.rising; break;
            case ClockSource::Engine: edge = true; break;
            case ClockSource::Internal: {
                double sr = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000.;
                double hz = std::isfinite(rateHz) ? rateHz : 8.;
                hz = std::max(0., std::min(hz, sr * .5));
                phase += hz / sr;
                if (phase >= 1.) { phase -= std::floor(phase); edge = true; }
                break;
            }
        }
        Step out;
        if (resetEdge) { restart(); out.seed = true; }
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
            if (edge) { strobe = out.clocked; out.acquire = true; }
        }
        out.clk = strobe;
        return out;
    }
};
} // namespace dspemu::clock_owner
