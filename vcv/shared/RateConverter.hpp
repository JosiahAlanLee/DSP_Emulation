// SPDX-License-Identifier: BSD-3-Clause
// Allocation-free audio paths; tables are constructed before processing begins.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace dspemu::rate {
constexpr double pi = 3.1415926535897932384626433832795;

struct SincBank {
    static constexpr int taps = 64, phases = 1024, cutoffs = 9;
    std::array<float, taps * phases * cutoffs> kernel{};
    SincBank() {
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
                for (int k = 0; k < taps; ++k)
                    kernel[(b * phases + p) * taps + k] /= float(sum);
            }
        }
    }
    const float *at(double fraction, double ratio) const {
        int p = std::clamp(int(std::clamp(fraction, 0., .999999999) * phases), 0, phases - 1);
        int b = std::clamp(int(std::ceil(-2 * std::log2(std::clamp(ratio, 1. / 16., 1.)))), 0, cutoffs - 1);
        return &kernel[(b * phases + p) * taps];
    }
};
inline const SincBank &sincBank() {
    static const SincBank table;
    return table;
}

struct SincHistory {
    static constexpr int capacity = 128;
    std::array<float, 2 * capacity> history{};
    unsigned head = 0;
    void reset() {
        history = {};
        head = 0;
    }
    void push(float x) {
        head = (head + 1) % capacity;
        history[head] = history[head + capacity] = x;
    }
    float read(double fraction, double ratio) const {
        if (!std::isfinite(fraction))
            fraction = 0;
        if (!std::isfinite(ratio))
            ratio = 1;
        int whole = int(std::floor(std::clamp(fraction, -2., 2.)));
        unsigned index = unsigned(int(head) + capacity + whole) % capacity;
        const float *signal = &history[index + capacity];
        const float *kernel = sincBank().at(fraction - whole, ratio);
        float out = 0;
        for (int k = 0; k < SincBank::taps; ++k)
            out += signal[-k] * kernel[k];
        return out;
    }
};

// Every level is continuously maintained, so RATE modulation never allocates
// or starts a decimator from an empty state. The chosen path has a rate-
// dependent delay; changing paths can move that delay (documented in the UI).
struct InputConverter {
    static constexpr int levels = 8, halfTaps = 63;
    std::array<SincHistory, levels> sinc{};
    std::array<std::array<float, halfTaps>, levels - 1> delay{};
    std::array<unsigned, levels - 1> head{}, parity{};
    std::array<int64_t, levels> last{};
    int64_t time = -1;
    InputConverter() {
        (void)sincBank();
        (void)halfKernel();
        last.fill(-1);
    }
    static const std::array<float, halfTaps> &halfKernel() {
        static const auto h = [] {
            std::array<float, halfTaps> a{};
            double sum = 0;
            for (int i = 0; i < halfTaps; ++i) {
                double d = i - (halfTaps - 1) / 2.;
                double w = .42 - .5 * std::cos(2 * pi * i / (halfTaps - 1)) +
                           .08 * std::cos(4 * pi * i / (halfTaps - 1));
                a[i] = float((d == 0 ? .5 : std::sin(.5 * pi * d) / (pi * d)) * w);
                sum += a[i];
            }
            for (float &x : a)
                x /= float(sum);
            return a;
        }();
        return h;
    }
    void reset() {
        for (auto &h : sinc)
            h.reset();
        delay = {};
        head = {};
        parity = {};
        last.fill(-1);
        time = -1;
    }
    void push(float x) {
        ++time;
        sinc[0].push(x);
        last[0] = time;
        const auto &h = halfKernel();
        for (int l = 0; l < levels - 1; ++l) {
            auto &d = delay[l];
            auto &p = head[l];
            p = (p + 1) % halfTaps;
            d[p] = x;
            if ((parity[l] ^= 1) != 0)
                break;
            x = 0;
            for (int k = 0; k < halfTaps; ++k)
                x += d[(p + halfTaps - k) % halfTaps] * h[k];
            sinc[l + 1].push(x);
            last[l + 1] = time;
        }
    }
    int level(double device, double host) const {
        int l = 0;
        while (l < levels - 1 && device / host * double(1u << l) < .25)
            ++l;
        return l;
    }
    float read(double hostFraction, double device, double host) const {
        int l = level(device, host);
        double stride = double(1u << l);
        double fraction = (double(time) - 1 + hostFraction - double(last[l])) / stride;
        return sinc[l].read(fraction, device / (host / stride));
    }
    double latency(double device, double host) const {
        int l = level(device, host);
        double stride = double(1u << l);
        return ((halfTaps - 1) * .5 * (stride - 1) + 31 * stride) / host;
    }
};

inline void fft(std::vector<std::complex<double>> &a, bool inverse) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t b = n >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j ^= b;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        auto step = std::polar(1., (inverse ? 2 : -2) * pi / len);
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w = 1;
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= step;
            }
        }
    }
    if (inverse)
        for (auto &x : a)
            x /= double(n);
}

// Real-cepstrum minimum-phase conversion of a Blackman-windowed sinc, followed
// by integration. The oversampled step is used at fractional event positions.
struct BlepTable {
    static constexpr int samples = 64, oversample = 128, length = samples * oversample;
    std::array<float, length + 1> step{};
    BlepTable() {
        constexpr int n = 32768;
        std::vector<std::complex<double>> a(n);
        for (int i = 0; i < length; ++i) {
            double d = double(i) - (length - 1) * .5;
            double w =
                .42 - .5 * std::cos(2 * pi * i / (length - 1)) + .08 * std::cos(4 * pi * i / (length - 1));
            a[i] = (d == 0 ? .92 / oversample : std::sin(.92 * pi * d / oversample) / (pi * d)) * w;
        }
        fft(a, false);
        for (auto &x : a)
            x = std::log(std::max(1e-12, std::abs(x)));
        fft(a, true);
        for (int i = 1; i < n / 2; ++i)
            a[i] *= 2.;
        for (int i = n / 2 + 1; i < n; ++i)
            a[i] = 0;
        fft(a, false);
        for (auto &x : a)
            x = std::exp(x);
        fft(a, true);
        double sum = 0;
        for (int i = 0; i < length; ++i) {
            sum += a[i].real();
            step[i + 1] = float(sum);
        }
        for (auto &x : step)
            x /= float(sum);
        step.back() = 1;
    }
    float at(double age) const {
        if (age <= 0)
            return 0;
        if (age >= samples)
            return 1;
        double p = age * oversample;
        int i = int(p);
        return step[i] + float(p - i) * (step[i + 1] - step[i]);
    }
};
inline const BlepTable &blepTable() {
    static const BlepTable table;
    return table;
}
struct BlepHold {
    std::array<float, BlepTable::samples + 1> correction{};
    unsigned head = 0;
    BlepHold() { (void)blepTable(); }
    void reset() {
        correction = {};
        head = 0;
    }
    void transition(float delta, double fraction) {
        fraction = std::clamp(fraction, 0., 1.);
        const auto &table = blepTable();
        for (int k = 0; k < BlepTable::samples; ++k)
            correction[(head + k) % correction.size()] += delta * (table.at(k + 1 - fraction) - 1.f);
    }
    float read(float held) {
        float out = held + correction[head];
        correction[head] = 0;
        head = (head + 1) % correction.size();
        return out;
    }
};
} // namespace dspemu::rate
