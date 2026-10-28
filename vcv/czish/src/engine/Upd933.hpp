// SPDX-License-Identifier: BSD-3-Clause
// Portable sound-path adaptation of MAME's upd933.cpp, copyright Devin Acker.
// Upstream: mamedev/mame 55784e9c9fac41cd7b019f23891c92292af9c679.
// MAME's device, scheduler and stream interfaces have been removed; register
// writes and one-sample rendering are explicit. See THIRD_PARTY_NOTICES.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dspemu::cz {

class Upd933 {
public:
    static constexpr int voices = 8;
    static constexpr double sampleRate = 40000.;
    static constexpr unsigned noteShift = 9, pitchShift = 20, fineShift = 12;
    static constexpr unsigned volumeShift = 12, dcaShift = 16, dcwShift = 16, dcoShift = 11;
    // Where each envelope register's level field lands in the envelope's
    // 32-bit counter (MAME's ENV_*_SHIFT plus the register-specific offset).
    // A DCO level with bit 6 set is shifted a further dcoUnitShift bits.
    static constexpr unsigned dcaTargetShift = dcaShift + 2, dcwTargetShift = dcwShift + 3,
                              dcoTargetShift = dcoShift + 5, dcoUnitShift = 5;

    struct Envelope {
        uint8_t direction = 0, sustain = 1, irq = 0;
        uint32_t rate = 0, target = 0, current = 0;
        void update() {
            if (current != target) {
                if (!direction) {
                    if (current > target || target - current <= rate) current = target;
                    else current += rate;
                } else {
                    if (current < target || current - target <= rate) current = target;
                    else current -= rate;
                }
            }
            if (!sustain && current == target) irq = sustain = true;
        }
    };
    struct Voice {
        uint8_t wave[2] = {0, 0};
        uint8_t window = 0, ringMod = 0, pitchMod = 0, muteOther = 0;
        uint16_t pitch = 0;
        uint32_t position = 0, pitchStep = 0;
        uint16_t dcwLimit = 0;
        int16_t pmLevel = 0;
    };

    std::array<Voice, voices> voice{};
    std::array<Envelope, voices> dca{}, dco{}, dcw{};
    std::array<int, voices> dcwOffset{}; // optional CV extension, zero for parity
    std::array<int16_t, voices> lastVoices{};

    Upd933() {
        constexpr double pi = 3.1415926535897932384626433832795;
        for (int i = 0; i < 0x800; ++i)
            cosine_[i] = uint16_t(0xfff * (1 - std::cos(2 * pi * i / 0x7ff)) / 2);
        for (int i = 0; i < 0x80; ++i) {
            double freq = 442. * std::pow(2., (i - 62) / 12.);
            pitch_[i] = uint32_t((1 << pitchShift) * (freq * 0x800 / sampleRate));
        }
        for (int i = 0; i < 0x200; ++i)
            pitchFine_[i] = uint16_t((1 << fineShift) * (std::pow(2., double(i) / (12. * 0x200)) - 1));
        for (int i = 1; i < 0x200; ++i)
            volume_[i] = uint16_t(std::pow(2 << volumeShift, double(i) / 0x1ff));
        volume_[0] = 0;
        reset();
    }

    void reset(uint64_t seed = 0x933) {
        voice = {}; dca = {}; dco = {}; dcw = {}; dcwOffset = {}; lastVoices = {};
        registers_ = {}; sampleCount_ = 0; lastSample_ = 0; random_ = seed;
    }
    static uint32_t envelopeRate(uint8_t data) { return uint32_t(8 | (data & 7)) << (data >> 3); }
    // The counter value a register write's level field selects; the same
    // arithmetic setEnvelope uses. Callers choosing the direction bit must
    // compare against this, not against their own shift.
    static uint32_t envelopeTarget(uint16_t value, unsigned shift, bool pitch) {
        uint32_t target = uint32_t(value & (pitch ? 63 : 127)) << shift;
        if (pitch && (value & 0x40)) target <<= dcoUnitShift;
        return target;
    }

    // The caller owns voice allocation, note timing, and envelope sequencing.
    // Values and register numbers are the chip's 16-bit interface.
    void write(uint8_t reg, uint16_t value) {
        registers_[reg] = value;
        int n = reg & 7;
        Voice& v = voice[n];
        switch (reg >> 3) {
        case 0x0: setEnvelope(dca[n], value, dcaTargetShift, false); break;
        case 0x2: setEnvelope(dco[n], value, dcoTargetShift, true); break;
        case 0x4: setEnvelope(dcw[n], value, dcwTargetShift, false); break;
        case 0xc: v.pitch = value; updatePitch(n); break;
        case 0xd: {
            v.wave[0] = (value >> 13) & 7;
            v.wave[1] = (value & (1u << 9)) ? ((value >> 10) & 7) : v.wave[0];
            v.window = (value >> 6) & 7;
            if (!(n & 1)) {
                Voice& mod = voice[(n + 6) & 7];
                mod.ringMod = (value >> 5) & 1;
                mod.pitchMod = (value >> 3) & 3;
                mod.muteOther = (value >> 2) & 1;
            }
            break;
        }
        case 0x13: v.position = uint32_t(value) << (pitchShift - 4); break;
        case 0x17:
            if (n < 4) voice[n << 1].pmLevel = int16_t(value);
            break;
        default: break;
        }
    }

    // One 40 kHz chip sample; the contribution array is an optional Rack
    // extension. Its order preserves MAME's cross-voice modulation ordering.
    int16_t render() {
        int32_t sum = 0;
        static constexpr int order[] = {5, 0, 7, 2, 1, 4, 3, 6};
        for (int n : order) { lastVoices[n] = updateVoice(n); sum += lastVoices[n]; }
        ++sampleCount_;
        return int16_t(std::clamp(sum, -32768, 32767));
    }

private:
    std::array<uint16_t, 0x800> cosine_{};
    std::array<uint32_t, 0x80> pitch_{};
    std::array<uint16_t, 0x200> pitchFine_{}, volume_{};
    std::array<uint16_t, 256> registers_{};
    uint32_t sampleCount_ = 0;
    int16_t lastSample_ = 0;
    uint64_t random_ = 0;

    static void setEnvelope(Envelope& e, uint16_t value, unsigned shift, bool pitch) {
        e.direction = (value >> 15) & 1;
        e.rate = envelopeRate((value >> 8) & 127);
        e.sustain = (value >> 7) & 1;
        e.target = envelopeTarget(value, shift, pitch);
        e.irq = 0;
    }
    uint64_t nextRandom() {
        uint64_t z = (random_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    void updatePitch(int n) {
        Voice& v = voice[n];
        int32_t pitch = int32_t(v.pitch + (dco[n].current >> dcoShift)) + v.pmLevel;
        uint32_t step = 0;
        if (pitch > 0 && pitch < (1 << 16)) {
            uint8_t note = pitch >> noteShift;
            uint16_t fine = pitch & ((1 << noteShift) - 1);
            step = pitch_[note];
            if (fine) step += (step >> fineShift) * pitchFine_[fine];
        }
        v.pitchStep = step;
        v.dcwLimit = uint16_t(0x400 - std::min(0x400u, step >> (pitchShift - 2)));
    }
    int16_t updateVoice(int n) {
        Voice& v = voice[n];
        int16_t sample = 0;
        uint16_t pos = (v.position >> pitchShift) & 0x7ff;
        uint8_t wave = (v.position >> (pitchShift + 11)) & 1;
        int baseDcw = int(dcw[n].current >> dcwShift);
        uint16_t shape = uint16_t(std::min(int(v.dcwLimit), std::clamp(baseDcw + dcwOffset[n], 0, 1016)));
        uint16_t pivot = 0x400 - shape, phase = 0, window = 0;
        switch (v.wave[wave] & 7) {
        case 0:
            phase = pos < pivot ? pos * 0x400 / pivot : 0x400 + (pos - pivot) * 0x400 / (0x800 - pivot); break;
        case 1:
            phase = (pos & 0x3ff) < pivot ? (pos & 0x3ff) * 0x400 / pivot : 0x3ff;
            phase |= pos & 0x400; break;
        case 2: phase = pos < pivot * 2 ? pos * 0x800 / (pivot * 2) : 0x7ff; break;
        case 3: break;
        case 4:
            phase = pos < pivot ? pos * 0x800 / pivot : (pos - pivot) * 0x800 / (0x800 - pivot); break;
        case 5:
            if (pos < 0x400) phase = pos;
            else if (pos < pivot + 0x400) phase = 0x400 + (pos & 0x3ff) * 0x400 / pivot;
            else phase = 0x7ff;
            break;
        case 6: phase = (pos + ((pos * shape) >> 6)) & 0x7ff; break;
        case 7: phase = (pos & 0x3ff) < pivot ? (pos & 0x3ff) * 0x400 / pivot : 0x7ff; break;
        }
        switch (v.window & 7) {
        case 0: break;
        case 1: window = pos; break;
        case 2: window = (pos & 0x3ff) * 2; if (pos < 0x400) window ^= 0x7fe; break;
        case 3: if (pos >= 0x400) window = (pos & 0x3ff) * 2; break;
        case 4: window = pos < 0x400 ? pos * 2 : 0x7ff; break;
        default: window = (0x3ff ^ (pos & 0x3ff)) * 2; break;
        }
        sample = int16_t(cosine_[phase]);
        if (window) sample = int16_t((int32_t(sample) * (0x800 - window)) / 0x800);
        uint16_t volume = uint16_t(dca[n].current >> dcaShift);
        sample = int16_t((int32_t(sample) * volume_[volume]) >> volumeShift);
        sample = int16_t(sample - volume_[volume] / 2);
        if (v.ringMod) sample = int16_t((int32_t(sample) * lastSample_) / 0x1000);
        if (v.muteOther) sample = int16_t(sample - lastSample_);
        uint32_t oldDco = dco[n].current;
        int16_t oldPm = v.pmLevel;
        dca[n].update(); dcw[n].update(); dco[n].update();
        if (!(sampleCount_ & 7)) {
            switch (v.pitchMod & 3) {
            default: v.pmLevel = 0; break;
            case 2: v.pmLevel = lastSample_; break;
            case 3: v.pmLevel = int16_t(nextRandom() & (32 << noteShift)); break;
            }
        }
        if (((oldDco ^ dco[n].current) >> dcoShift) || oldPm != v.pmLevel) updatePitch(n);
        v.position += v.pitchStep;
        lastSample_ = sample;
        return sample;
    }
};
} // namespace dspemu::cz
