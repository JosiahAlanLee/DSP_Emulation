// PVX confirmed-defect correction tests (DeepSeek, job 20261030-155517).
// Numeric checks only: no audio I/O, no images. Built with
//   g++ -std=c++17 -O3 -o pvx_corrections_test pvx_corrections_test.cpp -I..
// Run from this folder. Covers the four confirmed defects plus the stability /
// channel-retirement / CPU checks the review asked for.
#include "../src/engine/PvxVoice.hpp"
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace dspemu::pvx;

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        ++checks;                                                                          \
        if (!(cond)) {                                                                     \
            ++failures;                                                                    \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                               \
            std::printf(__VA_ARGS__);                                                      \
            std::printf("\n");                                                             \
        }                                                                                  \
    } while (0)

// ---------------------------------------------------------------- helpers
static double mean(const std::vector<float> &x, long from, long to) {
    double s = 0;
    for (long i = from; i < to; ++i)
        s += x[i];
    return s / (to - from);
}
static double rms(const std::vector<float> &x, long from, long to) {
    double s = 0;
    for (long i = from; i < to; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / (to - from));
}
static double meanAbs(const std::vector<float> &x, long from, long to) {
    double s = 0;
    for (long i = from; i < to; ++i)
        s += std::abs(double(x[i]));
    return s / (to - from);
}
static double crossingsHz(const std::vector<float> &x, long from, long to, double sr) {
    long ups = 0;
    for (long i = from + 1; i < to; ++i)
        if (x[i - 1] <= 0 && x[i] > 0)
            ++ups;
    return ups * sr / double(to - from);
}
// Engine pitch is V/OCT with C4 (261.6255653005986 Hz) at 0 V.
static float pitchFor(double f) { return float(std::log2(f / 261.6255653005986)); }

// Voice settings that isolate the VCOs: high cutoff, no cutoff drive (so the
// filter input stage gain is exactly inputDriveScale), VCA open.
static Parameters voiceParams() {
    Parameters p;
    p.env2Attack = .003f;
    p.env2Sustain = 1;
    p.cutoff = 20000;
    p.cutoffDrive = 0;
    p.level = 1;
    return p;
}
// Linear-regime calibration: identity input stage, no integrator saturation,
// no van der Pol compression, no resonance drive. Makes the filter exactly
// match the analytic Chamberlin transfer function.
static FilterCalibration linearCal() {
    FilterCalibration c;
    c.inputDriveScale = 1.0f;
    c.integ1Saturation = 1e-4f;
    c.integ2Saturation = 1e-4f;
    c.dampingCompression = 0.f;
    c.resDriveAmount = 0.f;
    return c;
}
// The tanh plateau for a +-1 pulse: tanh(inputDriveScale * inGain). With
// cutoffDrive = res = 0 the input-stage gain is exactly inputDriveScale.
static double plateau(const Parameters &p, const FilterCalibration &cal) {
    double inGain = 1.0 + cal.cutoffDriveScale * std::clamp(p.cutoffDrive, 0.f, 1.f) *
                              std::pow(std::clamp(p.cutoff / 20000.f, 0.f, 1.f),
                                       cal.cutoffDrivePower);
    inGain *= 1.0 + cal.resDriveAmount * double(std::clamp(p.resonance, 0.f, 1.f));
    return std::tanh(inGain * cal.inputDriveScale);
}

static void fft(std::vector<std::complex<double>> &a) {
    int n = int(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2 * dspemu::rate::pi / len;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (int j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
static double bh4(long n, long N) {
    constexpr double a0 = .35875, a1 = .48829, a2 = .14128, a3 = .01168;
    double t = 2 * dspemu::rate::pi * n / double(N);
    return a0 - a1 * std::cos(t) + a2 * std::cos(2 * t) - a3 * std::cos(3 * t);
}
// Spurious energy: BH4-windowed FFT. Only the true baseband harmonics
// (k*f0 below Nyquist) count as intended harmonic energy; everything else,
// including every folded harmonic, counts as spur/alias. Bins 0..3 carry the
// waveform's DC through the window's own DC lobes (W(0)=N*a0, W(1)=N*a1/2,
// ...), so they are reported as the DC term, not folded into the AC spurious
// ratio. See README for the remaining limits of this metric.
struct AliasReport {
    double dc = 0;      // waveform mean (BLEP DC deficit, see README)
    double spurious = -300;  // AC spur/foldback vs harmonic energy, dB
};
static AliasReport spuriousDb(const std::vector<float> &x, double f0, double sr) {
    long N = long(x.size());
    std::vector<std::complex<double>> a(N);
    for (long i = 0; i < N; ++i)
        a[i] = double(x[i]) * bh4(i, N);
    fft(a);
    auto nearHarmonic = [&](long bin) {
        for (int k = 1; f0 * k < sr / 2; ++k) {
            long h = long(std::lround(f0 * k * N / sr)) % N;
            long d = std::labs(bin - h);
            d = std::min(d, N - d);
            if (d <= 8)
                return true;
        }
        return false;
    };
    double harmonic = 0, alias = 0;
    for (long b = 4; b < N / 2; ++b) {
        double e = std::norm(a[b]);
        if (nearHarmonic(b))
            harmonic += e;
        else
            alias += e;
    }
    AliasReport r;
    r.dc = mean(x, 0, N);
    if (harmonic <= 0 || alias <= 0)
        return r;
    r.spurious = 10 * std::log10(alias / harmonic);
    return r;
}
// Analytic Chamberlin SVF LP magnitude for the exact update equations.
static double analyticH(double g, double k, double w, bool bandpass) {
    using C = std::complex<double>;
    C z = std::exp(C(0, w));
    C zm = 1.0 - 1.0 / z;
    C den = zm * zm + g * k * zm / z + g * g / z;
    return std::abs(bandpass ? (g * zm / den) : (g * g / den));
}
// Reference phase model (no BLEP) for one sub-sample of hard sync, from the
// spec: master wraps at fraction t = (1-p1)/inc1; slave runs freely to
// p2 + t*inc2, resets to 0, then advances (1-t)*inc2.
static double refSlavePhase(double p1, double p2, double inc1, double inc2) {
    if (p1 + inc1 >= 1.0) {
        double t = (1.0 - p1) / inc1;
        double rem = (1.0 - t) * inc2;
        return rem >= 1.0 ? rem - 1.0 : rem;
    }
    double ph = p2 + inc2;
    return ph >= 1.0 ? ph - 1.0 : ph;
}

// ---------------------------------------------------------------- 1. noise
static void t_noise() {
    uint32_t state = 123;
    double sum = 0, sum2 = 0;
    float lo = 1, hi = -1;
    const int N = 1000000;
    for (int i = 0; i < N; ++i) {
        float x = detail::noiseValue(state);
        sum += x;
        sum2 += double(x) * x;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    double m = sum / N;
    double rmsv = std::sqrt(sum2 / N - m * m);
    std::printf("noise: mean %+.6f (want ~0), min %.6f, max %.6f, std %.4f\n", m, lo, hi, rmsv);
    CHECK(std::abs(m) < 2e-3, "noise mean %g, wanted |mean| < 2e-3 (3 sigma)", m);
    CHECK(lo < -0.9999, "noise min %g, wanted < -0.9999", lo);
    CHECK(hi > 0.9999, "noise max %g, wanted positive range > 0.9999", hi);
}

// ------------------------------- 2. pulse BLEP edges (duty + white box)
static void t_pulse() {
    const double sr = 48000;
    // Duty from DC through the tanh plateau (exact for any symmetric
    // compression). A wrong edge polarity or a missing edge at the wrap (+2)
    // or pw (-2) shifts the DC; at 1 kHz the buggy code's error is ~2%, far
    // above the tolerance.
    double T = plateau(voiceParams(), linearCal());
    for (double f : {100., 1000.}) {
        std::printf("pulse duty at %.0f Hz (plateau %.6f), requested pw vs measured:\n", f, T);
        for (float pw : {.1f, .25f, .5f, .75f, .9f}) {
            VoiceEngine e;
            Parameters p = voiceParams();
            p.osc1Wave = 1;
            p.osc2Level = 0;
            p.osc1Pw = pw;
            std::vector<float> x(long(1.5 * sr));
            std::array<Input, 8> in{};
            std::array<float, 8> o, l, b;
            for (long i = 0; i < long(x.size()); ++i) {
                in[0].pitch = pitchFor(f);
                in[0].gate = 10;
                e.process(in, 1, p, linearCal(), sr, 0, 0, o, l, b);
                x[i] = o[0];
            }
            double dc = mean(x, long(0.5 * sr), long(x.size()));
            double duty = (dc / T + 1) / 2;
            std::printf("  pw %.2f -> duty %.4f (want %.4f)\n", pw, duty, double(pw));
            CHECK(std::abs(duty - pw) < 2e-3, "pw %.2f at %.0f Hz: duty %g off by %g", pw, f,
                  duty, duty - pw);
        }
    }
    // White box: one step that carries both pulse edges. inc1 = 0.48, pw
    // clamped to 1.5*inc1 = 0.72, phase 0.58 -> falling edge at
    // (pw-p1)/inc1 and rising wrap edge at (1-p1)/inc1 in the same sub-sample.
    // After the call, the BLEP hold must contain exactly the sum of a -2 edge
    // at the pw fraction and a +2 edge at the wrap fraction.
    {
        VoiceEngine e;
        Parameters p = voiceParams();
        p.oversample = 1;
        p.osc1Wave = 1;
        p.osc1Pw = .6f;
        e.state[0].phase1 = .58;
        float f1 = 261.6255653005986f * std::exp2(std::clamp(pitchFor(23040.0), -12.f, 6.5f));
        f1 = std::min(f1, float(.48 * sr));
        double inc1 = f1 / sr;
        float pw1 = std::max(std::clamp(p.osc1Pw, .05f, .95f), float(1.5 * inc1));
        double fp = (pw1 - .58) / inc1, fw = (1.0 - .58) / inc1;
        std::array<Input, 8> in{};
        in[0].pitch = pitchFor(23040.0);
        in[0].gate = 10;
        std::array<float, 8> out{}, lp{}, bp{};
        e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, out, lp, bp);
        auto &blep = e.state[0].blep1;
        auto &table = dspemu::rate::blepTable();
        double worst = 0;
        for (int k = 0; k < 20; ++k) {
            double want = -2.0 * (table.at(k + 2 - fp) - 1.0) + 2.0 * (table.at(k + 2 - fw) - 1.0);
            double got = blep.correction[(blep.head + k) % blep.correction.size()];
            worst = std::max(worst, std::abs(got - want));
        }
        std::printf("two-edge step: pw frac %.6f, wrap frac %.6f, correction match worst %.2e\n",
                    fp, fw, worst);
        CHECK(worst < 2e-5, "two-edge BLEP corrections off by %.2e", worst);
    }
    // Saw regression: mean ~0 (the shared min-phase BLEP has a small per-edge
    // DC deficit, see README), frequency exact, fundamental matches the
    // analytic tanh-compressed saw through the (linearized) filter.
    {
        VoiceEngine e;
        Parameters p = voiceParams();
        p.osc2Level = 0;
        std::vector<float> x(long(1.5 * sr));
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < long(x.size()); ++i) {
            in[0].pitch = pitchFor(220.0);
            in[0].gate = 10;
            e.process(in, 1, p, linearCal(), sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        long from = long(0.5 * sr), to = long(x.size());
        double m = mean(x, from, to);
        double f = crossingsHz(x, from, to, sr);
        // Fundamental of tanh(saw), saw(theta) = theta/pi - 1 on [0, 2pi):
        // b1 = (2/M) sum tanh(saw(theta)) sin(theta).
        const int M = 40000;
        double b1 = 0;
        for (int i = 0; i < M; ++i) {
            double th = (i + .5) * 2 * dspemu::rate::pi / M;
            b1 += std::tanh(th / dspemu::rate::pi - 1) * std::sin(th);
        }
        b1 *= 2. / M;
        double g = 2 * std::sin(dspemu::rate::pi * 20000 / (sr * 4));
        double want = std::abs(b1) * analyticH(g, 2.0, 2 * dspemu::rate::pi * 220 / (sr * 4), false);
        double a1 = 0, c1 = 0;
        for (long i = from; i < to; ++i) {
            double w = 2 * dspemu::rate::pi * 220.0 * i / sr;
            a1 += x[i] * std::cos(w);
            c1 += x[i] * std::sin(w);
        }
        double amp = 2 * std::sqrt(a1 * a1 + c1 * c1) / (to - from);
        std::printf("saw 220 Hz: mean %+.5f, crossings %g Hz, fundamental %.5f (analytic %.5f)\n",
                    m, f, amp, want);
        CHECK(std::abs(m) < .015, "saw mean %g, wanted ~0 (BLEP DC deficit allows .015)", m);
        CHECK(std::abs(f - 220) < 1.1, "saw frequency %g, wanted 220", f);
        CHECK(std::abs(amp - want) < .02 * want, "saw fundamental %g vs analytic %g", amp, want);
    }
}

// ------------------------------------------------- 3. hard sync, fractional
static void t_sync() {
    const double sr = 48000;
    // Codex repro: one sample, host 48k, os 1, master 12000 Hz, detune +3 st,
    // phase1 = .9, phase2 = .2. Correct post-reset slave phase .178381067.
    {
        VoiceEngine e;
        Parameters p = voiceParams();
        p.oversample = 1;
        p.sync = true;
        p.detune = 3;
        e.state[0].phase1 = .9;
        e.state[0].phase2 = .2;
        std::array<Input, 8> in{};
        in[0].pitch = std::log2(12000.f / 261.6255653f);
        in[0].gate = 10;
        std::array<float, 8> out{}, lp{}, bp{};
        e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, out, lp, bp);
        double want = 0.178381067;
        std::printf("sync repro: master %.9f, slave %.9f (want %.9f, err %.3e cycles)\n",
                    e.state[0].phase1, e.state[0].phase2, want,
                    e.state[0].phase2 - want);
        CHECK(std::abs(e.state[0].phase1 - .149999980) < 1e-6, "master phase off: %.9f",
              e.state[0].phase1);
        CHECK(std::abs(e.state[0].phase2 - want) < 2e-6, "slave phase %.9f, wanted %.9f",
              e.state[0].phase2, want);
    }
    // White box: the sync BLEP jump sits at the master's wrap fraction
    // t = (1-p1)/inc1 with delta = -2*pe, not at the slave's own wrap.
    {
        VoiceEngine e;
        Parameters p = voiceParams();
        p.oversample = 1;
        p.sync = true;
        p.detune = 3;
        e.state[0].phase1 = .9;
        e.state[0].phase2 = .2;
        float f1 = 261.6255653005986f *
                   std::exp2(std::clamp(std::log2(12000.f / 261.6255653f), -12.f, 6.5f));
        f1 = std::min(f1, float(.48 * sr));
        double inc1 = f1 / sr;
        double inc2 = f1 * std::exp2(3. / 12.) / sr;
        double t = (1.0 - .9) / inc1;
        double pe = .2 + t * inc2;
        double delta = -2.0 * pe;
        std::array<Input, 8> in{};
        in[0].pitch = std::log2(12000.f / 261.6255653f);
        in[0].gate = 10;
        std::array<float, 8> out{}, lp{}, bp{};
        e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, out, lp, bp);
        auto &blep = e.state[0].blep2;
        auto &table = dspemu::rate::blepTable();
        double worst = 0;
        for (int k = 0; k < 20; ++k) {
            double want = delta * (table.at(k + 2 - t) - 1.0);
            double got = blep.correction[(blep.head + k) % blep.correction.size()];
            worst = std::max(worst, std::abs(got - want));
        }
        std::printf("sync BLEP: t=%.6f pe=%.6f delta=%.6f, correction match worst %.2e\n", t, pe,
                    delta, worst);
        CHECK(worst < 2e-5, "sync BLEP corrections off by %.2e", worst);
    }
    // White box: slave wraps before the sync event; the wrap edge (-2) lands at
    // its own fraction and the sync jump at t with the post-wrap phase.
    {
        VoiceEngine e;
        Parameters p = voiceParams();
        p.oversample = 1;
        p.sync = true;
        p.detune = 3;
        e.state[0].phase1 = .9;
        e.state[0].phase2 = .95;
        float f1 = 261.6255653005986f *
                   std::exp2(std::clamp(std::log2(12000.f / 261.6255653f), -12.f, 6.5f));
        f1 = std::min(f1, float(.48 * sr));
        double inc1 = f1 / sr;
        double inc2 = f1 * std::exp2(3. / 12.) / sr;
        double t = (1.0 - .9) / inc1;
        double fw = (1.0 - .95) / inc2;
        double pe = .95 + t * inc2 - 1.0;
        double delta = -2.0 * pe;
        std::array<Input, 8> in{};
        in[0].pitch = std::log2(12000.f / 261.6255653f);
        in[0].gate = 10;
        std::array<float, 8> out{}, lp{}, bp{};
        e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, out, lp, bp);
        auto &blep = e.state[0].blep2;
        auto &table = dspemu::rate::blepTable();
        double worst = 0;
        for (int k = 0; k < 20; ++k) {
            double want =
                -2.0 * (table.at(k + 2 - fw) - 1.0) + delta * (table.at(k + 2 - t) - 1.0);
            double got = blep.correction[(blep.head + k) % blep.correction.size()];
            worst = std::max(worst, std::abs(got - want));
        }
        std::printf("pre-sync slave wrap: fw=%.6f t=%.6f delta=%.6f, match worst %.2e\n", fw, t,
                    delta, worst);
        CHECK(worst < 2e-5, "pre-sync wrap BLEP corrections off by %.2e", worst);
    }
    // Grid: engine slave phase vs independent reference, non-integer ratio
    // (+7 st), 49 (p1, p2) pairs covering both wrap and no-wrap steps.
    {
        double inc1 = 0.25, inc2 = inc1 * std::exp2(7. / 12.);
        int bad = 0;
        double worst = 0;
        for (double p1 : {0.01, 0.1, 0.25, 0.5, 0.7, 0.9, 0.999}) {
            for (double p2 : {0.001, 0.05, 0.2, 0.5, 0.75, 0.9, 0.999}) {
                VoiceEngine e;
                Parameters p = voiceParams();
                p.oversample = 1;
                p.sync = true;
                p.detune = 7;
                e.state[0].phase1 = p1;
                e.state[0].phase2 = p2;
                std::array<Input, 8> in{};
                in[0].pitch = pitchFor(inc1 * sr);
                in[0].gate = 10;
                std::array<float, 8> out{}, lp{}, bp{};
                e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, out, lp, bp);
                double err = std::abs(e.state[0].phase2 - refSlavePhase(p1, p2, inc1, inc2));
                worst = std::max(worst, err);
                if (err > 2e-6)
                    ++bad;
            }
        }
        std::printf("sync phase grid (49 pairs, detune +7 st): %d mismatches, worst %.3e\n", bad,
                    worst);
        CHECK(bad == 0, "sync phase grid: %d/49 pairs off", bad);
    }
    // Spectral: master 200 Hz, slave +7 st, sync on -> slave locks to 200 Hz;
    // the synced saw's mean matches a phase-only reference (tanh applied,
    // BLEP DC deficit accounted for within tolerance, see README).
    {
        double f1 = 200.0;
        double inc1 = f1 / (sr * 4), inc2 = inc1 * std::exp2(7. / 12.);
        VoiceEngine e;
        Parameters p = voiceParams();
        p.osc1Level = 0;
        p.osc2Level = 1;
        p.detune = 7;
        p.sync = true;
        std::vector<float> x(long(1.5 * sr));
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < long(x.size()); ++i) {
            in[0].pitch = pitchFor(f1);
            in[0].gate = 10;
            e.process(in, 1, p, linearCal(), sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        double f = crossingsHz(x, long(0.5 * sr), long(x.size()), sr);
        // Phase-only reference mean with tanh (no BLEP).
        double p1 = 0, p2 = 0;
        long warm = long(0.5 * sr) * 4, n = long(x.size()) - long(0.5 * sr);
        double refSum = 0;
        long refN = 0;
        for (long i = 0; i < warm + n; ++i) {
            if (p1 + inc1 >= 1.0) {
                double t = (1.0 - p1) / inc1;
                p2 = (1.0 - t) * inc2;
                p1 += inc1;
                p1 -= 1.0;
            } else {
                p1 += inc1;
                p2 += inc2;
                if (p2 >= 1.0)
                    p2 -= 1.0;
            }
            if (i >= warm) {
                refSum += std::tanh(2.0 * p2 - 1.0);
                ++refN;
            }
        }
        double m = mean(x, long(0.5 * sr), long(x.size()));
        double ref = refSum / refN;
        std::printf("sync saw (master 200 Hz, +7 st): crossings %g Hz, mean %+.5f (reference "
                    "%+.5f, diff %+.5f)\n",
                    f, m, ref, m - ref);
        CHECK(std::abs(f - 200) < 1.0, "sync slave fundamental %g, wanted 200", f);
        CHECK(std::abs(m - ref) < .02, "sync saw mean %g vs reference %g", m, ref);
    }
    // Pulse slave with sync: the output DC (normalized by the tanh plateau)
    // matches a phase-only reference of the ideal pulse-with-sync waveform.
    {
        double f1 = 6000.0;
        double inc1 = f1 / (sr * 4), inc2 = inc1 * std::exp2(7. / 12.);
        float pw = .4f;
        VoiceEngine e;
        Parameters p = voiceParams();
        p.osc1Level = 0;
        p.osc2Level = 1;
        p.osc2Wave = 1;
        p.osc2Pw = pw;
        p.detune = 7;
        p.sync = true;
        long n = long(1.5 * sr);
        std::vector<float> x(n);
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < n; ++i) {
            in[0].pitch = pitchFor(f1);
            in[0].gate = 10;
            e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        double p1 = 0, p2 = 0;
        long warm = 48000;
        double refSum = 0;
        long refN = 0;
        for (long i = 0; i < warm + n; ++i) {
            for (int s = 0; s < 4; ++s) {
                if (p1 + inc1 >= 1.0) {
                    double t = (1.0 - p1) / inc1;
                    p2 = (1.0 - t) * inc2;
                    p1 += inc1;
                    p1 -= 1.0;
                } else {
                    p1 += inc1;
                    p2 += inc2;
                    if (p2 >= 1.0)
                        p2 -= 1.0;
                }
                if (i >= warm) {
                    refSum += (p2 < pw ? 1.0 : -1.0);
                    ++refN;
                }
            }
        }
        double T = plateau(p, FilterCalibration{});
        double got = mean(x, 0, n) / T;
        double want = refSum / refN;
        std::printf("pulse+sync DC: engine %.5f, reference %.5f (diff %+.5f)\n", got, want,
                    got - want);
        CHECK(std::abs(got - want) < .02, "pulse+sync DC %g vs reference %g", got, want);
    }
}

// ------------------------------------------------------- 4. alias honesty
static void t_alias() {
    const long N = 65536;
    const double sr = 48000;
    // Rate-dependent cutoff caps and a fixed-length FIR change bandwidth
    // between modes. Require adequate rejection rather than monotonicity,
    // which is not a valid invariant across these different signal paths.
    // These windowed diagnostics include startup; the independent steady-
    // state harness provides the final delivery's spectral measurements.
    auto measure = [&](Parameters p, double f0, const char *name, bool assertRejection) {
        // Integer cycles per window: removes window-edge leakage so the
        // measurement reflects the engine, not the analysis window.
        double fc = std::round(f0 * N / sr) * sr / N;
        std::printf("spurious/alias level, %s (%.0f Hz requested, %.4f measured, cutoff 8k):\n",
                    name, f0, fc);
        for (int os : {1, 2, 4}) {
            VoiceEngine e;
            p.oversample = os;
            std::vector<float> x(N);
            std::array<Input, 8> in{};
            std::array<float, 8> o, l, b;
            for (long i = 0; i < N; ++i) {
                in[0].pitch = pitchFor(fc);
                in[0].gate = 10;
                e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, o, l, b);
                x[i] = o[0];
            }
            auto r = spuriousDb(x, fc, sr);
            std::printf("  os %dx: spurious %+6.1f dB, mean %+.5f\n", os, r.spurious, r.dc);
            if (assertRejection && os > 1)
                CHECK(r.spurious < -45., "oversampled spurious at os %d: %g dB exceeds -45 dB", os, r.spurious);
        }
    };
    Parameters saw = voiceParams();
    saw.osc2Level = 0;
    saw.cutoff = 8000;
    measure(saw, 5432.0, "saw", true);
    Parameters pulse = voiceParams();
    pulse.osc1Wave = 1;
    pulse.osc1Pw = .35f;
    pulse.osc2Level = 0;
    pulse.cutoff = 8000;
    measure(pulse, 5432.0, "pulse pw .35", true);
    Parameters sync = voiceParams();
    sync.osc1Level = 0;
    sync.osc2Level = 1;
    sync.detune = 7;
    sync.sync = true;
    sync.cutoff = 8000;
    measure(sync, 200.0, "hard-synced saw (master 200 Hz, +7 st)", false);
    // Driven nonlinear path: 997 Hz sine through overdrive, drive = 1.
    double fod = std::round(997.0 * N / sr) * sr / N;
    std::printf("spurious/alias level, 997 Hz sine through overdrive (drive 1, cutoff 8k):\n");
    for (int os : {1, 2, 4}) {
        VoiceEngine e;
        Parameters p = voiceParams();
        p.osc1Level = p.osc2Level = p.noiseLevel = 0;
        p.extLevel = 1;
        p.oversample = os;
        p.cutoff = 8000;
        p.drive = 1;
        std::vector<float> x(N);
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < N; ++i) {
            in[0].gate = 10;
            in[0].external = float(std::sin(2 * dspemu::rate::pi * fod * i / sr));
            e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        auto r = spuriousDb(x, fod, sr);
        std::printf("  os %dx: spurious %+6.1f dB, mean %+.5f\n", os, r.spurious, r.dc);
        CHECK(r.spurious < -70., "driven sine spurious at os %d: %g dB exceeds -70 dB", os, r.spurious);
    }
}

// ----------------------------------- 5. channel retirement / reactivation
static void t_channels() {
    const double sr = 48000;
    std::array<Input, 8> in{};
    std::array<float, 8> o, l, b;
    Parameters p = voiceParams();
    p.env2Attack = .05f;  // slow attack so a stale held envelope is obvious
    p.env2Sustain = 1;
    p.env1Attack = .003f;
    p.env1Sustain = 1;
    VoiceEngine e;
    // All 8 voices gated and held for 0.5 s.
    for (long i = 0; i < long(.5 * sr); ++i) {
        for (int c = 0; c < 8; ++c) {
            in[c].pitch = pitchFor(220.0);
            in[c].gate = 10;
        }
        e.process(in, 8, p, FilterCalibration{}, sr, 0, 0, o, l, b);
    }
    // Shrink to 4 channels for 1 s: voices 4..7 retire.
    for (long i = 0; i < long(1.0 * sr); ++i) {
        for (int c = 0; c < 4; ++c)
            in[c].gate = 10;
        e.process(in, 4, p, FilterCalibration{}, sr, 0, 0, o, l, b);
        CHECK(o[5] == 0.f && l[5] == 0.f && b[5] == 0.f,
              "retired channel outputs not exactly zero at sample %ld", i);
    }
    // Re-expand to 8. Voice 5 is re-triggered; it must attack from zero (no
    // stale held envelope) while voice 0 (kept gated) stays at full level.
    std::vector<float> v0, v5;
    for (long i = 0; i < long(1.0 * sr); ++i) {
        for (int c = 0; c < 8; ++c) {
            in[c].pitch = pitchFor(c == 5 ? 330.0 : 220.0);
            in[c].gate = 10;
        }
        e.process(in, 8, p, FilterCalibration{}, sr, 0, 0, o, l, b);
        v0.push_back(o[0]);
        v5.push_back(o[5]);
    }
    double early5 = meanAbs(v5, 0, long(.01 * sr));
    double late5 = meanAbs(v5, long(.9 * sr), long(1.0 * sr));
    double late0 = meanAbs(v0, long(.9 * sr), long(1.0 * sr));
    double first5 = 0;
    for (int i = 0; i < 10; ++i)
        first5 = std::max(first5, std::abs(double(v5[i])));
    std::printf("re-expanded voice 5: first 10 ms mean|out| %.5f (wants ~0), first 10 smp peak "
                "%.5f, late %.5f; voice 0 late %.5f\n",
                early5, first5, late5, late0);
    CHECK(early5 < late5 * .4, "voice 5 did not attack from zero: early %g vs late %g", early5,
          late5);
    CHECK(first5 < .2, "voice 5 first-sample level %g, stale held envelope?", first5);
    CHECK(late5 > .3, "voice 5 never reached full level: late %g", late5);
    CHECK(late0 > .3, "voice 0 level dropped: late %g", late0);
    CHECK(!e.hasDenormals(), "denormals after shrink/reexpand cycle");
    // Independent triggering: retire voice 5 again, then re-trigger it while
    // voices 0..3 stay gated; its envelope must restart from zero.
    for (long i = 0; i < long(.5 * sr); ++i) {
        for (int c = 0; c < 4; ++c)
            in[c].gate = 10;
        e.process(in, 4, p, FilterCalibration{}, sr, 0, 0, o, l, b);
    }
    std::vector<float> w5;
    for (long i = 0; i < long(.3 * sr); ++i) {
        for (int c = 0; c < 8; ++c)
            in[c].gate = 10;
        e.process(in, 8, p, FilterCalibration{}, sr, 0, 0, o, l, b);
        w5.push_back(o[5]);
    }
    double wEarly = meanAbs(w5, 0, long(.01 * sr));
    double wLate = meanAbs(w5, long(.2 * sr), long(.3 * sr));
    std::printf("second re-trigger: early %.5f, late %.5f\n", wEarly, wLate);
    CHECK(wEarly < wLate * .4, "second re-trigger early %g vs late %g", wEarly, wLate);
    // Shrink/expand repeatedly while gated; nothing may become non-finite.
    for (int round = 0; round < 20; ++round) {
        int ch = (round % 2) ? 8 : 3;
        for (long i = 0; i < 1000; ++i) {
            for (int c = 0; c < 8; ++c)
                in[c].gate = 10;
            e.process(in, ch, p, FilterCalibration{}, sr, 0, 0, o, l, b);
            for (int c = 0; c < 8; ++c)
                CHECK(std::isfinite(o[c]) && std::isfinite(l[c]) && std::isfinite(b[c]),
                      "non-finite channel %d at round %d sample %ld", c, round, i);
        }
    }
    std::printf("shrink/expand: finite across 20 rounds, no denormals\n");
}

// -------------------------------- 6. 8-voice stability, rates, CPU
static void t_stability8() {
    const double srs[] = {44100, 48000, 96000};
    for (double sr : srs) {
        for (int os : {1, 2, 4}) {
            VoiceEngine e;
            Parameters p = voiceParams();
            p.osc1Wave = p.osc2Wave = 1;
            p.osc1Pw = .3f;
            p.osc2Level = 1;
            p.noiseLevel = .5f;
            p.sync = true;
            p.resonance = 1.f;  // full resonance, self-oscillating
            p.cutoff = 1000;
            p.drive = 1;
            p.cutoffDrive = 1;
            p.oversample = os;
            p.env1Attack = p.env2Attack = .002f;
            p.env1Sustain = p.env2Sustain = 1;
            long n = long(1.0 * sr);
            std::array<Input, 8> in{};
            std::array<float, 8> o, l, b;
            bool finite = true;
            float peak = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (long i = 0; i < n; ++i) {
                for (int c = 0; c < 8; ++c) {
                    in[c].pitch = pitchFor(110.0 * std::exp2(c / 12.0));
                    in[c].gate = 10;
                }
                e.process(in, 8, p, FilterCalibration{}, sr, 0, 0, o, l, b);
                for (int c = 0; c < 8; ++c) {
                    finite &= std::isfinite(o[c]);
                    peak = std::max(peak, std::abs(o[c]));
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / n;
            std::printf("8 voices, sr %g, os %dx, res 1.0: finite=%d peak=%.3f, %.2f us/sample "
                        "(%.2fx realtime)\n",
                        sr, os, finite, peak, us, sr * us / 1e6);
            CHECK(finite && !e.hasDenormals(), "8-voice non-finite/denormal at sr=%g os=%d", sr,
                  os);
            CHECK(peak < 4, "8-voice peak %g at sr=%g os=%d", peak, sr, os);
        }
        // Self-osc pitch near 1 kHz should be rate-independent (measured;
        // exact tracking is a calibration concern, see README).
        VoiceEngine e2;
        Parameters q = voiceParams();
        q.osc1Level = q.osc2Level = q.noiseLevel = 0;
        q.resonance = 1;
        q.cutoff = 1000;
        q.cutoffDrive = 0;
        q.env2Attack = .002f;
        long n = long(3.0 * sr);
        std::vector<float> lp(n);
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < n; ++i) {
            in[0].gate = 10;
            e2.process(in, 1, q, FilterCalibration{}, sr, 0, 0, o, l, b);
            lp[i] = l[0];
        }
        double f = crossingsHz(lp, long(2.0 * sr), n, sr);
        std::printf("self-osc 1 kHz at sr %g: measured %g Hz\n", sr, f);
        CHECK(std::abs(f - 1000) < 40, "self-osc at sr=%g measured %g Hz", sr, f);
    }
}

// ----------------------------------------- 7. pitch API and filter gain
static void t_pitchGain() {
    // V/OCT request form and round-trip.
    double hz = 440.0;
    float v = pitchFor(hz);
    CHECK(std::abs(v - std::log2(hz / 261.6255653005986)) < 1e-6, "pitch request form wrong");
    const double sr = 48000;
    {
        VoiceEngine e;
        std::vector<float> x(long(1.5 * sr));
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < long(x.size()); ++i) {
            in[0].pitch = v;
            in[0].gate = 10;
            e.process(in, 1, voiceParams(), linearCal(), sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        double f = crossingsHz(x, long(0.5 * sr), long(x.size()), sr);
        std::printf("pitch API: request log2(440/261.6256)=%+.4f V -> %g Hz\n", v, f);
        CHECK(std::abs(f - 440) < 1.1, "pitch API round-trip %g Hz", f);
    }
    // Gain convention: an RMS .01 sine (amplitude .01 -> RMS .01/sqrt2) into
    // the filter measures its magnitude; compare against the analytic transfer
    // function of the exact update equations.
    {
        double fc = 1000;
        double g = 2 * std::sin(dspemu::rate::pi * fc / (sr * 4));
        std::printf("LP gain (cutoff 1 kHz, res 0), measured vs analytic:\n");
        for (double f : {100., 400., 1000., 2000., 4000.}) {
            VoiceEngine e;
            Parameters p = voiceParams();
            p.osc1Level = p.osc2Level = p.noiseLevel = 0;
            p.extLevel = 1;
            p.cutoff = float(fc);
            p.cutoffDrive = 0;
            std::vector<float> lp(long(1.5 * sr));
            std::array<Input, 8> in{};
            std::array<float, 8> o, l, b;
            for (long i = 0; i < long(lp.size()); ++i) {
                in[0].gate = 10;
                in[0].external = float(.01 * std::sin(2 * dspemu::rate::pi * f * i / sr));
                e.process(in, 1, p, linearCal(), sr, 0, 0, o, l, b);
                lp[i] = l[0];
            }
            double meas = rms(lp, long(sr), long(1.5 * sr)) / (.01 / std::sqrt(2.0));
            double want = analyticH(g, 2.0, 2 * dspemu::rate::pi * f / (sr * 4), false);
            double db = 20 * std::log10(std::max(meas, 1e-9) / std::max(want, 1e-9));
            std::printf("  %6g Hz: %+7.2f dB (analytic %+7.2f dB, diff %+.3f)\n", f,
                        20 * std::log10(meas), 20 * std::log10(want), db);
            CHECK(std::abs(db) < .2, "LP at %g Hz: %g dB off analytic", f, db);
        }
    }
}

int main() {
    t_noise();
    t_pulse();
    t_sync();
    t_alias();
    t_channels();
    t_stability8();
    t_pitchGain();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
