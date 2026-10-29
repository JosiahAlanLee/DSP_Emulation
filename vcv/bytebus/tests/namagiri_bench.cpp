// SPDX-License-Identifier: BSD-3-Clause
// Bounded benchmark for Namagiri's per-sample table rebuild under continuous
// modulation. A 1 Hz ANGLE LFO (depth 32, radius .5, CLOCK jack patched and
// idle) changes q past the rebuild tolerance on every 48 kHz sample, the worst
// case the module supports. It reports CPU seconds per audio second; there is
// deliberately no timing assertion, since the number is machine-dependent.
// Before the O(depth) convergent recurrence this took about 1.7 CPU seconds per
// audio second; after it, a few percent of one core (see docs/NAMAGIRI.md).
#include "../src/engine/Namagiri.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <ctime>

int main() {
    using namespace dspemu::namagiri;
    constexpr double SR = 48000, AUDIO_SECONDS = 1;
    constexpr int SAMPLES = int(SR * AUDIO_SECONDS), RUNS = 3;
    Parameters p;
    p.mode = ClockMode::Jack; p.radius = .5; p.depth = MAX_DEPTH;
    Inputs in; in.clock.clockConnected = true;
    double best = 0, checksum = 0;
    for (int run = 0; run < RUNS; ++run) {
        Namagiri m;
        double sum = 0;
        std::clock_t start = std::clock();
        for (int i = 0; i < SAMPLES; ++i) {
            p.turns = .2 + .1 * std::sin(TAU * i / SR);     // 1 Hz, +/-0.1 turn
            sum += m.tick(p, in, SR).c.real;
        }
        double cpu = double(std::clock() - start) / CLOCKS_PER_SEC;
        if (run == 0 || cpu < best) best = cpu;
        if (run == 0) checksum = sum;
        else assert(sum == checksum);                       // deterministic, no timing assertion
        std::printf("run %d: %.4f CPU s per %.0f audio s at %.0f Hz (%.1f%% of one core)\n",
            run + 1, cpu, AUDIO_SECONDS, SR, 100 * cpu / AUDIO_SECONDS);
    }
    std::printf("best: %.4f CPU s per audio second, %.1fx real time, checksum %.6f\n",
        best, AUDIO_SECONDS / best, checksum);
    std::printf("PASS: Namagiri continuous-modulation benchmark, DEPTH %d, radius .5, 1 Hz ANGLE LFO\n",
        MAX_DEPTH);
}
