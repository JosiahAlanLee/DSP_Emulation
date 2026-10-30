// PVX voice engine numeric tests. No Rack, no audio I/O, no images:
// everything is measured here and printed as numbers.
#include "../src/engine/PvxVoice.hpp"
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <vector>

using namespace dspemu::pvx;

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                                                              \
    do {                                                                              \
        ++checks;                                                                     \
        if (!(cond)) {                                                                \
            ++failures;                                                               \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                          \
            std::printf(__VA_ARGS__);                                                 \
            std::printf("\n");                                                        \
        }                                                                             \
    } while (0)

// ---------------------------------------------------------------- helpers
struct Run {
    std::vector<float> out, lp, bp;
};

static Run run(VoiceEngine &engine, const Parameters &p, const FilterCalibration &cal,
               double sr, double seconds, int channels,
               const std::function<void(long, std::array<Input, 8> &)> &fill = nullptr) {
    long n = long(seconds * sr + .5);
    Run r;
    r.out.resize(n);
    r.lp.resize(n);
    r.bp.resize(n);
    std::array<Input, 8> in{};
    for (long i = 0; i < n; ++i) {
        if (fill)
            fill(i, in);
        std::array<float, 8> o, l, b;
        engine.process(in, channels, p, cal, sr, 0, 0, o, l, b);
        r.out[i] = o[0];
        r.lp[i] = l[0];
        r.bp[i] = b[0];
    }
    return r;
}

static double rms(const std::vector<float> &x, long from, long to) {
    double s = 0;
    long n = 0;
    for (long i = from; i < to; ++i) {
        s += double(x[i]) * x[i];
        ++n;
    }
    return std::sqrt(s / n);
}

static double meanAbs(const std::vector<float> &x, long from, long to) {
    double s = 0;
    for (long i = from; i < to; ++i)
        s += std::abs(double(x[i]));
    return s / (to - from);
}

static double mean(const std::vector<float> &x, long from, long to) {
    double s = 0;
    for (long i = from; i < to; ++i)
        s += x[i];
    return s / (to - from);
}

// Zero-crossing frequency over [from, to).
static double crossingsHz(const std::vector<float> &x, long from, long to, double sr) {
    long ups = 0;
    for (long i = from + 1; i < to; ++i)
        if (x[i - 1] <= 0 && x[i] > 0)
            ++ups;
    return ups * sr / double(to - from);
}

// Octaves of pitch for a frequency (engine pitch is V/OCT, C4 = 0 V).
static float pitchFor(double f) { return float(std::log2(f / 261.6255653005986)); }

// Analytic Chamberlin SVF magnitudes for the exact update equations the
// engine runs in its linear regime:
//   b[n] = b[n-1] + g (x[n] - k b[n-1] - l[n-1])
//   l[n] = l[n-1] + g b[n]
//   H_LP(z) = g^2 / D,  H_BP(z) = g (1-z^-1) / D,
//   D = (1-z^-1)^2 + g k z^-1 (1-z^-1) + g^2 z^-1.
static double analyticH(double g, double k, double w, bool bandpass) {
    using C = std::complex<double>;
    C z = std::exp(C(0, w));
    C zm = 1.0 - 1.0 / z;
    C den = zm * zm + g * k * zm / z + g * g / z;
    return std::abs(bandpass ? (g * zm / den) : (g * g / den));
}

// The engine's external input is held at the host rate (control rate), so the
// measured response includes the zero-order-hold droop sin(pi f / fs) / (pi f / fs).
static double holdDroop(double f, double sr) {
    double w = dspemu::rate::pi * f / sr;
    return std::abs(std::sin(w) / w);
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

// 4-term Blackman-Harris window.
static double bh4(long n, long N) {
    constexpr double a0 = .35875, a1 = .48829, a2 = .14128, a3 = .01168;
    double t = 2 * dspemu::rate::pi * n / double(N);
    return a0 - a1 * std::cos(t) + a2 * std::cos(2 * t) - a3 * std::cos(3 * t);
}

// Alias level: energy in bins away from the harmonic bins vs harmonic energy.
static double aliasDb(const std::vector<float> &x, double f0, double sr, long skip = 0) {
    if (skip > 0)
        return aliasDb(std::vector<float>(x.begin() + skip, x.end()), f0, sr, 0);
    long N = long(x.size());
    std::vector<std::complex<double>> a(N);
    for (long i = 0; i < N; ++i)
        a[i] = double(x[i]) * bh4(i, N);
    fft(a);
    auto nearHarmonic = [&](long bin) {
        int kMax = int(sr / (2 * f0));  // in-band harmonics only
        for (int k = 1; k <= kMax; ++k) {
            double hb = f0 * k * N / sr;
            long h = long(std::lround(hb)) % N;
            long d = std::labs(bin - h);
            d = std::min(d, N - d);
            if (d <= 8)
                return true;
        }
        return false;
    };
    double harmonic = 0, alias = 0;
    for (long b = 0; b < N / 2; ++b) {
        double e = std::norm(a[b]);
        if (nearHarmonic(b) || b < 24)  // DC and transient leakage are not alias
            harmonic += e;
        else
            alias += e;
    }
    if (harmonic <= 0 || alias <= 0)
        return -300;
    return 10 * std::log10(alias / harmonic);
}

// ---------------------------------------------------------------- tests
static Parameters voice() {
    Parameters p;
    p.env2Attack = .003f;
    p.env2Sustain = 1;
    p.cutoff = 20000;
    p.level = 1;
    return p;
}

static void t_garbageInputs() {
    VoiceEngine e;
    Parameters p = voice();
    p.resonance = 1;
    std::array<Input, 8> in{};
    in[0] = {std::nanf(""), std::numeric_limits<float>::infinity(), std::nanf("")};
    std::array<float, 8> o, l, b;
    for (int i = 0; i < 1000; ++i) {
        e.process(in, 1, p, FilterCalibration{}, 48000, std::nanf(""), -1e30f, o, l, b);
        CHECK(std::isfinite(o[0]) && std::isfinite(l[0]) && std::isfinite(b[0]),
              "garbage inputs produced non-finite output o=%g l=%g b=%g", o[0], l[0], b[0]);
    }
    std::printf("garbage inputs: finite throughout\n");
}

static void t_stability() {
    const double srs[] = {44100, 48000, 96000};
    for (double sr : srs)
        for (int os : {1, 2, 4})
            for (float res : {0.f, .5f, .8f, .9f, .95f, .99f, 1.f})
                for (float cutoff : {50.f, 200.f, 1000.f, 5000.f, 16000.f})
                    for (float drive : {0.f, .5f, 1.f}) {
                        VoiceEngine e;
                        Parameters p = voice();
                        p.osc1Wave = p.osc2Wave = 1;
                        p.osc2Level = 1;
                        p.noiseLevel = .5f;
                        p.sync = true;
                        p.resonance = res;
                        p.cutoff = cutoff;
                        p.drive = drive;
                        p.cutoffDrive = 1;
                        p.oversample = os;
                        p.env1Attack = .002f;
                        p.env1Sustain = 1;
                        p.env2Attack = .002f;
                        Run r = run(e, p, FilterCalibration{}, sr, 1.0, 1, [](long, auto &in) {
                            in[0].pitch = 2;
                            in[0].gate = 10;
                        });
                        bool finite = true;
                        float peak = 0;
                        for (float x : r.out) {
                            finite &= std::isfinite(x);
                            peak = std::max(peak, std::abs(x));
                        }
                        CHECK(finite, "non-finite output at sr=%g os=%d res=%g fc=%g drive=%g",
                              sr, os, res, cutoff, drive);
                        CHECK(peak < 4, "output unbounded: peak %g at sr=%g os=%d res=%g fc=%g",
                              peak, sr, os, res, cutoff);
                        CHECK(!e.hasDenormals(), "denormals at sr=%g os=%d res=%g fc=%g", sr, os,
                              res, cutoff);
                    }
    std::printf("stability: res x cutoff x drive x sr x os sweep bounded and denormal-free\n");
}

static void t_silence() {
    VoiceEngine e;
    Parameters p = voice();
    p.resonance = .9f;
    p.noiseLevel = 0;  // the generator runs free after gate-off; silence it for the idle check
    p.env2Release = .2f;
    p.osc1Level = p.osc2Level = 0;
    long hot = long(0.5 * 48000), tail = long(10 * 48000);
    std::array<Input, 8> in{};
    std::array<float, 8> o, l, b;
    for (long i = 0; i < hot; ++i) {
        in[0] = {1, 10, 0};
        e.process(in, 1, p, FilterCalibration{}, 48000, 0, 0, o, l, b);
    }
    for (long i = 0; i < tail; ++i) {
        in[0] = {1, 0, 0};
        e.process(in, 1, p, FilterCalibration{}, 48000, 0, 0, o, l, b);
    }
    CHECK(o[0] == 0.f && l[0] == 0.f && b[0] == 0.f, "silence did not reach exact zero");
    CHECK(!e.hasDenormals(), "denormals after 10 s of silence");
    std::printf("silence: exact zeros, no denormals after 10 s tail\n");
}

static void t_vco() {
    // Linear-regime calibration: the voice-level tests below measure the
    // VCOs directly, so the input stage must not reshape or re-level them.
    FilterCalibration cal;
    cal.inputDriveScale = 0.01f;  // tanh(0.01 x) ~ 0.01 x: linear at full scale
    cal.resDriveAmount = 0;
    auto vcoParams = [] {
        Parameters p = voice();
        p.cutoffDrive = 0;
        p.cutoff = 5000;  // well-behaved Chamberlin region (see CALIBRATION.md)
        return p;
    };
    // Saw fundamental.
    {
        VoiceEngine e;
        Parameters p = vcoParams();
        Run r = run(e, p, cal, 48000, 2.0, 1, [](long, auto &in) {
            in[0].pitch = pitchFor(220.0);
            in[0].gate = 10;
        });
        double f = crossingsHz(r.out, long(0.5 * 48000), long(2.0 * 48000), 48000);
        CHECK(std::abs(f - 220) < 1.1, "saw fundamental %g Hz, wanted 220", f);
        std::printf("vco saw: measured %g Hz (220 requested)\n", f);
    }
    // Pulse duty.
    {
        VoiceEngine e;
        Parameters p = vcoParams();
        p.osc1Wave = 1;
        p.osc1Pw = .25f;
        Run r = run(e, p, cal, 48000, 2.0, 1, [](long, auto &in) {
            in[0].pitch = pitchFor(50.0);
            in[0].gate = 10;
        });
        double dc = mean(r.out, long(1.0 * 48000), long(2.0 * 48000)) / .01;
        double duty = (dc + 1) / 2;
        CHECK(std::abs(duty - .25) < .02, "pulse duty %g, wanted 0.25", duty);
        std::printf("vco pulse: measured duty %g (0.25 requested)\n", duty);
    }
    // Sync: VCO2 locks to VCO1.
    for (bool sync : {false, true}) {
        VoiceEngine e;
        Parameters p = vcoParams();
        p.osc1Level = 0;
        p.osc2Level = 1;
        p.detune = 3;
        p.sync = sync;
        Run r = run(e, p, cal, 48000, 2.0, 1, [](long, auto &in) {
            in[0].pitch = pitchFor(200.0);
            in[0].gate = 10;
        });
        double f = crossingsHz(r.out, long(0.5 * 48000), long(2.0 * 48000), 48000);
        double want = sync ? 200.0 : 200.0 * std::exp2(3.0 / 12);
        CHECK(std::abs(f - want) < 1.5, "sync=%d: measured %g Hz, wanted %g", sync, f, want);
        std::printf("vco sync %s: measured %g Hz (wanted %g)\n", sync ? "on" : "off", f, want);
    }
}

static void t_envelopes() {
    FilterCalibration cal;
    cal.inputDriveScale = 0.01f;  // tanh(0.01 x) ~ 0.01 x: linear at full scale
    cal.resDriveAmount = 0;
    // ADSR timing.
    {
        VoiceEngine e;
        Parameters p = voice();
        p.cutoffDrive = 0;
        p.cutoff = 5000;
        p.osc1Level = p.osc2Level = p.noiseLevel = 0;
        p.envMode = 1;
        p.env2Attack = .1f;
        p.env2Decay = .3f;
        p.env2Sustain = .5f;
        p.env2Release = .2f;
        Run r = run(e, p, cal, 48000, 2.0, 1, [](long n, auto &in) {
            in[0].gate = n < 48000 ? 10 : 0;
            in[0].external = float(std::sin(2 * dspemu::rate::pi * 440.0 * n / 48000.0));
        });
        // mean|sine| = 2/pi * envelope * 0.01 (input-stage scale).
        double peak = meanAbs(r.out, long(.09 * 48000), long(.11 * 48000)) * dspemu::rate::pi / 2 /
                      .01;
        double sus = meanAbs(r.out, long(.5 * 48000), long(.6 * 48000)) * dspemu::rate::pi / 2 /
                     .01;
        double rel = meanAbs(r.out, long(1.5 * 48000), long(1.6 * 48000)) * dspemu::rate::pi / 2 /
                     .01;
        CHECK(std::abs(peak - .98) < .06, "ADSR attack plateau %g, wanted ~0.98", peak);
        CHECK(std::abs(sus - .5) < .04, "ADSR sustain %g, wanted 0.5", sus);
        CHECK(rel < .02, "ADSR release tail %g, wanted < 0.02", rel);
        std::printf("ADSR: attack %g, sustain %g, release tail %g\n", peak, sus, rel);
    }
    // AR mode: holds while gated, D/S ignored.
    {
        VoiceEngine e;
        Parameters p = voice();
        p.cutoffDrive = 0;
        p.cutoff = 5000;
        p.osc1Level = p.osc2Level = p.noiseLevel = 0;
        p.envMode = 0;
        p.env2Attack = .05f;
        p.env2Decay = 5;
        p.env2Sustain = .01f;
        p.env2Release = .1f;
        Run r = run(e, p, cal, 48000, 1.0, 1, [](long n, auto &in) {
            in[0].gate = n < long(.5 * 48000) ? 10 : 0;
            in[0].external = float(std::sin(2 * dspemu::rate::pi * 440.0 * n / 48000.0));
        });
        double held = meanAbs(r.out, long(.3 * 48000), long(.4 * 48000)) * dspemu::rate::pi / 2 /
                      .01;
        double rel = meanAbs(r.out, long(.7 * 48000), long(.8 * 48000)) * dspemu::rate::pi / 2 /
                     .01;
        CHECK(std::abs(held - 1) < .06, "AR hold %g, wanted ~1 (D and S ignored)", held);
        CHECK(rel < .05, "AR release tail %g, wanted < 0.05", rel);
        std::printf("AR: hold %g, release tail %g\n", held, rel);
    }
}

static void t_filterResponse() {
    Parameters p = voice();
    p.osc1Level = p.osc2Level = p.noiseLevel = 0;
    p.extLevel = 1;
    p.cutoff = 1000;
    p.cutoffDrive = 0;
    const double sr = 48000;
    const int os = 4;
    double fs4 = sr * os;
    auto gk = [&](float res) {
        double g = 2 * std::sin(dspemu::rate::pi * p.cutoff / fs4);
        g = std::min(g, 1.9);
        float k = 2.f * std::pow(1.f - res, FilterCalibration{}.resonanceExponent);
        return std::pair<double, double>(g, k);
    };
    auto fill = [](double f) {
        return [f](long n, auto &in) {
            in[0].gate = 10;
            in[0].external = float(.01 * std::sin(2 * dspemu::rate::pi * f * n / 48000.0));
        };
    };
    // Linear-regime calibration: identity input stage (no drive loss).
    FilterCalibration cal;
    cal.inputDriveScale = 1.0f;
    cal.resDriveAmount = 0;
    // LP magnitude table vs analytic.
    std::printf("LP response (cutoff 1 kHz, res 0, 4x), measured vs analytic:\n");
    auto [g0, k0] = gk(0);
    for (double f : {100., 400., 1000., 2000., 4000., 8000., 12000.}) {
        VoiceEngine e;
        Run r = run(e, p, cal, sr, 1.5, 1, fill(f));
        double meas = rms(r.lp, long(sr), long(1.5 * sr)) / (.01 / std::sqrt(2.0));
        double want = analyticH(g0, k0, 2 * dspemu::rate::pi * f / fs4, false) *
                            holdDroop(f, sr);
        double db = 20 * std::log10(std::max(meas, 1e-9) / std::max(want, 1e-9));
        CHECK(std::abs(db) < .2, "LP at %g Hz: %g dB off analytic", f, db);
        std::printf("  %6g Hz: %+7.2f dB (analytic %+7.2f dB, diff %+.3f)\n", f,
                    20 * std::log10(meas), 20 * std::log10(want), db);
    }
    // BP peak at resonance.
    p.resonance = .8f;
    auto [g1, k1] = gk(.8f);
    std::printf("BP response (cutoff 1 kHz, res 0.8):\n");
    for (double f : {250., 500., 1000., 2000., 4000.}) {
        VoiceEngine e;
        Run r = run(e, p, cal, sr, 1.5, 1, fill(f));
        double meas = rms(r.bp, long(sr), long(1.5 * sr)) / (.01 / std::sqrt(2.0));
        double want = analyticH(g1, k1, 2 * dspemu::rate::pi * f / fs4, true) *
                            holdDroop(f, sr);
        double db = 20 * std::log10(std::max(meas, 1e-9) / std::max(want, 1e-9));
        CHECK(std::abs(db) < 1.0, "BP at %g Hz: %g dB off analytic", f, db);
        std::printf("  %6g Hz: %+7.2f dB (analytic %+7.2f dB, diff %+.3f)\n", f,
                    20 * std::log10(meas), 20 * std::log10(want), db);
    }
    double q = 1.0 / k1;
    CHECK(std::abs(q - 17.04) < .5, "resonance Q %g, wanted ~17 (k=%g)", q, k1);
    std::printf("res 0.8 -> Q = 1/k = %g\n", q);
    // 2-pole slope check: LP gain at 2k/4k vs 1k.
    p.resonance = 0;
    auto [g2, k2] = gk(0);
    VoiceEngine e;
    Run r1 = run(e, p, cal, sr, 1.5, 1, fill(1000));
    double base = rms(r1.lp, long(sr), long(1.5 * sr));
    for (double f : {2000., 4000.}) {
        VoiceEngine e2;
        Run r = run(e2, p, cal, sr, 1.5, 1, fill(f));
        double m = rms(r.lp, long(sr), long(1.5 * sr));
        double slope = 20 * std::log10(m / base);
        // The reference is the analytic response itself (a Q = 0.5 two-pole
        // is only ~-8 dB at 2 fc, not the -12 dB/oct asymptote).
        double want = 20 * std::log10(analyticH(g2, k2, 2 * dspemu::rate::pi * f / fs4, false) *
                                      holdDroop(f, sr) /
                                      (analyticH(g2, k2, 2 * dspemu::rate::pi * 1000 / fs4,
                                                 false) *
                                       holdDroop(1000, sr)));
        CHECK(std::abs(slope - want) < 1.0, "LP slope at %g Hz: %g dB, analytic %g", f, slope,
              want);
        std::printf("LP slope %g Hz vs 1 kHz: %+7.2f dB (analytic two-pole %+7.2f)\n", f, slope,
                    want);
    }
}

static void t_selfOsc() {
    const double sr = 48000;
    std::printf("self-oscillation pitch tracking (res 1.0, kick-excited, 4x):\n");
    for (double fc : {100., 200., 400., 800., 1600., 2400., 4000., 6000., 8000., 12000., 16000.}) {
        VoiceEngine e;
        Parameters p = voice();
        p.osc1Level = p.osc2Level = p.noiseLevel = 0;
        p.resonance = 1;
        p.cutoff = float(fc);
        p.cutoffDrive = 0;
        p.env2Attack = .002f;
        Run r = run(e, p, FilterCalibration{}, sr, 3.0, 1, [](long n, auto &in) {
            in[0].gate = 10;
            (void)n;
        });
        double f = crossingsHz(r.lp, long(2.0 * sr), long(3.0 * sr), sr);
        double cents = 1200 * std::log2(std::max(f, .01) / fc);
        double amp = rms(r.lp, long(2.0 * sr), long(3.0 * sr));
        if (fc <= 2000)
            CHECK(std::abs(cents) < 60, "self-osc %g Hz measured %g Hz (%+.1f cents)", fc, f,
                  cents);
        std::printf("  %6g Hz: %8.2f Hz (%+6.1f cents), RMS %g\n", fc, f, cents, amp);
    }
}

static void t_alias() {
    const long N = 65536;
    const double sr = 48000;
    std::printf("alias level, saw 5432 Hz through the voice:\n");
    for (int os : {1, 2, 4}) {
        VoiceEngine e;
        Parameters p = voice();
        p.osc2Level = 0;
        p.oversample = os;
        p.cutoff = 21600;
        std::vector<float> x(N);
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < N; ++i) {
            in[0].pitch = pitchFor(5432.0);
            in[0].gate = 10;
            e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        double db = aliasDb(x, 5432.0, sr);
        std::printf("  os %dx: %+6.1f dB\n", os, db);
        if (os == 4)
            CHECK(db < -20, "4x saw alias %g dB should be < -20", db);
    }
    std::printf("alias level, 997 Hz sine through overdrive (drive 1):\n");
    for (int os : {1, 2, 4}) {
        VoiceEngine e;
        Parameters p = voice();
        p.osc1Level = p.osc2Level = p.noiseLevel = 0;
        p.extLevel = 1;
        p.oversample = os;
        p.cutoff = 21600;
        p.drive = 1;
        std::vector<float> x(N);
        std::array<Input, 8> in{};
        std::array<float, 8> o, l, b;
        for (long i = 0; i < long(.5 * sr); ++i) {  // let the transient settle
            in[0].gate = 10;
            in[0].external = float(.5 * std::sin(2 * dspemu::rate::pi * 997.0 * i / sr));
            e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, o, l, b);
        }
        for (long i = 0; i < N; ++i) {
            in[0].gate = 10;
            in[0].external = float(.5 * std::sin(2 * dspemu::rate::pi * 997.0 * i / sr));
            e.process(in, 1, p, FilterCalibration{}, sr, 0, 0, o, l, b);
            x[i] = o[0];
        }
        double db = aliasDb(x, 997.0, sr);
        std::printf("  os %dx: %+6.1f dB\n", os, db);
        if (os == 4)
            CHECK(db < -45, "4x overdrive alias %g dB should be < -45", db);
        if (os == 2)
            CHECK(db < -60, "2x overdrive alias %g dB should be < -60", db);
    }
}

static void t_sampleRates() {
    // The same voice must behave (and stay finite) at 44.1/48/96 kHz.
    for (double sr : {44100., 48000., 96000.}) {
        VoiceEngine e;
        Parameters p = voice();
        p.resonance = .99f;
        p.osc2Level = 1;
        p.noiseLevel = .3f;
        Run r = run(e, p, FilterCalibration{}, sr, 1.0, 1, [](long, auto &in) {
            in[0].pitch = 1;
            in[0].gate = 10;
        });
        bool finite = true;
        for (float x : r.out)
            finite &= std::isfinite(x);
        CHECK(finite && !e.hasDenormals(), "non-finite or denormal at sr=%g", sr);
        // Self-osc pitch at 1 kHz should be sample-rate independent.
        VoiceEngine e2;
        Parameters q = voice();
        q.osc1Level = q.osc2Level = q.noiseLevel = 0;
        q.resonance = 1;
        q.cutoff = 1000;
        q.cutoffDrive = 0;
        Run s = run(e2, q, FilterCalibration{}, sr, 3.0, 1, [](long, auto &in) { in[0].gate = 10; });
        double f = crossingsHz(s.lp, long(2.0 * sr), long(3.0 * sr), sr);
        CHECK(std::abs(f - 1000) < 25, "self-osc at sr=%g measured %g Hz", sr, f);
        std::printf("sr %g: finite, self-osc 1 kHz measured %g Hz\n", sr, f);
    }
}

int main() {
    t_garbageInputs();
    t_stability();
    t_silence();
    t_vco();
    t_envelopes();
    t_filterResponse();
    t_selfOsc();
    t_alias();
    t_sampleRates();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
