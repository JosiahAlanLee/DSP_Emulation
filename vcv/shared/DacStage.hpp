// SPDX-License-Identifier: BSD-3-Clause
// Initial device-word quantizers and Harriot tap. The Yamaha encoding follows
// ymfm (copyright Aaron Giles); see CZishLab/THIRD_PARTY_NOTICES.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dspemu::dac {
enum class Profile { Yamaha = 0, Casio = 1, Linear = 2 };
struct Word {
    uint16_t code = 0;
    uint8_t width = 0;
    int16_t value = 0;
    uint8_t high() const { return width >= 8 ? uint8_t(code >> (width - 8)) : uint8_t(code << (8 - width)); }
};

inline int32_t floorShift(int32_t v, unsigned n) {
    if (!n) return v;
    if (v >= 0) return v / int32_t(1u << n);
    return -int32_t((uint32_t(-int64_t(v)) + (1u << n) - 1) >> n);
}
inline int16_t clip16(int64_t v) { return int16_t(std::clamp<int64_t>(v, -32768, 32767)); }

// ymfm's sign-inverted, 3-exponent/10-mantissa format. For a positive
// overflow ymfm's roundtrip returns 32767 even though its encoded maximum
// decodes to 32704; preserve that documented distinction when no mask is used.
inline uint16_t yamahaEncode(int32_t v) {
    if (v < -32768) return 7u << 10;
    if (v > 32767) return (7u << 10) | 0x3ffu;
    uint32_t scan = uint32_t(v < 0 ? ~v : v);
    unsigned exponent = 1;
    while (scan >= 512 && exponent < 7) { scan >>= 1; ++exponent; }
    int32_t mantissa = floorShift(v, exponent - 1);
    return uint16_t(((exponent << 10) | (uint32_t(mantissa) & 0x3ffu)) ^ 0x200u);
}
inline int16_t yamahaDecode(uint16_t code) {
    unsigned exponent = (code >> 10) & 7;
    int32_t mantissa = (code ^ 0x200u) & 0x3ffu;
    if (mantissa & 0x200) mantissa -= 0x400;
    return exponent ? clip16(int64_t(mantissa) * (1u << (exponent - 1))) : int16_t(floorShift(mantissa, 1));
}
inline int16_t yamahaRoundtrip(int32_t v) {
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    return yamahaDecode(yamahaEncode(v));
}

// An explicitly provisional 12-bit mantissa + two-bit shift model, NOT a
// verified CZ-101 compander. Invalid shift 3, possible through the bus XOR,
// decodes as shift 2 so every byte-bus word has a defined, bounded result.
inline uint16_t casioEncode(int32_t input) {
    int32_t value = floorShift(std::clamp(input, -32768, 32767), 2);
    unsigned shift = 0;
    while ((floorShift(value, shift) < -2048 || floorShift(value, shift) > 2047) && shift < 2) ++shift;
    return uint16_t((shift << 12) | (uint32_t(floorShift(value, shift)) & 0xfffu));
}
inline int16_t casioDecode(uint16_t code, float tracking = 0.f) {
    unsigned shift = std::min(2u, unsigned((code >> 12) & 3));
    int32_t mantissa = code & 0xfff;
    if (mantissa & 0x800) mantissa -= 0x1000;
    double scale = std::isfinite(tracking) ? std::clamp(double(tracking), -.5, .5) : 0.;
    return clip16(std::lround(mantissa * double(1u << shift) * 4. * (1. + scale * shift)));
}
inline uint16_t linearEncode(int32_t input, int bits) {
    bits = std::clamp(bits, 4, 16);
    int32_t v = std::clamp(input, -32768, 32767);
    return uint16_t(uint32_t(floorShift(v, 16 - bits)) & ((1u << bits) - 1));
}
inline int16_t linearDecode(uint16_t code, int bits) {
    bits = std::clamp(bits, 4, 16);
    int32_t mask = (1 << bits) - 1;
    int32_t value = code & mask;
    if (value & (1 << (bits - 1))) value -= 1 << bits;
    return clip16(int64_t(value) * (1 << (16 - bits)));
}

// XOR acts on the exposed high byte of the encoded word, before decoding.
// If the word is shorter than eight bits, the unused low mask bits are ignored.
inline Word quantize(Profile profile, int32_t value, int bits = 12, uint8_t xorMask = 0, float track = 0.f) {
    Word w;
    switch (profile) {
    case Profile::Yamaha: w.width = 13; w.code = yamahaEncode(value); break;
    case Profile::Casio: w.width = 14; w.code = casioEncode(value); break;
    default: w.width = uint8_t(std::clamp(bits, 4, 16)); w.code = linearEncode(value, w.width); break;
    }
    if (xorMask) {
        w.code ^= w.width >= 8 ? uint16_t(xorMask) << (w.width - 8) : uint16_t(xorMask) >> (8 - w.width);
    }
    switch (profile) {
    case Profile::Yamaha: w.value = xorMask ? yamahaDecode(w.code) : yamahaRoundtrip(value); break;
    case Profile::Casio: w.value = casioDecode(w.code, track); break;
    default: w.value = linearDecode(w.code, w.width); break;
    }
    return w;
}

// Nine-channel byte-bus output. A producer clock cannot pulse faster than
// hostRate/2: the protocol requires a low sample after every high sample.
// Sample every Nth device word, N=ceil(2*deviceRate/hostRate). Changes of rate
// can produce a pending request on the low-gap sample; coalesce it to the most
// recent word and publish at the next permitted sample.
struct BusTap {
    uint8_t word = 0, pendingWord = 0;
    bool clk = false, pending = false;
    unsigned count = 0, divider = 1;
    void reset() { word = pendingWord = 0; clk = pending = false; count = 0; divider = 1; }
    void deviceSample(uint8_t value, double deviceRate, double hostRate) {
        if (!std::isfinite(deviceRate) || !std::isfinite(hostRate) || hostRate <= 0) return;
        divider = unsigned(std::max(1., std::ceil(2. * std::max(0., deviceRate) / hostRate)));
        if (++count >= divider) { count = 0; pendingWord = value; pending = true; }
    }
    void hostSample() {
        if (clk) { clk = false; return; }
        if (pending) { word = pendingWord; pending = false; clk = true; }
    }
};

// Precomputed causal polyphase Blackman-sinc kernels. The bank is built during
// module construction, not on the audio thread. Select the next narrower
// cutoff so changing RATE/CV does not require allocating or rebuilding a table.
// Very large ratios would need a half-band cascade for strong rejection; this
// first stage does not claim that part of the draft contract yet.
struct SincBank {
    static constexpr int taps = 64, phases = 256, cutoffs = 13;
    std::array<float, taps * phases * cutoffs> kernel{};
    SincBank() {
        constexpr double pi = 3.1415926535897932384626433832795;
        for (int b = 0; b < cutoffs; ++b) {
            double cutoff = .46 * std::exp2(-.5 * b);
            for (int p = 0; p < phases; ++p) {
                double fraction = double(p) / phases, sum = 0;
                for (int k = 0; k < taps; ++k) {
                    double d = k - (taps / 2. - 1) + fraction;
                    double window = .42 - .5 * std::cos(2 * pi * k / (taps - 1)) +
                                    .08 * std::cos(4 * pi * k / (taps - 1));
                    double h = std::abs(d) < 1e-12 ? 2 * cutoff : std::sin(2 * pi * cutoff * d) / (pi * d);
                    kernel[(b * phases + p) * taps + k] = float(h * window);
                    sum += h * window;
                }
                for (int k = 0; k < taps; ++k) kernel[(b * phases + p) * taps + k] /= float(sum);
            }
        }
    }
    const float* at(double fraction, double ratio) const {
        if (!std::isfinite(fraction)) fraction = 0;
        if (!std::isfinite(ratio)) ratio = 1;
        int p = std::clamp(int(std::clamp(fraction, 0., .999999999) * phases), 0, phases - 1);
        int b = std::clamp(int(std::ceil(-2 * std::log2(std::clamp(ratio, 1./64., 1.)))), 0, cutoffs - 1);
        return &kernel[(b * phases + p) * taps];
    }
};
inline const SincBank& sincBank() { static const SincBank bank; return bank; }

struct SincHistory {
    std::array<float, 2 * SincBank::taps> history{};
    unsigned head = 0;
    void reset() { history = {}; head = 0; }
    void push(float x) {
        head = (head + 1) % SincBank::taps;
        history[head] = history[head + SincBank::taps] = x;
    }
    float read(double fraction, double ratio) const {
        const float* signal = &history[head + SincBank::taps];
        const float* kernel = sincBank().at(fraction, ratio);
        float out = 0;
        for (int k = 0; k < SincBank::taps; ++k) out += signal[-k] * kernel[k];
        return out;
    }
};

// Host-rate sampling plus the device-rate entry used by the CZ voice. CLEAN
// uses the sinc converter; HOLD is a raw unbandlimited hold, not yet the
// draft's minimum-phase BLEP. No allocations occur in either processing path.
struct Stage {
    double phase = 0.;
    float previous = 0.f, held = 0.f;
    bool initialized = false;
    Word word{};
    BusTap tap{};
    SincHistory inputFilter{}, outputFilter{};
    Stage() { (void)sincBank(); }
    void reset() {
        phase = 0.; previous = held = 0.f; initialized = false; word = {}; tap.reset();
        inputFilter.reset(); outputFilter.reset();
    }
    float deviceSample(float value, Profile profile, int bits, uint8_t xorMask, float tracking,
                       double deviceRate, double hostRate) {
        double x = std::isfinite(value) ? double(value) : 0.;
        x = std::clamp(x, -16., 16.);
        int32_t input = int32_t(std::floor(x * 32768.));
        word = quantize(profile, input, bits, xorMask, tracking);
        held = float(word.value) / 32768.f;
        outputFilter.push(held);
        tap.deviceSample(word.high(), deviceRate, hostRate);
        return held;
    }
    float reconstruct(bool clean, double fraction, double deviceRate, double hostRate) const {
        return clean ? outputFilter.read(fraction, hostRate / deviceRate) : held;
    }
    float process(float in, Profile profile, int bits, uint8_t xorMask, float tracking,
                  double deviceRate, double hostRate, bool aa = false, bool clean = false) {
        if (!std::isfinite(in)) in = 0;
        if (!std::isfinite(hostRate) || hostRate < 1000.) hostRate = 48000.;
        if (!std::isfinite(deviceRate)) deviceRate = 40000.;
        deviceRate = std::clamp(deviceRate, 1000., 192000.);
        if (!initialized) { previous = in; initialized = true; }
        inputFilter.push(in);
        double step = deviceRate / hostRate;
        double total = phase + step;
        int ticks = int(std::floor(total));
        for (int n = 1; n <= ticks; ++n) {
            double f = std::clamp((n - phase) / step, 0., 1.);
            float x = aa ? inputFilter.read(f, deviceRate / hostRate) : previous + float(f) * (in - previous);
            deviceSample(x, profile, bits, xorMask, tracking, deviceRate, hostRate);
        }
        phase = total - ticks;
        previous = in;
        tap.hostSample();
        return reconstruct(clean, phase, deviceRate, hostRate);
    }
};
} // namespace dspemu::dac
