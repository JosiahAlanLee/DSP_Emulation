// SPDX-License-Identifier: BSD-3-Clause
#include "../src/engine/Percussion.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

static bool prohibitAllocations = false;
void* operator new(std::size_t n) {
    assert(!prohibitAllocations);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace drumlab;

static void soundsAndDecay() {
    for (double sr : {44100., 48000., 96000.}) for (int m = 0; m < machines; ++m) {
        Engine e; Hit p; p.machine = Machine(m); p.decay = .12;
        e.trigger(0, p, sr);
        double energy = 0, late = 0;
        for (int i = 0; i < int(sr * .4); ++i) {
            double out = e.tick(0, sr);
            assert(std::isfinite(out) && std::abs(out) <= 1.01);
            energy += out * out;
            if (i > int(sr * .3)) late += out * out;
        }
        assert(energy / sr > .00001 && late == 0);
        assert(!e.lane[0].voice.active);
        for (int c = 1; c < channels; ++c) assert(e.tick(c, sr) == 0);
    }
    // Zero color/contour gives a simple kick whose settled fundamental is 55 Hz.
    Engine kick; Hit p; p.color = p.contour = p.drive = 0; p.decay = 4;
    kick.trigger(0, p, 48000);
    int crossings = 0; double prev = 0;
    for (int i = 0; i < 48000; ++i) {
        double x = kick.tick(0, 48000);
        if (i >= 4800 && prev <= 0 && x > 0) ++crossings;
        prev = x;
    }
    assert(crossings >= 49 && crossings <= 50); // 0.9 s * 55 Hz
}

static void captureAndReset() {
    for (int m = 0; m < machines; ++m) {
        Engine a, b; Hit source; source.machine = Machine(m);
        a.trigger(0, source, 48000); b.trigger(0, source, 48000);
        source.pitch = 4; source.color = 0; source.decay = .005; source.bits = 4;
        for (int i = 0; i < 800; ++i) assert(a.tick(0, 48000) == b.tick(0, 48000));
        // A retrigger captures new values but keeps the outgoing hit unchanged.
        a.trigger(0, source, 48000);
        assert(a.lane[0].voice.hit.pitch == 4 && a.lane[0].tail.hit.pitch == 0);
        assert(a.lane[0].tail.hit.bits == 16 && a.lane[0].voice.hit.bits == 4);
        double first = a.tick(0, 48000);
        assert(first == b.tick(0, 48000)); // crossfade begins at the old voice
    }
    Engine e; Hit p; p.machine = Machine::Snare;
    std::vector<double> trace;
    for (int run = 0; run < 2; ++run) {
        e.reset();
        for (int i = 0; i < 6000; ++i) {
            if (i % 600 == 0) e.trigger(3, p, 48000);
            double x = e.tick(3, 48000);
            if (!run) trace.push_back(x); else assert(trace[i] == x);
        }
    }
    e.reset(); p.velocity = 0; e.trigger(0, p, 48000);
    assert(e.lane[0].hits == 0 && e.tick(0, 48000) == 0);
    p.velocity = 1; e.trigger(0, p, 48000);
    for (int i = 0; i < 600; ++i) e.tick(0, 48000);
    e.choke(0);
    for (int i = 0; i < 100; ++i) e.tick(0, 48000);
    assert(e.tick(0, 48000) == 0);
    e.clear(3); assert(e.tick(3, 48000) == 0);
}

static void crunchAndVelocity() {
    Voice a, b, quiet; Hit p; p.machine = Machine::FM; p.decay = .8; p.reduction = 7; p.bits = 4;
    a.start(p, 123, 48000); b.start(p, 123, 48000);
    p.velocity = .25; quiet.start(p, 123, 48000);
    double prev = 0;
    for (int i = 0; i < 4096; ++i) {
        double x = a.tick(48000);
        assert(x == b.tick(48000));
        assert(std::abs(quiet.tick(48000) - x * .25) < 1e-12);
        assert(std::abs(x * 8 - std::round(x * 8)) < 1e-10);
        if (i % 7) assert(x == prev);
        prev = x;
    }
}

static void boundsAndRealtime() {
    Engine e;
    Hit p;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    p.pitch = nan; p.decay = -1; p.color = nan; p.contour = 50;
    p.drive = 99; p.bits = nan; p.reduction = 1e30; p.velocity = 1;
    p.machine = Machine(999);
    e.trigger(0, p, nan);
    for (int i = 0; i < 10000; ++i) assert(std::isfinite(e.tick(0, nan)));
    Edge edge;
    assert(edge.process(10)); assert(!edge.process(10)); assert(!edge.process(1));
    assert(!edge.process(nan)); assert(edge.process(10));
    uint32_t rng = 123;
    prohibitAllocations = true;
    for (int i = 0; i < 120000; ++i) {
        rng = hash(rng);
        int c = int(rng % channels);
        if (i % 17 == 0) {
            p.machine = Machine(rng % machines);
            p.pitch = int(rng % 100) / 12. - 4;
            p.color = double(rng % 101) / 100;
            p.decay = i % 2 ? .005 : 4;
            p.reduction = 1 + rng % 64;
            e.trigger(c, p, i % 3 ? 8000 : 384000);
        }
        double out = e.tick(c, i % 3 ? 8000 : 384000);
        assert(std::isfinite(out) && std::abs(out) <= 1.01);
        if (i % 701 == 0) e.choke(c);
        if (i % 12001 == 0) e.reset();
    }
    prohibitAllocations = false;
}

int main() {
    soundsAndDecay(); captureAndReset(); crunchAndVelocity(); boundsAndRealtime();
    std::cout << "PASS: six audible models, pitch/decay, captured hits/tails, reset replay, choke, crunch, velocity, finite bounds, no engine allocation\n";
}
