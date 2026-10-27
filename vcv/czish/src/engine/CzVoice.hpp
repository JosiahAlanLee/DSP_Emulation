// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "Upd933.hpp"
#include "Tone.hpp"
#include "../../../shared/DacStage.hpp"
#include "../../../shared/ByteBus.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace dspemu::cz {
struct Input {
    float pitch = 0, gate = 0, dcw = 0, vib = 0, bend = 0;
};
struct Parameters {
    int wave = 0, window = 0;
    float shape = 32, attackMs = 2, releaseMs = 250, tracking = 0;
};
inline uint8_t rateForMilliseconds(float ms) {
    double wanted = double(127u << 18) / (std::clamp(double(std::isfinite(ms) ? ms : 2), 2., 4000.) * 40.);
    int best = 0;
    double distance = 1e100;
    for (int i = 0; i < 128; ++i) {
        double d = std::abs(double(Upd933::envelopeRate(uint8_t(i))) - wanted);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return uint8_t(best);
}
inline uint16_t pitchRegister(float volts, float tuning = 440) {
    if (!std::isfinite(volts))
        volts = 0;
    double note = 62. +
                  12. * std::log2(261.6255653005986 / 442. * std::clamp(double(tuning), 400., 480.) / 440.) +
                  12. * std::clamp(double(volts), -8., 8.);
    return uint16_t(std::clamp<long>(std::lround(note * 512.), 1, 65535));
}
struct RenderSettings {
    float depth = 127, tracking = 0, noiseDb = -120;
    dac::CzModel czModel = dac::CzModel::Instant;
    bool polyEnabled = true;
};
class VoiceEngine {
  public:
    Upd933 chip;
    dac::Stage output;
    std::array<dac::Stage, 8> poly{};
    std::array<float, 8> polySamples{};
    std::array<bus::Gate, 8> gates{};
    struct Run {
        int step = 0;
        bool done = true;
    };
    std::array<std::array<Run, 3>, 8> runs{};
    double phase = 0;
    uint64_t deviceFrames = 0;
    int activeChannels = 0;
    static constexpr int first[4] = {5, 7, 1, 3}, second[4] = {0, 2, 4, 6};
    int outputChannels(const Tone &tone) const {
        int count = activeChannels;
        for (int slot = 0; slot < noteLimit(tone); ++slot) {
            bool active = false;
            forEachVoice(tone, slot,
                         [&](int n, int) { active |= !runs[n][2].done || chip.dca[n].current != 0; });
            if (active)
                count = std::max(count, slot + 1);
        }
        return std::max(1, count);
    }
    void reset() {
        chip.reset();
        output.reset();
        for (size_t c = 0; c < poly.size(); ++c) {
            poly[c].noiseSeed = 0x933 + 127 * c;
            poly[c].reset();
        }
        polySamples = {};
        gates = {};
        runs = {};
        lastInput_ = {};
        phase = 0;
        deviceFrames = 0;
        mode_ = -1;
        ages_ = {};
        vibPhase_ = {};
        pitchInitialized_ = {};
        activeChannels = 0;
    }
    float process(const std::array<Input, 8> &incoming, int channels, const Tone &tone, uint8_t mask,
                  double host, bool clean = true, const RenderSettings &settings = {}) {
        if (!std::isfinite(host) || host < 1000)
            host = 48000;
        if (mode_ != tone.mode) {
            reset();
            mode_ = tone.mode;
        }
        auto in = incoming;
        for (int c = 0; c < 8; ++c) {
            if (c >= channels) {
                in[c] = lastInput_[c];
                in[c].gate = 0;
            } else {
                auto finite = [](float x) { return std::isfinite(x) ? x : 0.f; };
                in[c] = {finite(in[c].pitch), finite(in[c].gate), finite(in[c].dcw), finite(in[c].vib),
                         finite(in[c].bend)};
                lastInput_[c] = in[c];
            }
        }
        activeChannels = std::min(std::clamp(channels, 0, 8), noteLimit(tone));
        if (settings.polyEnabled && !hadPoly_) {
            for (auto &p : poly)
                p.reset();
        }
        hadPoly_ = settings.polyEnabled;
        output.renderHold = !clean;
        for (auto &p : poly)
            p.renderHold = !clean;
        for (int slot = 0; slot < noteLimit(tone); ++slot) {
            bool before = gates[slot].high;
            bool rising = gates[slot].process(slot < activeChannels ? in[slot].gate : 0);
            bool falling = before && !gates[slot].high;
            if (rising) {
                ages_[slot] = 0;
                vibPhase_[slot] = 0;
                forEachVoice(tone, slot, [&](int n, int line) {
                    chip.write(uint8_t(0x98 + n), 0);
                    chip.dca[n].current = chip.dcw[n].current = chip.dco[n].current = 0;
                    for (int e = 0; e < 3; ++e) {
                        runs[n][e] = {0, false};
                        writeSegment(n, line, e, 0, tone, in[slot].pitch);
                    }
                });
            }
            if (falling)
                forEachVoice(tone, slot, [&](int n, int line) {
                    for (int e = 0; e < 3; ++e) {
                        const auto &env = tone.line[line].env[e];
                        if (env.sustain >= 0 && env.sustain <= env.end && !runs[n][e].done) {
                            int next = std::min(env.end, env.sustain + 1);
                            runs[n][e] = {next, false};
                            writeSegment(n, line, e, next, tone, in[slot].pitch);
                        }
                    }
                });
        }
        // Gate-time writes, and pitch changes, are visible before the next chip sample.
        updateControls(in, tone, settings, false);
        double before = phase, step = Upd933::sampleRate / host, total = before + step;
        int count = int(std::floor(total));
        phase = total - count;
        for (int i = 1; i <= count; ++i) {
            if (!(deviceFrames & 63))
                updateControls(in, tone, settings, true);
            double f = std::clamp((i - before) / step, 0., 1.);
            int16_t summed = chip.render();
            ++deviceFrames;
            for (int slot = 0; slot < noteLimit(tone); ++slot) {
                ++ages_[slot];
                forEachVoice(tone, slot, [&](int n, int line) {
                    for (int e = 0; e < 3; ++e) {
                        auto &run = runs[n][e];
                        if (run.done)
                            continue;
                        auto &state = chipEnvelope(n, e);
                        const auto &env = tone.line[line].env[e];
                        if (state.current != state.target)
                            continue;
                        if (run.step >= env.end) {
                            run.done = true;
                            continue;
                        }
                        if (run.step == env.sustain && gates[slot].high)
                            continue;
                        ++run.step;
                        writeSegment(n, line, e, run.step, tone, in[slot].pitch);
                    }
                });
            }
            setOutput(output, settings);
            output.deviceSample(float(summed) / 32768.f, dac::Profile::Casio, 12, mask, settings.tracking,
                                40000, host, f);
            if (settings.polyEnabled)
                for (int slot = 0; slot < noteLimit(tone); ++slot) {
                    int value = 0;
                    forEachVoice(tone, slot, [&](int n, int) { value += chip.lastVoices[n]; });
                    setOutput(poly[slot], settings);
                    poly[slot].deviceSample(float(value) / 32768.f, dac::Profile::Casio, 12, mask,
                                            settings.tracking, 40000, host, f);
                }
        }
        output.tap.hostSample();
        if (settings.polyEnabled)
            for (int slot = 0; slot < noteLimit(tone); ++slot)
                polySamples[slot] = poly[slot].reconstruct(clean, phase, 40000, host);
        return output.reconstruct(clean, phase, 40000, host);
    }
    // Source-compatible entry for the first prototype's tests/clients.
    float process(const std::array<Input, 8> &in, int channels, const Parameters &p, uint8_t mask,
                  double host, bool clean = true) {
        Tone t;
        t.line[0].wave = p.wave;
        t.line[0].window = p.window;
        t.line[0].env[1].level[0] = 99;
        t.line[0].env[2].rate[0] = fromCode(EnvKind::Dca, rateForMilliseconds(p.attackMs), true);
        t.line[0].env[2].rate[1] = fromCode(EnvKind::Dca, rateForMilliseconds(p.releaseMs), true);
        return process(in, channels, t, mask, host, clean, {p.shape, p.tracking});
    }

  private:
    int mode_ = -1;
    bool hadPoly_ = false;
    std::array<uint64_t, 8> ages_{};
    std::array<double, 8> vibPhase_{}, glide_{};
    std::array<bool, 8> pitchInitialized_{};
    std::array<Input, 8> lastInput_{};
    template <class F> static void forEachVoice(const Tone &t, int slot, F f) {
        if (!paired(t)) {
            f(slot, t.mode == 1 ? 1 : 0);
            return;
        }
        f(first[slot], 0);
        f(second[slot], t.mode == 2 ? 0 : 1);
    }
    Upd933::Envelope &chipEnvelope(int n, int e) {
        return e == 0 ? chip.dco[n] : (e == 1 ? chip.dcw[n] : chip.dca[n]);
    }
    void writeSegment(int n, int line, int e, int step, const Tone &tone, float pitch) {
        const auto &env = tone.line[line].env[e];
        EnvKind kind = EnvKind(e);
        int level = levelCode(kind, step == env.end ? 0 : env.level[step]);
        int rate = rateCode(kind, env.rate[step]);
        if (e == 2 && tone.line[line].dcaKey) {
            double speed = std::exp2(tone.line[line].dcaKey * std::max(0., double(pitch) * 12.) / 72.);
            double target = Upd933::envelopeRate(uint8_t(rate)) * speed;
            while (rate < 127 && Upd933::envelopeRate(uint8_t(rate)) < target)
                ++rate;
        }
        auto &state = chipEnvelope(n, e);
        uint32_t target = uint32_t(level & (e == 0 ? 63 : 127)) << (e == 0 ? 16 : 18);
        if (e == 0 && (level & 64))
            target <<= 5;
        bool down = state.current > target;
        uint16_t value = uint16_t((down ? 0x8000 : 0) | (rate << 8) | level);
        if (step == env.end || (step == env.sustain))
            value |= 128;
        chip.write(uint8_t((e == 0 ? 0x10 : (e == 1 ? 0x20 : 0)) + n), value);
    }
    static void setOutput(dac::Stage &out, const RenderSettings &p) {
        out.czModel = p.czModel;
        out.noiseDb = p.noiseDb;
    }
    void updateControls(const std::array<Input, 8> &in, const Tone &t, const RenderSettings &settings,
                        bool control) {
        std::array<uint16_t, 8> regs{};
        for (int slot = 0; slot < noteLimit(t); ++slot) {
            double pitch = std::clamp(double(in[slot].pitch), -8., 8.);
            if (!pitchInitialized_[slot] || !t.portamento) {
                glide_[slot] = pitch;
                pitchInitialized_[slot] = true;
            } else if (control) {
                double duration = .005 * std::exp2(t.portamentoTime / 10.);
                double delta = 64. / 40000 / duration;
                glide_[slot] += std::clamp(pitch - glide_[slot], -delta, delta);
            }
            double vibrato = 0;
            double delay = t.vibDelay == 0 ? 0 : .01 * std::exp2(t.vibDelay / 10.);
            if (t.vibrato && ages_[slot] >= uint64_t(delay * 40000)) {
                if (control)
                    vibPhase_[slot] += 64. / 40000 * (.1 * std::exp2(t.vibRate / 13.));
                double x = vibPhase_[slot] - std::floor(vibPhase_[slot]);
                double wave =
                    t.vibWave == 0
                        ? 1. - 4. * std::abs(x - .5)
                        : (t.vibWave == 1 ? 2 * x - 1 : (t.vibWave == 2 ? 1 - 2 * x : (x < .5 ? 1 : -1)));
                vibrato = wave * std::clamp(t.vibDepth + 9.9 * double(in[slot].vib), 0., 99.) / 99.;
            }
            forEachVoice(t, slot, [&](int n, int line) {
                const auto &l = t.line[line];
                regs[n] = waveRegister(l);
                double detune =
                    paired(t) && n == second[slot] ? t.detuneSign * (t.detune + t.fine / 60.) / 12. : 0;
                double volts = glide_[slot] + t.octave + detune + vibrato +
                               std::clamp(double(in[slot].bend) / 5., -1., 1.) * t.bendRange / 12.;
                uint16_t code = pitchRegister(float(volts), t.tuning);
                if (chip.voice[n].pitch != code)
                    chip.write(uint8_t(0x60 + n), code);
                int base = int(chip.dcw[n].current >> Upd933::dcwShift);
                double key = std::exp2(-l.dcwKey * std::max(0., pitch * 12.) / 72.);
                chip.dcwOffset[n] =
                    int(std::lround(base * (std::clamp(double(settings.depth), 0., 127.) / 127. * key - 1) +
                                    101.6 * in[slot].dcw));
            });
        }
        if (paired(t))
            for (int slot = 0; slot < 4; ++slot) {
                int bits = t.mod == 1 ? 32 : (t.mod == 2 ? 24 : 0);
                if (t.chipPitchMod)
                    bits = (bits & 32) | (std::clamp(t.chipPitchMod, 0, 3) << 3);
                if (t.muteOther)
                    bits |= 4;
                regs[(second[slot] + 2) & 7] |= uint16_t(bits);
            }
        for (int n = 0; n < 8; ++n)
            chip.write(uint8_t(0x68 + n), regs[n]);
    }
};
} // namespace dspemu::cz
