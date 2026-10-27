// SPDX-License-Identifier: BSD-3-Clause
// CZ front-end data. MIDI maps: Casio CZ-1 MIDI Specification, pp. 84-98,
// SEND/RECEIVE REQUEST 1 (the explicitly documented CZ-101/5000 format).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace dspemu::cz {
enum class EnvKind { Dco = 0, Dcw = 1, Dca = 2 };
struct Envelope {
    std::array<int, 8> rate{{99, 60, 60, 60, 60, 60, 60, 60}};
    std::array<int, 8> level{};
    int sustain = 0, end = 1;
};
inline int rateCode(EnvKind kind, int value) {
    value = std::clamp(value, 0, 99);
    return kind == EnvKind::Dco ? 127 * value / 99 : 119 * value / 99 + (kind == EnvKind::Dcw ? 8 : 0);
}
inline int levelCode(EnvKind kind, int value) {
    value = std::clamp(value, 0, 99);
    if (kind == EnvKind::Dca)
        return value;
    if (kind == EnvKind::Dcw)
        return 127 * value / 99;
    return value < 64 ? value : value + 4;
}
inline int fromCode(EnvKind kind, int code, bool rate) {
    int best = 0, distance = 10000;
    for (int i = 0; i < 100; ++i) {
        int d = std::abs((rate ? rateCode(kind, i) : levelCode(kind, i)) - code);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}
struct Line {
    int wave = 0, second = 0, window = 0;
    bool alternate = false;
    int dcaKey = 0, dcwKey = 0;
    std::array<Envelope, 3> env{};
    Line() {
        env[0].end = 0;
        env[0].sustain = 0;
        env[1].level[0] = 25;
        env[1].level[1] = 0;
        env[2].level[0] = 99;
    }
};
struct Tone {
    std::array<Line, 2> line{};
    int mode = 0, octave = 0, detune = 0, fine = 0, detuneSign = 1, mod = 0;
    int vibWave = 0, vibDelay = 0, vibRate = 50, vibDepth = 0;
    bool vibrato = true, portamento = false, muteOther = false;
    int portamentoTime = 0, bendRange = 2;
    int chipPitchMod = 0;
    float tuning = 440;
    bool sourceValid = false, sourceHighNibbles = false;
    int sourceChannel = 0, sourceCommand = 0x30, sourceBank = 0x60;
    std::array<uint8_t, 128> source{};
};
inline bool paired(const Tone &t) { return t.mode >= 2; }
inline int noteLimit(const Tone &t) { return paired(t) ? 4 : 8; }
inline uint16_t waveRegister(const Line &l) {
    return uint16_t((std::clamp(l.wave, 0, 7) << 13) | (std::clamp(l.second, 0, 7) << 10) |
                    (l.alternate ? 512 : 0) | (std::clamp(l.window, 0, 7) << 6));
}
} // namespace dspemu::cz
