// SPDX-License-Identifier: BSD-3-Clause
// Early firmware-free, single-line front end for the portable uPD933 core.
#pragma once
#include "Upd933.hpp"
#include "../../../shared/DacStage.hpp"
#include "../../../shared/ByteBus.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace dspemu::cz {
struct Input {
    float pitch = 0, gate = 0, dcw = 0;
};
struct Parameters {
    int wave = 0, window = 0;
    float shape = 32, attackMs = 2, releaseMs = 250, tracking = 0;
};

// The chip's DCA register holds a 7-bit rate. Choose the nearest available
// rate to a requested full-scale duration. This is a convenience front end,
// not a claim that these are the CZ firmware's 0–99 envelope mappings.
inline uint8_t rateForMilliseconds(float ms) {
    if (!std::isfinite(ms)) ms = 2;
    double wanted = double(127u << 18) / (std::clamp(double(ms), 2., 4000.) * 40.);
    int low = 0, high = 127;
    while (low < high) {
        int mid = (low + high) / 2;
        if (Upd933::envelopeRate(uint8_t(mid)) < wanted) low = mid + 1;
        else high = mid;
    }
    if (low > 0 && std::abs(double(Upd933::envelopeRate(uint8_t(low - 1))) - wanted) <
                   std::abs(double(Upd933::envelopeRate(uint8_t(low))) - wanted)) --low;
    return uint8_t(low);
}

inline uint16_t pitchRegister(float volts) {
    if (!std::isfinite(volts)) volts = 0;
    // MAME's pitch table uses 442 Hz at note index 62. Shift the code so the
    // Rack convention is C4 = 0 V and A4 = 440 Hz.
    double note = 62. + 12. * std::log2(261.6255653005986 / 442.) +
                  12. * std::clamp(double(volts), -8., 8.);
    return uint16_t(std::clamp<long>(std::lround(note * 512.), 1, 65535));
}

class VoiceEngine {
public:
    Upd933 chip;
    dac::Stage output;
    std::array<bus::Gate, 8> gates{};
    double phase = 0;

    void reset() {
        chip.reset(); output.reset(); gates = {}; phase = 0;
        pitches_.fill(-1); shapes_.fill(-1); waves_.fill(-1);
    }
    float process(const std::array<Input, 8>& in, int channels, const Parameters& params,
                  uint8_t xorMask, double hostRate, bool clean = true) {
        if (!std::isfinite(hostRate) || hostRate < 1000.) hostRate = 48000.;
        channels = std::clamp(channels, 0, 8);
        int wave = std::clamp(params.wave, 0, 7), window = std::clamp(params.window, 0, 7);
        int waveReg = (wave << 13) | (window << 6);
        for (int n = 0; n < 8; ++n) {
            bool before = gates[n].high;
            float gate = n < channels ? in[n].gate : 0.f;
            bool rising = gates[n].process(gate);
            bool falling = before && !gates[n].high;
            if (rising) {
                chip.write(uint8_t(0x98 + n), 0); // restart phase
                chip.write(uint8_t(n), uint16_t((rateForMilliseconds(params.attackMs) << 8) | 0x80 | 127));
            }
            if (falling)
                chip.write(uint8_t(n), uint16_t(0x8000 | (rateForMilliseconds(params.releaseMs) << 8) | 0x80));
            if (!gates[n].high) continue;
            int pitch = pitchRegister(in[n].pitch);
            if (pitch != pitches_[n]) { chip.write(uint8_t(0x60 + n), uint16_t(pitch)); pitches_[n] = pitch; }
            if (waveReg != waves_[n]) { chip.write(uint8_t(0x68 + n), uint16_t(waveReg)); waves_[n] = waveReg; }
            float cv = std::isfinite(in[n].dcw) ? in[n].dcw : 0.f;
            float base = std::isfinite(params.shape) ? params.shape : 0.f;
            int shape = int(std::lround(std::clamp(base + 12.7f * cv, 0.f, 127.f)));
            if (shape != shapes_[n]) {
                bool down = (chip.dcw[n].current >> Upd933::dcwShift) > unsigned(shape * 8);
                chip.write(uint8_t(0x20 + n), uint16_t((down ? 0x8000 : 0) | (127 << 8) | 0x80 | shape));
                shapes_[n] = shape;
            }
        }
        phase += Upd933::sampleRate / hostRate;
        int count = int(std::floor(phase)); phase -= count;
        for (int i = 0; i < count; ++i) {
            float x = float(chip.render()) / 32768.f;
            output.deviceSample(x, dac::Profile::Casio, 12, xorMask, params.tracking, Upd933::sampleRate, hostRate);
        }
        output.tap.hostSample();
        return output.reconstruct(clean, phase, Upd933::sampleRate, hostRate);
    }
private:
    std::array<int, 8> pitches_{{-1,-1,-1,-1,-1,-1,-1,-1}};
    std::array<int, 8> shapes_{{-1,-1,-1,-1,-1,-1,-1,-1}};
    std::array<int, 8> waves_{{-1,-1,-1,-1,-1,-1,-1,-1}};
};
} // namespace dspemu::cz
