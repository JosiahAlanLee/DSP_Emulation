// Otis Kit's engine layer (docs/KIT.md): packing, region playback equal to OTIS playing the slot's
// sample alone, rate offsets, decay, and slot choice.
#include "../src/engine/Kit.hpp"
#include <cassert>
#include <iostream>
#include <random>

using namespace otis;
static std::vector<int16_t> noise(size_t n, unsigned seed) {
    std::mt19937 rng(seed); std::vector<int16_t> v(n);
    for (auto& x : v) x = int16_t(int(rng() % 40001) - 20000);
    return v;
}
// Kit voice s (bank) against a standalone Engine holding only that slot's sample, same controls.
static void sameAsAlone(const std::array<kit::Slot, kit::SLOTS>& slots, int s, float start, bool reverse,
                        float pitch, float k1, float level, int mode, double host) {
    std::vector<int16_t> image; auto layout = kit::pack(slots, image);
    Engine bank; bank.setHostRate(host); bank.install(image, kit::BANK_RATE, layout.frames, false);
    Engine alone; alone.setHostRate(host);
    std::vector<int16_t> own = slots[size_t(s)].pcm; alone.install(own, slots[size_t(s)].rate);
    for (Engine* e : {&bank, &alone}) { e->controls.engineMode = mode; e->controls.declick = false; }
    auto& b = bank.controls; size_t i = size_t(s);
    b.regionBegin[i] = layout.begin[i]; b.regionEnd[i] = layout.end[i];
    b.startv[i] = start; b.reversev[i] = reverse; b.loopv[i] = 0; b.k1v[i] = k1; b.level[i] = level;
    b.pitch[i] = float(pitch + layout.octaves[i]);
    auto& a = alone.controls;
    a.startv[0] = start; a.reversev[0] = reverse; a.loopv[0] = 0; a.k1v[0] = k1; a.level[0] = level; a.pitch[0] = pitch;
    bank.trigger(s); alone.trigger(0);
    double energy = 0;
    for (int n = 0; n < int(host * .6); ++n) {
        float x = bank.process(0), y = alone.process(0);
        assert(x == y); energy += double(x) * x;
    }
    assert(energy > 1e-3 && !bank.voices[i].running);   // it played, and the one-shot ended
}
int main() {
    // Packing: slots in order, contiguous, with the slot's own rate as an octave offset; a slot past
    // the end of memory is truncated and the later ones are empty.
    {
        std::array<kit::Slot, kit::SLOTS> slots;
        slots[0].pcm = noise(1000, 1); slots[2].pcm = noise(30, 2); slots[2].rate = 22050; slots[5].pcm = noise(1, 3);
        std::vector<int16_t> image; auto l = kit::pack(slots, image);
        assert(image.size() == memoryWords && l.frames == 1031);
        assert(l.begin[0] == 0 && l.end[0] == 1000 && l.begin[2] == 1000 && l.end[2] == 1030 && l.begin[5] == 1030 && l.end[5] == 1031);
        assert(l.playable(0) && !l.playable(1) && l.playable(2) && !l.playable(5) && std::abs(l.octaves[2] + 1) < 1e-12);
        assert(std::equal(slots[2].pcm.begin(), slots[2].pcm.end(), image.begin() + 1000));
        slots[3].pcm.assign(memoryWords - 500, 7); slots[4].pcm = noise(100, 4);
        l = kit::pack(slots, image);
        assert(l.truncated[3] && l.end[3] == int32_t(memoryWords) && !l.playable(4) && l.truncated[4] && l.frames == memoryWords);
    }
    // A slot in the bank plays exactly as OTIS plays its sample alone: forward and reverse, from a
    // START, transposed, filtered, at each chip clock, at two host rates.
    {
        std::array<kit::Slot, kit::SLOTS> slots;
        for (int s = 0; s < kit::SLOTS; ++s) slots[size_t(s)].pcm = noise(3000 + 977 * size_t(s), 10 + unsigned(s));
        for (int mode = 0; mode < 3; ++mode) for (double host : {44100., 96000.}) {
            sameAsAlone(slots, 0, 0, false, 0, 1, 1, mode, host);
            sameAsAlone(slots, 3, .25f, false, .5f, .4f, .7f, mode, host);
            sameAsAlone(slots, 7, 0, true, -.75f, 1, 1, mode, host);
            sameAsAlone(slots, 5, .5f, true, 1.f, .6f, .5f, mode, host);
        }
    }
    // A slot at another rate: the bank's pitch offset gives the same chip increment as the slot's
    // own rate would.
    for (double rate : {22050., 32000., 48000., 96000.}) {
        std::array<kit::Slot, kit::SLOTS> slots; slots[1].pcm = noise(2000, 5); slots[1].rate = rate; slots[0].pcm = noise(500, 6);
        std::vector<int16_t> image; auto l = kit::pack(slots, image);
        double viaOffset = kit::BANK_RATE / chipRates[0] * std::exp2(l.octaves[1]), direct = rate / chipRates[0];
        assert(Voice::pitchIncrement(viaOffset) == Voice::pitchIncrement(direct));
    }
    // DECAY: off at full, otherwise 60 dB in its time; a decaying hit stops below -80 dB.
    {
        assert(kit::decaySeconds(1) == 0 && std::abs(kit::decaySeconds(0) - .02) < 1e-12 && std::abs(kit::decaySeconds(.5f) - .02 * std::sqrt(500.)) < 1e-9);
        std::array<kit::Slot, kit::SLOTS> slots; slots[0].pcm.resize(44100 * 3);
        for (size_t n = 0; n < slots[0].pcm.size(); ++n) slots[0].pcm[n] = int16_t(12000 * std::sin(n * .05));
        std::vector<int16_t> image; auto l = kit::pack(slots, image);
        Engine e; e.setHostRate(48000); e.install(image, kit::BANK_RATE, l.frames, false);
        e.controls.regionBegin[0] = 0; e.controls.regionEnd[0] = l.end[0]; e.controls.loopv[0] = 0; e.controls.declick = false;
        e.controls.decaySeconds[0] = .5f; e.trigger(0);
        auto rms = [&](int n) { double s = 0; for (int k = 0; k < n; ++k) { float x = e.process(0); s += double(x) * x; } return std::sqrt(s / n); };
        double first = rms(2400); rms(24000 - 2400 - 1200); double later = rms(2400);   // centred near 0 s and 0.5 s
        double drop = 20 * std::log10(first / later);
        assert(drop > 54 && drop < 66);
        rms(48000); assert(!e.voices[0].running);
        // With decaySeconds 0 the output is bit-identical to an engine that never had the field set.
        Engine plain, zero; for (Engine* x : {&plain, &zero}) { x->setHostRate(48000); std::vector<int16_t> copy = image; x->install(copy, kit::BANK_RATE, l.frames, false); x->controls.declick = false; }
        zero.controls.decaySeconds[0] = 0; plain.trigger(0); zero.trigger(0);
        for (int n = 0; n < 20000; ++n) assert(plain.process(0) == zero.process(0));
    }
    // Slot choice: SLOT's bits 0-2 when patched, else the gate channel modulo 8.
    for (int ch = 0; ch < 16; ++ch) assert(kit::slotFor(false, 0xff, ch) == (ch & 7));
    for (int w = 0; w < 256; ++w) assert(kit::slotFor(true, uint8_t(w), 13) == (w & 7));
    std::cout << "PASS: Otis Kit packing and truncation, every slot equal to OTIS playing its sample alone (forward, reverse, START, pitch, filter, level, three clocks, two host rates), rate offsets, decay time and stop, decay off bit-identical, slot choice\n";
}
