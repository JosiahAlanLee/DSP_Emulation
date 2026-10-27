// SPDX-License-Identifier: BSD-3-Clause
// Otis Kit (docs/KIT.md): eight slots packed one after another into a single ES5505 sample memory,
// as the EPS laid out a bank. Slot s plays on chip voice s over its own region; a slot recorded or
// loaded at another rate keeps its pitch through a per-slot octave offset rather than resampling.
#pragma once
#include "OtisEngine.hpp"
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
// SLOT's bits 0-2 choose the slot when it is patched; otherwise GATE channel n plays slot n (mod 8).
inline int slotFor(bool slotPatched, uint8_t word, int channel) { return slotPatched ? word & 7 : channel & 7; }
} // namespace otis::kit
