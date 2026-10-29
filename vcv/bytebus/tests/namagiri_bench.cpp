// SPDX-License-Identifier: BSD-3-Clause
// Bounded benchmark for Namagiri's per-sample table rebuild under continuous
// modulation. A 1 Hz ANGLE LFO (depth 32, radius .5 then the maximum .97, CLOCK
// jack patched and idle) changes q past the rebuild tolerance on every 48 kHz
// sample, the worst case the module supports. It reports CPU seconds per audio
// second; there is deliberately no timing assertion, since the number is
// machine-dependent. Before the O(depth) convergent recurrence this took about
// 1.7 CPU seconds per audio second at radius .5; after it, a few percent of one
// core at both radii (see docs/NAMAGIRI.md).
#include "../src/engine/Namagiri.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <ctime>

int main() {
    using namespace dspemu::namagiri;
    constexpr double SR = 48000, AUDIO_SECONDS = 1;
    constexpr int SAMPLES = int(SR * AUDIO_SECONDS), RUNS = 3;
    Parameters p;
    p.mode = ClockMode::Jack; p.depth = MAX_DEPTH;
    Inputs in; in.clock.clockConnected = true;
    for (double radius : {.5, MAX_RADIUS}) {
        p.radius = radius;
        double best = 0, checksum = 0;
        for (int run = 0; run < RUNS; ++run) {
            Namagiri m;
            double sum = 0;
            std::clock_t start = std::clock();
            for (int i = 0; i < SAMPLES; ++i) {
                p.turns = .2 + .1 * std::sin(TAU * i / SR);   // 1 Hz, +/-0.1 turn
                sum += m.tick(p, in, SR).c.real;
            }
            double cpu = double(std::clock() - start) / CLOCKS_PER_SEC;
            if (run == 0 || cpu < best) best = cpu;
            if (run == 0) checksum = sum;
            else assert(sum == checksum);                     // deterministic, no timing assertion
            std::printf("radius %.2f run %d: %.4f CPU s per %.0f audio s at %.0f Hz (%.1f%% of one core)\n",
                radius, run + 1, cpu, AUDIO_SECONDS, SR, 100 * cpu / AUDIO_SECONDS);
        }
        std::printf("radius %.2f best: %.4f CPU s per audio second, %.1fx real time, checksum %.6f\n",
            radius, best, AUDIO_SECONDS / best, checksum);
    }
    std::printf("PASS: Namagiri continuous-modulation benchmark, DEPTH %d, radius .5 and %.2f, 1 Hz ANGLE LFO\n",
        MAX_DEPTH, MAX_RADIUS);
}
