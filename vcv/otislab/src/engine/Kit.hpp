// SPDX-License-Identifier: BSD-3-Clause
// Otis Kit (docs/KIT.md): eight slots in a single ES5505 sample memory, as the EPS laid out a bank.
// Slot s plays on chip voice s over its own region; a slot recorded or loaded at another rate keeps
// its pitch through a per-slot octave offset rather than resampling. Capture (docs/CAPTURE.md) records
// new takes straight into free memory; a finished take becomes its slot's region, and the take it
// replaces stays untouched while a voice is still playing it.
#pragma once
#include "OtisEngine.hpp"
#include <algorithm>
#include <atomic>
#include <string>

namespace otis::kit {
constexpr int SLOTS = 8;
constexpr double BANK_RATE = 44100;   // the memory's nominal rate

struct Slot {
    std::vector<int16_t> pcm;          // mono
    double rate = BANK_RATE;
    std::string name;
};
struct Layout {
    std::array<int32_t, SLOTS> begin{}, end{};    // [begin, end) frames of sample memory
    std::array<double, SLOTS> octaves{};          // log2(slot rate / BANK_RATE), added to pitch
    std::array<double, SLOTS> rate{};
    std::array<bool, SLOTS> truncated{};
    size_t frames = 0;                            // memory in use
    bool playable(int s) const { return s >= 0 && s < SLOTS && end[s] - begin[s] >= 2; }
};
// Pack the slots in order. A slot that no longer fits is truncated and the ones after it are empty;
// nothing moves the earlier slots.
inline Layout pack(const std::array<Slot, SLOTS>& slots, std::vector<int16_t>& image) {
    image.assign(memoryWords, 0);
    Layout layout; size_t at = 0;
    for (int s = 0; s < SLOTS; ++s) {
        size_t n = std::min(slots[s].pcm.size(), memoryWords - at);
        layout.truncated[s] = n < slots[s].pcm.size();
        std::copy(slots[s].pcm.begin(), slots[s].pcm.begin() + n, image.begin() + at);
        layout.begin[s] = int32_t(at); layout.end[s] = int32_t(at + n); at += n;
        double rate = slots[s].rate;
        layout.rate[s] = std::isfinite(rate) && rate > 0 ? rate : BANK_RATE;
        layout.octaves[s] = std::isfinite(rate) && rate > 0 ? std::log2(rate / BANK_RATE) : 0;
    }
    layout.frames = at;
    return layout;
}
// DECAY: 20 ms to 10 s to fall 60 dB, exponential in the knob; fully clockwise holds (no decay).
inline double decaySeconds(float knob) {
    if (!std::isfinite(knob) || knob >= 1.f) return 0;
    return 0.02 * std::pow(500.0, clamp(double(knob), 0.0, 1.0));
}
struct Region {
    int32_t begin = 0, end = 0;                   // [begin, end) frames of sample memory
    int32_t size() const { return end - begin; }
    bool playable() const { return size() >= 2; }
    bool operator==(const Region& o) const { return begin == o.begin && end == o.end; }
    bool operator!=(const Region& o) const { return !(*this == o); }
};
// Each slot's current take and its rate. serial rises with every finished take and every repack.
// generation[s] is the serial at which slot s last changed: a new take can reuse the region of an
// older one, so a saver tells takes apart by generation, never by region.
struct BankMap {
    std::array<Region, SLOTS> take{};
    std::array<double, SLOTS> rate{};
    std::array<uint32_t, SLOTS> generation{};
    uint32_t serial = 0;
};
// The audio thread's bank. Recording writes only into memory that no current take, no take still
// sounding on its voice, and no region pinned by a save in progress covers, so nothing that can be
// heard or copied is ever overwritten, and nothing is allocated or freed.
struct Bank {
    BankMap map;
    std::array<Region, SLOTS> sounding{};         // a replaced take its voice is still playing
    Region recording{};                           // written so far
    int32_t limit = 0;                            // the end of the gap the take may fill
    int slot = -1;                                // the slot being recorded, or -1
    double rate = BANK_RATE;
    bool truncated = false;                       // the take reached the end of its gap

    bool active() const { return slot >= 0; }
    void reset(const Layout& l) {
        for (int s = 0; s < SLOTS; ++s) { map.take[size_t(s)] = {l.begin[size_t(s)], l.end[size_t(s)]}; map.rate[size_t(s)] = l.rate[size_t(s)]; }
        map.serial++; map.generation.fill(map.serial); sounding = {}; cancel();
    }
    void cancel() { slot = -1; recording = {}; limit = 0; truncated = false; }
    // The longest stretch of memory nothing above covers.
    Region largestGap(const std::array<Region, SLOTS>* pinned = nullptr) const {
        std::array<Region, 3 * SLOTS + 1> used{}; size_t n = 0;
        for (int s = 0; s < SLOTS; ++s) {
            used[n++] = map.take[size_t(s)]; used[n++] = sounding[size_t(s)];
            if (pinned) used[n++] = (*pinned)[size_t(s)];
        }
        if (active()) used[n++] = {recording.begin, limit};
        std::sort(used.begin(), used.begin() + n, [](const Region& a, const Region& b) { return a.begin < b.begin; });
        Region best{}; int32_t at = 0;
        for (size_t k = 0; k < n; ++k) {
            if (used[k].size() <= 0) continue;
            if (used[k].begin - at > best.size()) best = {at, used[k].begin};
            at = std::max(at, used[k].end);
        }
        if (int32_t(memoryWords) - at > best.size()) best = {at, int32_t(memoryWords)};
        return best;
    }
    // Start a take for slot s at the host rate, in the largest gap. False when there is no room.
    bool start(int s, double hostRate, const std::array<Region, SLOTS>* pinned = nullptr) {
        if (active() || s < 0 || s >= SLOTS) return false;
        Region gap = largestGap(pinned);
        if (gap.size() < 2) return false;
        recording = {gap.begin, gap.begin}; limit = gap.end; slot = s; rate = hostRate; truncated = false;
        return true;
    }
    void write(std::vector<int16_t>& memory, float x) {
        if (!active()) return;
        if (recording.end >= limit) { truncated = true; return; }
        memory[size_t(recording.end++)] = quantize(x);
    }
    // End the take: it becomes the slot's take. Returns false (and keeps the old take) if it is
    // shorter than two frames. The replaced take stays protected until protect() next looks at the
    // slot's voice, and is freed then unless the voice is still on it.
    bool finish() {
        if (!active()) return false;
        int s = slot; Region take = recording; double r = rate; cancel();
        if (!take.playable()) return false;
        if (!sounding[size_t(s)].playable()) sounding[size_t(s)] = map.take[size_t(s)];
        map.take[size_t(s)] = take; map.rate[size_t(s)] = r; map.generation[size_t(s)] = ++map.serial;
        return true;
    }
    // Keep the region voice s is reading out of the free memory while it differs from the slot's take.
    void protect(int s, Region playing) { sounding[size_t(s)] = playing == map.take[size_t(s)] ? Region{} : playing; }
};

// Hands the bank's takes from the audio thread to a thread that saves them, without either waiting:
// the audio thread publishes the map under a sequence lock; the saver pins the takes it is about to
// copy, so no new take is recorded over them, copies, unpins, and starts again if a take finished in
// between (that is the only way a copied region can have been freed and reused).
struct Exchange {
    std::array<std::atomic<int32_t>, SLOTS> begin{}, end{}, pinBegin{}, pinEnd{};
    std::array<std::atomic<double>, SLOTS> rate{};
    std::array<std::atomic<uint32_t>, SLOTS> generation{};
    std::atomic<uint32_t> serial{0}, sequence{0};
    std::atomic<bool> pinned{false};

    // Audio thread.
    void publish(const BankMap& m) {
        uint32_t q = sequence.load(std::memory_order_relaxed);
        sequence.store(q + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (int s = 0; s < SLOTS; ++s) {
            begin[size_t(s)].store(m.take[size_t(s)].begin, std::memory_order_relaxed);
            end[size_t(s)].store(m.take[size_t(s)].end, std::memory_order_relaxed);
            rate[size_t(s)].store(m.rate[size_t(s)], std::memory_order_relaxed);
            generation[size_t(s)].store(m.generation[size_t(s)], std::memory_order_relaxed);
        }
        serial.store(m.serial, std::memory_order_relaxed);
        sequence.store(q + 2, std::memory_order_release);
    }
    // Audio thread, when a take starts: the regions a save is copying, if one is.
    bool pins(std::array<Region, SLOTS>& out) const {
        if (!pinned.load(std::memory_order_acquire)) return false;
        for (int s = 0; s < SLOTS; ++s) out[size_t(s)] = {pinBegin[size_t(s)].load(std::memory_order_relaxed), pinEnd[size_t(s)].load(std::memory_order_relaxed)};
        return true;
    }
    // Saving thread.
    BankMap read() const {
        BankMap m; uint32_t before, after;
        do {
            before = sequence.load(std::memory_order_acquire);
            for (int s = 0; s < SLOTS; ++s) {
                m.take[size_t(s)] = {begin[size_t(s)].load(std::memory_order_relaxed), end[size_t(s)].load(std::memory_order_relaxed)};
                m.rate[size_t(s)] = rate[size_t(s)].load(std::memory_order_relaxed);
                m.generation[size_t(s)] = generation[size_t(s)].load(std::memory_order_relaxed);
            }
            m.serial = serial.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            after = sequence.load(std::memory_order_relaxed);
        } while (before != after || (before & 1));
        return m;
    }
    // copy(slot, region, generation) reads the region out of sample memory. Returns the map the copies match.
    template <class Copy> BankMap copyTakes(Copy copy) {
        for (;;) {
            BankMap m = read();
            for (int s = 0; s < SLOTS; ++s) {
                pinBegin[size_t(s)].store(m.take[size_t(s)].begin, std::memory_order_relaxed);
                pinEnd[size_t(s)].store(m.take[size_t(s)].end, std::memory_order_relaxed);
            }
            pinned.store(true, std::memory_order_seq_cst);
            for (int s = 0; s < SLOTS; ++s) copy(s, m.take[size_t(s)], m.generation[size_t(s)]);
            pinned.store(false, std::memory_order_release);
            if (read().serial == m.serial) return m;
        }
    }
};

// SLOT's bits 0-2 choose the slot when it is patched; otherwise GATE channel n plays slot n (mod 8).
inline int slotFor(bool slotPatched, uint8_t word, int channel) { return slotPatched ? word & 7 : channel & 7; }
} // namespace otis::kit
