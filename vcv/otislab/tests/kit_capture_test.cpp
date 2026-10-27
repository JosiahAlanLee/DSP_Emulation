// Otis Kit's takes (docs/CAPTURE.md), without Rack: the free-memory allocator, takes and their
// generations, the guarantee that recording never writes over a take that is current, still sounding
// or being saved, and the lock-free hand-over of takes to a saving thread (run under TSan too).
#include "../src/engine/Kit.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <thread>

using namespace otis;
using kit::Region;

// The longest uncovered stretch, by marking every frame.
static Region bruteGap(const std::vector<Region>& used) {
    std::vector<bool> taken(memoryWords, false);
    for (auto r : used) for (int32_t k = std::max(0, r.begin); k < std::min(r.end, int32_t(memoryWords)); ++k) taken[size_t(k)] = true;
    Region best{}; int32_t run = 0;
    for (int32_t k = 0; k <= int32_t(memoryWords); ++k) {
        if (k < int32_t(memoryWords) && !taken[size_t(k)]) { ++run; continue; }
        if (run > best.size()) best = {k - run, k};
        run = 0;
    }
    return best;
}
static Region randomRegion(std::mt19937& rng) {
    int32_t a = int32_t(rng() % memoryWords), n = int32_t(rng() % 200000);
    return {a, std::min(int32_t(memoryWords), a + n)};
}

int main() {
    // largestGap against a brute-force map, over takes, sounding takes, pins and a take in progress.
    {
        std::mt19937 rng(3);
        for (int trial = 0; trial < 40; ++trial) {
            kit::Bank b; std::array<Region, kit::SLOTS> pins{}; std::vector<Region> used;
            for (int s = 0; s < kit::SLOTS; ++s) {
                if (rng() % 3) { b.map.take[size_t(s)] = randomRegion(rng); used.push_back(b.map.take[size_t(s)]); }
                if (rng() % 4 == 0) { b.sounding[size_t(s)] = randomRegion(rng); used.push_back(b.sounding[size_t(s)]); }
                if (rng() % 4 == 0) { pins[size_t(s)] = randomRegion(rng); used.push_back(pins[size_t(s)]); }
            }
            Region gap = b.largestGap(&pins), want = bruteGap(used);
            assert(gap.size() == want.size());
            if (b.start(trial % 8, 48000, &pins)) { used.push_back({b.recording.begin, b.limit}); assert(b.largestGap(&pins).size() == bruteGap(used).size()); }
        }
    }
    // A take: written in order into the gap, made the slot's take on finish at the host rate; the
    // replaced take stays protected until protect() sees its voice has left it.
    {
        kit::Bank b; kit::Layout l; l.begin[2] = 0; l.end[2] = 1000; l.rate[2] = 44100; b.reset(l);
        std::vector<int16_t> memory(memoryWords, 0);
        uint32_t serial = b.map.serial;
        assert(b.start(2, 48000));
        assert(b.recording.begin == 1000 && b.limit == int32_t(memoryWords));
        for (int n = 0; n < 500; ++n) b.write(memory, float(n) / 1000);
        assert(b.finish() && !b.active());
        assert(b.map.take[2] == (Region{1000, 1500}) && b.map.rate[2] == 48000 && b.map.serial == serial + 1);
        for (int n = 0; n < 500; ++n) assert(memory[size_t(1000 + n)] == quantize(float(n) / 1000));
        assert(b.sounding[2] == (Region{0, 1000}));                                   // until protect() looks
        b.protect(2, Region{0, 1000}); assert(b.sounding[2] == (Region{0, 1000}));     // its voice is on it
        assert(b.largestGap().begin == 1500);
        b.protect(2, Region{}); assert(!b.sounding[2].playable());
        assert(b.largestGap() == (Region{1500, int32_t(memoryWords)}));
        // A take of one frame is dropped and the slot keeps its take.
        assert(b.start(2, 48000)); b.write(memory, .5f); assert(!b.finish() && b.map.take[2] == (Region{1000, 1500}));
        // A take that outgrows its gap is cut at the gap, and says so.
        kit::Layout full; full.begin[0] = 0; full.end[0] = int32_t(memoryWords) - 300; full.rate[0] = 44100; b.reset(full);
        assert(b.start(5, 48000) && b.limit - b.recording.begin == 300);
        for (int n = 0; n < 1000; ++n) b.write(memory, 0);
        assert(b.truncated && b.recording.size() == 300 && b.finish() && b.map.take[5].size() == 300);
        // No room: no take starts.
        full.end[0] = int32_t(memoryWords) - 1; b.reset(full); assert(!b.start(1, 48000) && !b.active());
    }
    // Recording never writes into a current take, a take still sounding, or a pinned region, through
    // random hits, takes, finishes and save pins.
    {
        std::mt19937 rng(11); kit::Bank b; kit::Layout l;
        int32_t at = 0;
        for (int s = 0; s < kit::SLOTS; ++s) { l.begin[size_t(s)] = at; at += 40000; l.end[size_t(s)] = at; l.rate[size_t(s)] = 44100; }
        b.reset(l);
        std::vector<int16_t> memory(memoryWords, 0);
        std::array<Region, kit::SLOTS> voice{}, pins{}; bool pinned = false; int takes = 0;
        for (int step = 0; step < 300000; ++step) {
            int s = int(rng() % kit::SLOTS);
            switch (rng() % 1000) {
                case 0: case 1: case 2: voice[size_t(s)] = b.map.take[size_t(s)]; break;                // a hit
                case 3: voice[size_t(s)] = {}; break;                                                  // its voice ends
                case 4: if (!b.active()) b.start(s, 48000, pinned ? &pins : nullptr); break;
                case 5: if (b.active() && b.finish()) ++takes; break;
                case 6: pins = b.map.take; pinned = true; break;
                case 7: pinned = false; break;
                default: break;
            }
            if (b.active()) {
                int32_t k = b.recording.end;
                if (k < b.limit) for (int t = 0; t < kit::SLOTS; ++t) {
                    auto inside = [k](Region r) { return k >= r.begin && k < r.end; };
                    assert(!inside(b.map.take[size_t(t)]) && !inside(voice[size_t(t)]) && !(pinned && inside(pins[size_t(t)])));
                }
                b.write(memory, 0);
            }
            for (int t = 0; t < kit::SLOTS; ++t) b.protect(t, voice[size_t(t)]);
        }
        assert(takes > 50);
    }
    // The hand-over: one thread records takes (each a recognisable pattern) and publishes the map, as
    // the audio thread does; another copies the takes as a save does. Every copy must be one whole,
    // finished take with its own pattern. Under TSan this also checks the hand-over is race-free.
    {
        kit::Bank b; kit::Layout l; for (int s = 0; s < kit::SLOTS; ++s) { l.begin[size_t(s)] = l.end[size_t(s)] = 0; l.rate[size_t(s)] = 44100; }
        b.reset(l);
        std::vector<int16_t> memory(memoryWords, 0);
        kit::Exchange x; x.publish(b.map);
        std::atomic<bool> stop{false}; std::atomic<int> copies{0};
        std::thread saver([&] {
            std::array<std::vector<int16_t>, kit::SLOTS> copied;
            while (!stop.load()) {
                // Only the copies of the pass copyTakes returns are promised to be whole takes.
                auto m = x.copyTakes([&](int s, Region r) {
                    copied[size_t(s)].assign(memory.begin() + r.begin, memory.begin() + std::max(r.begin, r.end));
                });
                for (int s = 0; s < kit::SLOTS; ++s) {
                    const auto& c = copied[size_t(s)];
                    assert(int32_t(c.size()) == std::max(0, m.take[size_t(s)].size()));
                    if (c.size() < 2) continue;
                    // A take is its id repeated, then its length in the last frame.
                    for (size_t k = 0; k + 1 < c.size(); ++k) assert(c[k] == c[0] && c[0] > 0);
                    assert(c.back() == int16_t(c.size() % 30000));
                }
                copies.fetch_add(1);
            }
        });
        std::mt19937 rng(5); std::array<Region, kit::SLOTS> voice{}; int16_t id = 1;
        for (int take = 0; take < 3000; ++take) {
            int s = int(rng() % kit::SLOTS);
            std::array<Region, kit::SLOTS> pins;
            if (!b.start(s, 48000, x.pins(pins) ? &pins : nullptr)) {   // full: let every voice end
                voice = {}; for (int t = 0; t < kit::SLOTS; ++t) b.protect(t, voice[size_t(t)]);
                continue;
            }
            int32_t n = 2 + int32_t(rng() % 3000);
            for (int32_t k = 0; k < n - 1; ++k) b.write(memory, id / 32768.f);
            b.write(memory, float(n % 30000) / 32768.f);
            if (b.recording.size() == n && b.finish()) { x.publish(b.map); id = int16_t(id % 30000 + 1); }
            else b.finish();
            if (rng() % 2) voice[size_t(s)] = b.map.take[size_t(s)]; else voice[size_t(s)] = {};
            for (int t = 0; t < kit::SLOTS; ++t) b.protect(t, voice[size_t(t)]);
        }
        stop = true; saver.join();
        assert(copies.load() > 10);
    }
    std::cout << "PASS: Otis Kit takes: largest-gap allocation against a brute-force map, takes written and made current at the host rate, replaced takes kept while sounding, short and truncated takes, no room; recording never over a current, sounding or pinned take in 300000 random steps; lock-free hand-over of takes to a concurrent saver\n";
}
