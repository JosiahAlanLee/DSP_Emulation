// SPDX-License-Identifier: BSD-3-Clause
// Original percussion models. No manufacturer firmware, samples or algorithms.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace drumlab {
constexpr double pi = 3.14159265358979323846;
constexpr int channels = 16;
enum class Machine { Kick, Snare, Hat, FM, Membrane, Clap };
constexpr int machines = 6;
constexpr const char* names[machines] = {"Kick", "Snare", "Hat", "FM", "Membrane", "Clap"};

inline double bounded(double x, double lo, double hi, double fallback) {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
inline double rate(double sr) { return bounded(sr, 8000, 384000, 48000); }
inline uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu;
    return (x ^ (x >> 16)) | 1u;
}

// A hit owns a copy of these values. Changing a knob/CV after trigger() cannot
// retune, reshape or requantize that hit. Decay is time to -60 dB in seconds.
struct Hit {
    Machine machine = Machine::Kick;
    double pitch = 0;             // octaves relative to the model's base frequency
    double decay = .35;
    double color = .5;
    double contour = .5;
    double drive = .15;
    double bits = 16;
    double reduction = 1;         // held host samples, 1 = no rate reduction
    double velocity = 1;
    Hit sanitized() const {
        Hit p = *this;
        p.machine = Machine(std::clamp(int(machine), 0, machines - 1));
        p.pitch = bounded(pitch, -4, 4, 0);
        p.decay = bounded(decay, .005, 4, .35);
        p.color = bounded(color, 0, 1, .5);
        p.contour = bounded(contour, 0, 1, .5);
        p.drive = bounded(drive, 0, 1, .15);
        p.bits = std::round(bounded(bits, 4, 16, 16));
        p.reduction = std::round(bounded(reduction, 1, 64, 1));
        p.velocity = bounded(velocity, 0, 1, 0);
        return p;
    }
};

struct Voice {
    Hit hit;
    bool active = false;
    std::array<double, 6> phase{};
    std::array<double, 6> modeAmp{};
    std::array<double, 6> modeDecay{};
    double sr = 48000, age = 0, amp = 0, ampDecay = 0, pitchEnv = 1, pitchDecay = 0;
    double noiseLow = 0, noiseHighLow = 0, lowCoeff = 0, highCoeff = 0;
    double fmFeedback = 0, held = 0;
    int holdRemaining = 0;
    uint32_t rng = 1;

    void coefficients(double sampleRate) {
        sr = rate(sampleRate);
        ampDecay = std::exp(-std::log(1000.) / (hit.decay * sr));
        pitchDecay = std::exp(-1. / ((.008 + .065 * hit.color) * sr));
        lowCoeff = 1 - std::exp(-2 * pi * std::min(sr * .4, 1400 + 13000 * hit.color) / sr);
        highCoeff = 1 - std::exp(-2 * pi * std::min(sr * .4, hit.machine == Machine::Hat ? 1800. : 700.) / sr);
        for (int k = 0; k < 6; ++k)
            modeDecay[k] = std::exp(-std::log(1000.) * (1 + k * (.17 + .55 * hit.color)) / (hit.decay * sr));
    }
    void start(const Hit& p, uint32_t seed, double sampleRate) {
        *this = Voice{};
        hit = p.sanitized();
        active = hit.velocity > 0;
        amp = active ? 1 : 0;
        modeAmp.fill(1);
        rng = hash(seed);
        coefficients(sampleRate);
    }
    double noise() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return double(rng) / 2147483648. - 1.;
    }
    double oscillator(int i, double frequency) {
        // Keep every component below Nyquist even at extreme tuning. This does
        // not claim alias-free FM or clipping; digital sidebands are intentional.
        frequency = std::clamp(frequency, 5., sr * .45);
        double value = std::sin(2 * pi * phase[i]);
        phase[i] += frequency / sr;
        phase[i] -= std::floor(phase[i]);
        return value;
    }
    double tick(double sampleRate) {
        if (!active) return 0;
        if (rate(sampleRate) != sr) coefficients(sampleRate);
        if (amp < .00001) { active = false; return 0; }
        double white = noise();
        noiseLow += lowCoeff * (white - noiseLow);
        noiseHighLow += highCoeff * (noiseLow - noiseHighLow);
        double brightNoise = noiseLow - noiseHighLow;
        static constexpr double base[machines] = {55, 185, 1100, 130, 160, 950};
        double f = base[int(hit.machine)] * std::exp2(hit.pitch);
        double x = 0;
        switch (hit.machine) {
        case Machine::Kick: {
            double bend = std::exp2(4 * hit.contour * pitchEnv);
            double body = oscillator(0, f * bend);
            x = .85 * body + .3 * hit.color * brightNoise * pitchEnv * pitchEnv;
            break;
        }
        case Machine::Snare: {
            double body = .65 * oscillator(0, f) + .35 * oscillator(1, f * 1.47);
            double blend = .25 + .7 * hit.contour;
            x = (1 - blend) * body + blend * 2 * brightNoise;
            break;
        }
        case Machine::Hat: {
            static constexpr double ratios[6] = {1, 1.342, 1.814, 2.197, 2.697, 3.518};
            double metal = 0;
            for (int k = 0; k < 6; ++k)
                metal += oscillator(k, f * ratios[k] * (1 + .15 * hit.color * k));
            x = .28 * (1 - hit.contour) * metal + 1.7 * (.2 + hit.contour) * brightNoise;
            break;
        }
        case Machine::FM: {
            double mod = oscillator(1, f * (1 + 11 * hit.color));
            double index = (.1 + 9 * hit.contour) * pitchEnv;
            x = .8 * std::sin(2 * pi * phase[0] + index * mod + hit.color * fmFeedback);
            fmFeedback = x;
            oscillator(0, f * (1 + .15 * hit.contour * pitchEnv));
            break;
        }
        case Machine::Membrane: {
            static constexpr double ratios[6] = {1, 1.593, 2.136, 2.296, 2.653, 2.918};
            for (int k = 0; k < 6; ++k) {
                double weight = k == 0 ? .55 : (.15 + .45 * hit.contour) / (k + 1);
                // Each mode has its own damping; skip the common envelope below
                // rather than applying it twice to this bank.
                x += weight * modeAmp[k] * oscillator(k, f * ratios[k] * (1 + .035 * hit.color * k));
                modeAmp[k] *= modeDecay[k];
            }
            x += .12 * hit.color * brightNoise * pitchEnv * amp;
            break;
        }
        case Machine::Clap: {
            double spacing = .006 + .012 * hit.color;
            double bursts = 0;
            for (int k = 0; k < 3; ++k) {
                double t = age - k * spacing;
                if (t >= 0) bursts += std::exp(-t / .0035);
            }
            double tail = age >= 2 * spacing ? .3 + .65 * hit.contour : 0;
            x = 2 * brightNoise * (bursts + tail);
            break;
        }
        }
        if (hit.machine != Machine::Membrane) x *= amp;
        // Fast host-side attack avoids a random first noise sample discontinuity.
        x *= std::min(1., age / .0004);
        if (hit.drive > 0) {
            double gain = 1 + 12 * hit.drive;
            x = std::tanh(x * gain) / gain;
            x *= 1 + 2 * hit.drive;
        }
        if (holdRemaining == 0) {
            double steps = std::exp2(hit.bits - 1);
            held = std::round(std::clamp(x, -1., 1. - 1. / steps) * steps) / steps;
            holdRemaining = int(hit.reduction);
        }
        --holdRemaining;
        amp *= ampDecay;
        pitchEnv *= pitchDecay;
        age += 1 / sr;
        return held * hit.velocity;
    }
};

struct Lane {
    Voice voice, tail;
    double fade = 0;
    uint32_t hits = 0;
    void trigger(const Hit& p, uint32_t seed, double sr) {
        if (p.sanitized().velocity == 0) return;
        // A lane is monophonic with a 1 ms retrigger crossfade. The outgoing
        // voice keeps its own hit settings while it fades.
        tail = voice;
        fade = tail.active ? 1 : 0;
        voice.start(p, hash(seed ^ hits++), sr);
    }
    void choke() { tail = voice; fade = tail.active ? 1 : 0; voice.active = false; }
    double tick(double sr) {
        double outgoing = fade > 0 ? tail.tick(sr) : 0;
        double incoming = voice.tick(sr);
        double value = incoming * (1 - fade) + outgoing * fade;
        fade = std::max(0., fade - 1 / (.001 * rate(sr)));
        if (fade == 0) tail.active = false;
        return value;
    }
};

struct Engine {
    std::array<Lane, channels> lane{};
    uint32_t seed = 0x6472756du;
    void reset() { lane = {}; }
    void clear(int channel) { if (channel >= 0 && channel < channels) lane[channel] = {}; }
    void trigger(int channel, const Hit& p, double sr) {
        if (channel >= 0 && channel < channels) lane[channel].trigger(p, hash(seed ^ uint32_t(channel)), sr);
    }
    void choke(int channel) { if (channel >= 0 && channel < channels) lane[channel].choke(); }
    double tick(int channel, double sr) {
        return channel >= 0 && channel < channels ? lane[channel].tick(sr) : 0;
    }
};

// A high gate at startup is a hit. NaN/Inf behaves as a disconnected low gate.
struct Edge {
    bool high = false;
    bool process(double v) {
        v = bounded(v, -100, 100, 0);
        bool before = high;
        if (v >= 2) high = true;
        else if (v <= .1) high = false;
        return high && !before;
    }
};
} // namespace drumlab
