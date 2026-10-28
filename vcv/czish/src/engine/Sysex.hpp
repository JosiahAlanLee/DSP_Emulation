// SPDX-License-Identifier: BSD-3-Clause
// Original transcription of Casio Guidebook for MIDI (Nov. 1985), pp. 19-33,
// and CZ-1 MIDI Specification pp. 82-98, REQUEST 1 compatibility format.
#pragma once
#include "Tone.hpp"
#include <string>
#include <vector>
namespace dspemu::cz::sysex {
inline bool hardwareTone(const Tone &t) {
    if (t.chipPitchMod || t.muteOther)
        return false;
    for (const auto &l : t.line) {
        if (l.wave == 3 || l.wave == 7 || l.window > 3)
            return false;
        if (l.alternate && (l.second == 3 || l.second == 7 || (l.wave == 6 && l.second == 6)))
            return false;
        if (l.window && l.wave != 6 && (!l.alternate || l.second != 6))
            return false;
    }
    return true;
}
inline int vibratoValue(int kind, int v) {
    v = std::clamp(v, 0, 99);
    if (kind == 1) {
        if (v < 32)
            return (v + 1) * 32;
        int group = (v - 32) / 16;
        return (1120 << group) + ((1 << group) - 1) * 32 + (v % 16) * (64 << group);
    }
    if (kind == 2 && v == 99)
        return 0x300;
    if (v < 32)
        return v + (kind == 2);
    int group = (v - 32) / 16;
    return ((kind == 2 ? 35 : 33) << group) + ((1 << group) - 1) + (v % 16) * (2 << group);
}
inline int fineCode(int fine) {
    fine = std::clamp(fine, 0, 60);
    return fine + (fine ? ((fine - 1) / 15) : 0);
}
inline int word(const std::array<uint8_t, 128> &p, int offset) {
    return p[offset] | (int(p[offset + 1]) << 8);
}
inline bool decodePayload(const std::array<uint8_t, 128> &p, Tone &t, std::string &error) {
    if (p[0] > 11 || (p[0] & 12) == 12 || p[3] > 47 || (p[2] & 3)) {
        error = "Invalid tone header or detune fields";
        return false;
    }
    t.mode = p[0] & 3;
    t.octave = (p[0] >> 2) & 3;
    if (t.octave == 2)
        t.octave = -1;
    t.detuneSign = (p[1] & 1) ? -1 : 1;
    t.detune = p[3] & 63;
    int fine = p[2] >> 2;
    t.fine = 0;
    for (int i = 0; i <= 60; ++i)
        if (std::abs(fineCode(i) - fine) < std::abs(fineCode(t.fine) - fine))
            t.fine = i;
    const int waves[4] = {8, 4, 32, 2};
    t.vibWave = 0;
    for (int i = 0; i < 4; ++i)
        if (p[4] == waves[i])
            t.vibWave = i;
    if (p[4] != 32 && p[4] != 2 && p[4] != 4 && p[4] != 8) {
        error = "Invalid vibrato waveform";
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        if (p[5 + 3 * k] > 99) {
            error = "Vibrato parameter exceeds 99";
            return false;
        }
    }
    t.vibDelay = p[5];
    t.vibRate = p[8];
    t.vibDepth = p[11];
    for (int l = 0; l < 2; ++l) {
        int offset = 14 + 57 * l;
        int w = word(p, offset);
        auto &line = t.line[l];
        line.wave = (w >> 13) & 7;
        line.second = (w >> 10) & 7;
        line.alternate = w & 512;
        line.window = (w >> 6) & 7;
        if (!l) {
            t.mod = w & 32 ? 1 : ((w & 24) == 24 ? 2 : 0);
            t.muteOther = w & 4;
            t.chipPitchMod = ((w >> 3) & 3) == 2 ? 2 : 0;
        }
        line.dcaKey = p[offset + 2];
        line.dcwKey = p[offset + 4];
        if (line.dcaKey > 9 || line.dcwKey > 9) {
            error = "Key follow exceeds 9";
            return false;
        }
        for (int order = 0; order < 3; ++order) {
            int e = 2 - order;
            int base = offset + 6 + 17 * order;
            auto &env = line.env[e];
            env.end = p[base];
            env.sustain = -1;
            if (env.end > 7) {
                error = "Envelope end exceeds eight steps";
                return false;
            }
            for (int step = 0; step < 8; ++step) {
                env.rate[step] = fromCode(EnvKind(e), p[base + 1 + step * 2] & 127, true);
                env.level[step] = fromCode(EnvKind(e), p[base + 2 + step * 2] & 127, false);
                if (p[base + 2 + step * 2] & 128) {
                    if (env.sustain != -1) {
                        error = "Envelope has more than one sustain point";
                        return false;
                    }
                    env.sustain = step;
                }
            }
        }
    }
    t.sourceValid = true;
    t.source = p;
    return true;
}
inline bool decode(const std::vector<uint8_t> &data, Tone &result, std::string &error) {
    int start = 0;
    if (data.size() == 263 && data[5] == 0x30)
        start = 6;
    if (data.size() == 264 && data[5] == 0x20 && data[6] <= 0x60)
        start = 7;
    if (!start || data[0] != 0xf0 || data[1] != 0x44 || data[2] != 0 || data[3] != 0 ||
        (data[4] & 0xf0) != 0x70 || data.back() != 0xf7) {
        error = "Expected one CZ-101 tone dump (263 bytes) or receive request (264 bytes)";
        return false;
    }
    for (int order = 0; order < 2; ++order) {
        std::array<uint8_t, 128> payload{};
        for (int i = 0; i < 128; ++i) {
            uint8_t a = data[start + 2 * i], b = data[start + 2 * i + 1];
            if (a > 15 || b > 15) {
                error = "Tone data must contain four-bit MIDI nibbles";
                return false;
            }
            payload[i] = order ? ((a << 4) | b) : (a | (b << 4));
        }
        Tone candidate;
        if (!decodePayload(payload, candidate, error))
            continue;
        candidate.sourceHighNibbles = order;
        candidate.sourceChannel = data[4] & 15;
        candidate.sourceCommand = data[5];
        if (start == 7)
            candidate.sourceBank = data[6];
        result = candidate;
        error.clear();
        return true;
    }
    return false;
}
inline std::array<uint8_t, 128> encodePayload(const Tone &t) {
    std::array<uint8_t, 128> p{};
    Tone original;
    std::string ignored;
    bool source = t.sourceValid && decodePayload(t.source, original, ignored);
    auto preserve = [&](int at, int count, bool same) {
        if (source && same)
            for (int i = 0; i < count; ++i)
                p[at + i] = t.source[at + i];
    };
    p[0] = uint8_t(std::clamp(t.mode, 0, 3) | ((t.octave < 0 ? 2 : std::clamp(t.octave, 0, 1)) << 2));
    preserve(0, 1, t.mode == original.mode && t.octave == original.octave);
    p[1] = t.detuneSign < 0;
    preserve(1, 1, t.detuneSign == original.detuneSign);
    p[2] = uint8_t(fineCode(t.fine) << 2);
    p[3] = uint8_t(std::clamp(t.detune, 0, 47));
    preserve(2, 2, t.fine == original.fine && t.detune == original.detune);
    const int waves[4] = {8, 4, 32, 2};
    p[4] = waves[std::clamp(t.vibWave, 0, 3)];
    preserve(4, 1, t.vibWave == original.vibWave);
    const int vib[3] = {t.vibDelay, t.vibRate, t.vibDepth},
              oldVib[3] = {original.vibDelay, original.vibRate, original.vibDepth};
    for (int k = 0; k < 3; ++k) {
        int at = 5 + 3 * k, value = vibratoValue(k, vib[k]);
        p[at] = uint8_t(std::clamp(vib[k], 0, 99));
        p[at + 1] = uint8_t(value >> 8);
        p[at + 2] = uint8_t(value);
        preserve(at, 3, vib[k] == oldVib[k]);
    }
    constexpr int amp[10] = {0, 8, 17, 26, 36, 47, 58, 69, 82, 95},
                  wave[10] = {0, 31, 44, 57, 70, 83, 96, 110, 146, 255};
    for (int l = 0; l < 2; ++l) {
        int at = 14 + 57 * l;
        const auto &line = t.line[l];
        const auto &old = original.line[l];
        int w = waveRegister(line);
        if (!l)
            w |= t.mod == 1 ? 32 : (t.mod == 2 ? 24 : 0);
        p[at] = uint8_t(w);
        p[at + 1] = uint8_t(w >> 8);
        // Line 1's word also carries ring/noise, CHIP mute (bit 2) and CHIP pitch
        // modulation (bits 4:3). Keep the source word only when all of them are
        // unchanged; otherwise write it from the tone, which has no CHIP bits.
        preserve(at, 2,
                 waveRegister(line) == waveRegister(old) &&
                     (l || (t.mod == original.mod && t.muteOther == original.muteOther &&
                            t.chipPitchMod == original.chipPitchMod)));
        p[at + 2] = uint8_t(std::clamp(line.dcaKey, 0, 9));
        p[at + 3] = amp[p[at + 2]];
        preserve(at + 2, 2, line.dcaKey == old.dcaKey);
        p[at + 4] = uint8_t(std::clamp(line.dcwKey, 0, 9));
        p[at + 5] = wave[p[at + 4]];
        preserve(at + 4, 2, line.dcwKey == old.dcwKey);
        for (int order = 0; order < 3; ++order) {
            int e = 2 - order;
            auto &env = line.env[e];
            auto &before = old.env[e];
            int base = at + 6 + 17 * order;
            p[base] = uint8_t(std::clamp(env.end, 0, 7));
            preserve(base, 1, env.end == before.end);
            int previous = 0;
            for (int step = 0; step < 8; ++step) {
                int index = base + 1 + step * 2;
                int r = rateCode(EnvKind(e), env.rate[step]);
                int level = levelCode(EnvKind(e), step == env.end ? 0 : env.level[step]);
                p[index] = uint8_t(r | (level < previous ? 128 : 0));
                p[index + 1] = uint8_t(level | (step == env.sustain ? 128 : 0));
                preserve(index, 1,
                         env.rate[step] == before.rate[step] && env.level[step] == before.level[step] &&
                             (step == 0 || env.level[step - 1] == before.level[step - 1]));
                preserve(index + 1, 1,
                         env.level[step] == before.level[step] && env.sustain == before.sustain &&
                             env.end == before.end);
                previous = level;
            }
        }
    }
    return p;
}
inline std::vector<uint8_t> encode(const Tone &t, int channel = -1) {
    auto payload = encodePayload(t);
    int ch = channel < 0 ? t.sourceChannel : channel;
    int command = t.sourceValid ? t.sourceCommand : 0x30;
    std::vector<uint8_t> out{0xf0, 0x44, 0, 0, uint8_t(0x70 | std::clamp(ch, 0, 15)), uint8_t(command)};
    if (command == 0x20)
        out.push_back(uint8_t(t.sourceBank));
    for (auto byte : payload) {
        out.push_back(t.sourceHighNibbles ? byte >> 4 : byte & 15);
        out.push_back(t.sourceHighNibbles ? byte & 15 : byte >> 4);
    }
    out.push_back(0xf7);
    return out;
}
} // namespace dspemu::cz::sysex
