// SPDX-License-Identifier: BSD-3-Clause
// Initial device-word quantizers and Harriot tap. The Yamaha encoding follows
// ymfm (copyright Aaron Giles); see CZishLab/THIRD_PARTY_NOTICES.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "RateConverter.hpp"

namespace dspemu::dac {
enum class Profile { Yamaha = 0, Casio = 1, Linear = 2 };
enum class CzModel { Instant = 0, Syllabic = 1 };
struct Word {
    uint16_t code = 0;
    uint8_t width = 0;
    int16_t value = 0;
    uint8_t high() const { return width >= 8 ? uint8_t(code >> (width - 8)) : uint8_t(code << (8 - width)); }
};

inline int32_t floorShift(int32_t v, unsigned n) {
    if (!n)
        return v;
    if (v >= 0)
        return v / int32_t(1u << n);
    return -int32_t((uint32_t(-int64_t(v)) + (1u << n) - 1) >> n);
}
inline int16_t clip16(int64_t v) { return int16_t(std::clamp<int64_t>(v, -32768, 32767)); }

// ymfm's sign-inverted, 3-exponent/10-mantissa format. For a positive
// overflow ymfm's roundtrip returns 32767 even though its encoded maximum
// decodes to 32704; preserve that documented distinction when no mask is used.
inline uint16_t yamahaEncode(int32_t v) {
    if (v < -32768)
        return 7u << 10;
    if (v > 32767)
        return (7u << 10) | 0x3ffu;
    uint32_t scan = uint32_t(v < 0 ? ~v : v);
    unsigned exponent = 1;
    while (scan >= 512 && exponent < 7) {
        scan >>= 1;
        ++exponent;
    }
    int32_t mantissa = floorShift(v, exponent - 1);
    return uint16_t(((exponent << 10) | (uint32_t(mantissa) & 0x3ffu)) ^ 0x200u);
}
inline int16_t yamahaDecode(uint16_t code) {
    unsigned exponent = (code >> 10) & 7;
    int32_t mantissa = (code ^ 0x200u) & 0x3ffu;
    if (mantissa & 0x200)
        mantissa -= 0x400;
    return exponent ? clip16(int64_t(mantissa) * (1u << (exponent - 1))) : int16_t(floorShift(mantissa, 1));
}
inline int16_t yamahaRoundtrip(int32_t v) {
    if (v < -32768)
        return -32768;
    if (v > 32767)
        return 32767;
    return yamahaDecode(yamahaEncode(v));
}

// An explicitly provisional 12-bit mantissa + two-bit shift model, NOT a
// verified CZ-101 compander. Invalid shift 3, possible through the bus XOR,
// decodes as shift 2 so every byte-bus word has a defined, bounded result.
inline uint16_t casioEncode(int32_t input) {
    int32_t value = floorShift(std::clamp(input, -32768, 32767), 2);
    unsigned shift = 0;
    while ((floorShift(value, shift) < -2048 || floorShift(value, shift) > 2047) && shift < 2)
        ++shift;
    return uint16_t((shift << 12) | (uint32_t(floorShift(value, shift)) & 0xfffu));
}
inline int16_t casioDecode(uint16_t code, float tracking = 0.f) {
    unsigned shift = std::min(2u, unsigned((code >> 12) & 3));
    int32_t mantissa = code & 0xfff;
    if (mantissa & 0x800)
        mantissa -= 0x1000;
    double scale = std::isfinite(tracking) ? std::clamp(double(tracking), -.5, .5) : 0.;
    return clip16(std::lround(mantissa * double(1u << shift) * 4. * (1. + scale * shift)));
}
inline uint16_t linearEncode(int32_t input, int bits, bool nearest = false) {
    bits = std::clamp(bits, 4, 16);
    int32_t v = std::clamp(input, -32768, 32767);
    int step = 1 << (16 - bits);
    int32_t code = nearest ? (v < 0 ? -int32_t((uint32_t(-v) + step / 2) / step)
                                    : int32_t((uint32_t(v) + step / 2) / step))
                           : floorShift(v, 16 - bits);
    code = std::clamp(code, -(1 << (bits - 1)), (1 << (bits - 1)) - 1);
    return uint16_t(uint32_t(code) & ((1u << bits) - 1));
}
inline int16_t linearDecode(uint16_t code, int bits) {
    bits = std::clamp(bits, 4, 16);
    int32_t mask = (1 << bits) - 1;
    int32_t value = code & mask;
    if (value & (1 << (bits - 1)))
        value -= 1 << bits;
    return clip16(int64_t(value) * (1 << (16 - bits)));
}

// XOR acts on the exposed high byte of the encoded word, before decoding.
// If the word is shorter than eight bits, the unused low mask bits are ignored.
inline Word quantize(Profile profile, int32_t value, int bits = 12, uint8_t xorMask = 0, float track = 0.f,
                     bool nearest = false) {
    Word w;
    switch (profile) {
    case Profile::Yamaha:
        w.width = 13;
        w.code = yamahaEncode(value);
        break;
    case Profile::Casio:
        w.width = 14;
        w.code = casioEncode(value);
        break;
    default:
        w.width = uint8_t(std::clamp(bits, 4, 16));
        w.code = linearEncode(value, w.width, nearest);
        break;
    }
    if (xorMask) {
        w.code ^= w.width >= 8 ? uint16_t(xorMask) << (w.width - 8) : uint16_t(xorMask) >> (8 - w.width);
    }
    switch (profile) {
    case Profile::Yamaha:
        w.value = xorMask ? yamahaDecode(w.code) : yamahaRoundtrip(value);
        break;
    case Profile::Casio:
        w.value = casioDecode(w.code, track);
        break;
    default:
        w.value = linearDecode(w.code, w.width);
        break;
    }
    return w;
}

struct CzCompander {
    double peak = 0, cachedRate = 0, attack = 0, release = 0;
    void reset() {
        peak = 0;
        cachedRate = 0;
    }
    Word process(int32_t input, CzModel model, float tracking, uint8_t mask, double sampleRate) {
        if (model == CzModel::Instant)
            return quantize(Profile::Casio, input, 12, mask, tracking);
        if (cachedRate != sampleRate) {
            cachedRate = sampleRate;
            attack = std::exp(-1. / (.001 * sampleRate));
            release = std::exp(-1. / (.050 * sampleRate));
        }
        double magnitude = std::abs(double(std::clamp(input, -32768, 32767)));
        double coefficient = magnitude > peak ? attack : release;
        peak = magnitude + (peak - magnitude) * coefficient;
        unsigned shift = peak > 16380 ? 2 : (peak > 8188 ? 1 : 0);
        int mantissa = std::clamp(floorShift(std::clamp(input, -32768, 32767), 2 + shift), -2048, 2047);
        Word w;
        w.width = 14;
        w.code = uint16_t((shift << 12) | (uint32_t(mantissa) & 4095));
        w.code ^= uint16_t(mask) << 6;
        w.value = casioDecode(w.code, tracking);
        return w;
    }
};

struct Settings {
    Profile profile = Profile::Yamaha;
    double deviceRate = 3579545. / 64., ymClockHz = 3579545.;
    int bits = 12;
    bool roundNearest = false;
    CzModel czModel = CzModel::Instant;
    float czTrackPct = 0;
    bool aa = false, hold = false;
    float driveDb = 0, toneHz = 24000, noiseDb = -120, levelDb = 0;
};

// Nine-channel byte-bus output. A producer clock cannot pulse faster than
// hostRate/2: the protocol requires a low sample after every high sample.
// Sample every Nth device word, N=ceil(2*deviceRate/hostRate). Changes of rate
// can produce a pending request on the low-gap sample; coalesce it to the most
// recent word and publish at the next permitted sample.
struct BusTap {
    uint8_t word = 0, pendingWord = 0;
    bool clk = false, pending = false;
    unsigned count = 0, divider = 1;
    void reset() {
        word = pendingWord = 0;
        clk = pending = false;
        count = 0;
        divider = 1;
    }
    void deviceSample(uint8_t value, double deviceRate, double hostRate) {
        if (!std::isfinite(deviceRate) || !std::isfinite(hostRate) || hostRate <= 0)
            return;
        divider = unsigned(std::max(1., std::ceil(2. * std::max(0., deviceRate) / hostRate)));
        if (++count >= divider) {
            count = 0;
            pendingWord = value;
            pending = true;
        }
    }
    void hostSample() {
        if (clk) {
            clk = false;
            return;
        }
        if (pending) {
            word = pendingWord;
            pending = false;
            clk = true;
        }
    }
};

using SincBank = rate::SincBank;
using SincHistory = rate::SincHistory;
inline const SincBank &sincBank() { return rate::sincBank(); }

// Host-rate sampling plus the device-rate entry used by the CZ voice. CLEAN
// uses the sinc converter; HOLD is a raw unbandlimited hold, not yet the
// draft's minimum-phase BLEP. No allocations occur in either processing path.
struct Stage {
    double phase = 0.;
    float previous = 0.f, held = 0.f;
    bool initialized = false;
    Word word{};
    BusTap tap{};
    rate::InputConverter inputFilter{};
    SincHistory outputFilter{};
    rate::BlepHold blep{};
    CzCompander compander{};
    CzModel czModel = CzModel::Instant;
    bool nearest = false;
    bool renderHold = true, lastAA = false;
    float noiseDb = -120;
    uint64_t noiseState = 0x444143, noiseSeed = 0x444143;
    float noiseGain = 0, noiseGainDb = -1000;
    std::array<float, 4> hermite{};
    Settings settings{};
    double configuredHost = 48000;
    float toneA = 0, toneB = 0;
    Stage() { (void)sincBank(); }
    void reset() {
        phase = 0.;
        previous = held = 0.f;
        initialized = false;
        word = {};
        tap.reset();
        inputFilter.reset();
        outputFilter.reset();
        blep.reset();
        compander.reset();
        hermite = {};
        noiseState = noiseSeed;
        toneA = toneB = 0;
        lastAA = false;
    }
    float deviceSample(float value, Profile profile, int bits, uint8_t xorMask, float tracking,
                       double deviceRate, double hostRate, double fraction = 1.) {
        double x = std::isfinite(value) ? double(value) : 0.;
        x = std::clamp(x, -16., 16.);
        int32_t input = int32_t(std::floor(x * 32768.));
        word = profile == Profile::Casio ? compander.process(input, czModel, tracking, xorMask, deviceRate)
                                         : quantize(profile, input, bits, xorMask, tracking, nearest);
        float before = held;
        held = float(word.value) / 32768.f;
        if (noiseDb > -119.9f) {
            if (noiseDb != noiseGainDb) {
                noiseGainDb = noiseDb;
                noiseGain = std::pow(10.f, noiseDb / 20.f);
            }
            uint64_t z = (noiseState += 0x9e3779b97f4a7c15ULL);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            z ^= z >> 31;
            held += float((double(z >> 11) / 9007199254740992. - .5) * 3.4641016151377546) * noiseGain;
        }
        if (renderHold)
            blep.transition(held - before, fraction);
        outputFilter.push(held);
        tap.deviceSample(word.high(), deviceRate, hostRate);
        return held;
    }
    float reconstruct(bool clean, double fraction, double deviceRate, double hostRate) {
        float hold = blep.read(held);
        return clean ? outputFilter.read(fraction, hostRate / deviceRate) : hold;
    }
    float process(float in, Profile profile, int bits, uint8_t xorMask, float tracking, double deviceRate,
                  double hostRate, bool aa = false, bool clean = false) {
        if (!std::isfinite(in))
            in = 0;
        if (!std::isfinite(hostRate) || hostRate < 1000.)
            hostRate = 48000.;
        if (!std::isfinite(deviceRate))
            deviceRate = 40000.;
        deviceRate = std::clamp(deviceRate, 1000., 192000.);
        if (!initialized) {
            previous = in;
            initialized = true;
        }
        renderHold = !clean;
        if (aa && !lastAA)
            inputFilter.reset();
        lastAA = aa;
        if (aa)
            inputFilter.push(in);
        for (int i = 0; i < 3; ++i)
            hermite[i] = hermite[i + 1];
        hermite[3] = in;
        double step = deviceRate / hostRate;
        double total = phase + step;
        int ticks = int(std::floor(total));
        for (int n = 1; n <= ticks; ++n) {
            double f = std::clamp((n - phase) / step, 0., 1.);
            float t = float(f), a = hermite[1], b = .5f * (hermite[2] - hermite[0]);
            float c = hermite[0] - 2.5f * hermite[1] + 2.f * hermite[2] - .5f * hermite[3];
            float d = .5f * (hermite[3] - hermite[0]) + 1.5f * (hermite[1] - hermite[2]);
            float x = aa ? inputFilter.read(f, deviceRate, hostRate) : ((d * t + c) * t + b) * t + a;
            deviceSample(x, profile, bits, xorMask, tracking, deviceRate, hostRate, f);
        }
        phase = total - ticks;
        previous = in;
        tap.hostSample();
        return reconstruct(clean, phase, deviceRate, hostRate);
    }
    void configure(double host, const Settings &s) {
        configuredHost = std::max(1000., host);
        settings = s;
        czModel = s.czModel;
        nearest = s.roundNearest;
        noiseDb = s.noiseDb;
    }
    float finish(float x, double host, const Settings &s) {
        if (s.toneHz < 23999) {
            float a = 1.f - std::exp(float(-2. * rate::pi * std::max(1000.f, s.toneHz) / host));
            toneA += a * (x - toneA);
            toneB += a * (toneA - toneB);
            x = toneB;
        } else
            toneA = toneB = x;
        return x * std::pow(10.f, s.levelDb / 20.f);
    }
    float process(float in) {
        return finish(process(in * std::pow(10.f, settings.driveDb / 20.f), settings.profile, settings.bits,
                              0, settings.czTrackPct * .01f, settings.deviceRate, configuredHost, settings.aa,
                              !settings.hold),
                      configuredHost, settings);
    }
};

struct DeviceOutput {
    Stage stage;
    Settings settings{};
    double deviceRate = 40000, hostRate = 48000;
    void configure(double device, double host, const Settings &s) {
        deviceRate = device;
        hostRate = host;
        settings = s;
        stage.configure(host, s);
    }
    // fraction is the event position in the current host interval, in [0,1].
    // The caller schedules native samples; it must supply their actual times.
    void push(int32_t sample, double fraction, uint8_t mask = 0) {
        stage.deviceSample(float(sample) / 32768.f, settings.profile, settings.bits, mask,
                           settings.czTrackPct * .01f, deviceRate, hostRate, fraction);
    }
    // fraction is the host time's phase after the most recent device sample.
    float read(double fraction) {
        stage.tap.hostSample();
        return stage.finish(stage.reconstruct(!settings.hold, fraction, deviceRate, hostRate), hostRate,
                            settings);
    }
};
inline int16_t ymRoundTrip(int32_t v) { return yamahaRoundtrip(v); }
inline int16_t linearRoundTrip(int32_t v, int bits, bool nearest) {
    return linearDecode(linearEncode(v, bits, nearest), bits);
}
} // namespace dspemu::dac
