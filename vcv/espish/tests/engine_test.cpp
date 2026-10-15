// ESPish engine tests: MAME parity of the fixed-point layer, frequency-shifter
// accuracy, bounded feedback, reverb decay and freeze, ESP-rate band-limiting,
// echo timing and memory budget, ping-pong, capture, denormals, CPU.
#include "../src/engine/Fx.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
using namespace espish;

// ---- Copied from MAME src/devices/cpu/es5510/es5510.cpp at 016b077 (BSD-3-Clause,
// Christian Brunschen), reduced to what the parity checks need. ----
namespace mame {
constexpr uint8_t FLAG_N = 1 << 7, FLAG_C = 1 << 6, FLAG_V = 1 << 5, FLAG_LT = 1 << 4, FLAG_Z = 1 << 3;
constexpr uint8_t setFlagTo(uint8_t ccr, uint8_t flag, bool set) { return set ? (ccr | flag) : (ccr & ~flag); }
inline bool isFlagSet(uint8_t ccr, uint8_t flag) { return (ccr & flag) != 0; }
inline int32_t add(int32_t a, int32_t b, uint8_t& flags) {
    int32_t const aSign = a & 0x00800000, bSign = b & 0x00800000, result = a + b, resultSign = result & 0x00800000;
    bool const overflow = (aSign == bSign) && (aSign != resultSign);
    bool const carry = result & 0x01000000, negative = resultSign != 0;
    bool const lessThan = (overflow && !negative) || (!overflow && negative);
    flags = setFlagTo(flags, FLAG_C, carry); flags = setFlagTo(flags, FLAG_N, negative);
    flags = setFlagTo(flags, FLAG_Z, (result & 0x00ffffff) == 0); flags = setFlagTo(flags, FLAG_V, overflow);
    flags = setFlagTo(flags, FLAG_LT, lessThan);
    return result & 0x00ffffff;
}
inline int32_t saturate(int32_t value, uint8_t& flags, bool negative) {
    if (isFlagSet(flags, FLAG_V)) return negative ? 0x00800000 : 0x007fffff;
    return value;
}
constexpr int32_t negate(int32_t value) { return ((value ^ 0x00ffffff) + 1) & 0x00ffffff; }
inline int32_t aluADD(int32_t a, int32_t b) { uint8_t f = 0; int32_t t = add(a, b, f); return saturate(t, f, (a & 0x00800000) != 0); }
inline int32_t aluSUB(int32_t a, int32_t b) { uint8_t f = 0; int32_t t = add(a, negate(b), f); return saturate(t, f, (a & 0x00800000) != 0); }
inline int32_t aluADDU(int32_t a, int32_t b) { uint8_t f = 0; return add(a, b, f); }
inline int16_t round_24_to_16(int32_t dol24) { return int16_t(dol24 >> 8); }  // DOL_ROUND_TOWARDS_ZERO 0
inline int32_t macWriteBack(int32_t c24, int32_t d24, int mulshift) {           // mulacc, no accumulate
    int64_t product = (int64_t(sext24(uint32_t(c24))) * int64_t(sext24(uint32_t(d24)))) * (int64_t(1) << mulshift);
    bool overflow = product < -(int64_t(1) << 47) || product >= (int64_t(1) << 47);
    return overflow ? (product < 0 ? 0x00800000 : 0x007fffff) : int32_t((uint64_t(product) & 0x0000ffffff000000ULL) >> 24);
}
} // namespace mame

static double goertzel(const std::vector<float>& x, double hz, double sr) {
    // Hann-windowed single-bin DFT magnitude.
    double w = 2 * PI * hz / sr, re = 0, im = 0, n = double(x.size());
    for (size_t i = 0; i < x.size(); i++) { double h = .5 - .5 * std::cos(2 * PI * i / n); re += x[i] * h * std::cos(w * i); im -= x[i] * h * std::sin(w * i); }
    return std::sqrt(re * re + im * im);
}
static double db(double a, double b) { return 20 * std::log10(std::max(a, 1e-30) / std::max(b, 1e-30)); }
static double rmsOf(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; i++) s += double(v[i]) * v[i]; return std::sqrt(s / double(b - a)); }

static void fixedPointParity() {
    std::mt19937 g(5510); long checks = 0;
    std::vector<uint32_t> edges = {0, 1, 0x7FFFFF, 0x800000, 0x800001, 0xFFFFFF, 0x400000, 0xC00000, 0xFF, 0x100, 0xFFFF00};
    auto check = [&](uint32_t a, uint32_t b) {
        int32_t sa = sext24(a), sb = sext24(b);
        assert((uint32_t(sat24(int64_t(sa) + sb)) & 0xFFFFFF) == uint32_t(mame::aluADD(int32_t(a), int32_t(b))));
        if (b != 0x800000) assert((uint32_t(sat24(int64_t(sa) - sb)) & 0xFFFFFF) == uint32_t(mame::aluSUB(int32_t(a), int32_t(b))));
        else assert(mame::aluSUB(int32_t(a), int32_t(b)) == mame::aluADD(int32_t(a), int32_t(b)));  // quirk: negating -2^23 wraps, so SUB of it adds
        assert((uint32_t(sa + sb) & 0xFFFFFF) == uint32_t(mame::aluADDU(int32_t(a), int32_t(b))));       // ADDU wraps
        assert(toDelayWord(sa) == mame::round_24_to_16(int32_t(a)));
        assert((uint32_t(mul24(sa, sb)) & 0xFFFFFF) == uint32_t(mame::macWriteBack(int32_t(a), int32_t(b), 1)));
        checks += 5;
    };
    for (auto a : edges) for (auto b : edges) check(a, b);
    for (int i = 0; i < 2000000; i++) check(g() & 0xFFFFFF, g() & 0xFFFFFF);
    // The policies use exactly these helpers.
    // The float fast paths equal the integer helpers on random and edge inputs.
    for (int i = 0; i < 2000000; i++) {
        float x = std::uniform_real_distribution<float>(-1.5f, 1.5f)(g) * (i % 3 == 0 ? 1e-4f : 1.f);
        assert(EspMath::node(x) == fromQ23(toQ23(x)) && EspMath::store(x) == fromQ23(fromDelayWord(toDelayWord(toQ23(x)))));
    }
    for (float x : {0.f, .5f, -.5f, 1.f, -1.f, 2.f, -2.f, 1e-9f, -1e-9f, .123456789f, 8388607.f / 8388608.f, -1e-30f}) {
        assert(EspMath::node(x) == fromQ23(toQ23(x)) && EspMath::node(EspMath::node(x)) == EspMath::node(x));
        assert(EspMath::store(x) == fromQ23(fromDelayWord(toDelayWord(toQ23(x)))));
        float n = EspMath::node(x);
        if (x >= 1) assert(n == float(MAX24 / Q23)); else if (x <= -1) assert(n == -1.f);  // saturates
        else assert(n <= x && n > x - float(1 / Q23) - 1e-12f);                          // floors to the 24-bit grid
    }
    assert(EspMath::store(-1e-9f) < 0);  // round toward -infinity: tiny negatives become -1 LSB
    std::cout << "fixed point: " << checks << " checks match MAME ADD/SUB (saturating; SUB of -2^23 acts as ADD, as in MAME), ADDU (wrapping), DOL write, MAC (mulshift 1)\n";
}

static void shifterAccuracy() {
    const double sr = 48000;
    double worst = 1e9;
    for (double f0 : {150.0, 1000.0, 5000.0, 9500.0}) for (double shift : {60.0, -60.0, 400.0}) {
        if (f0 + shift < 100 || f0 + shift > 10000 || f0 - shift < 50) continue;
        FreqShifter fs; std::vector<float> out;
        for (int i = 0; i < 96000; i++) { float y = fs.tick(float(std::sin(2 * PI * f0 * i / sr)), float(shift), float(sr)); if (i >= 48000) out.push_back(y); }
        double want = goertzel(out, f0 + shift, sr), image = goertzel(out, f0 - shift, sr), carrier = goertzel(out, f0, sr);
        double sup = db(want, std::max(image, carrier)); worst = std::min(worst, sup);
        // Output frequency: the wanted bin dominates its neighbours one bin either side.
        assert(want > goertzel(out, f0 + shift + 2, sr) && want > goertzel(out, f0 + shift - 2, sr));
        assert(sup >= 40);
    }
    std::cout << "frequency shifter: worst unwanted sideband/carrier " << worst << " dB below wanted (100 Hz-10 kHz)\n";
}

static Params maxed(bool esp) {
    Params p; p.esp = esp;
    p.echo = {2.f, 1.1f, 1.f, 2000.f, 1.f, false};
    p.space = {.25f, 1.f, 30.f, 1.f, 0.f, 1.f, false};
    p.echoMix = p.spaceMix = p.mix = 1;
    return p;
}

static void stability() {
    for (double sr : {48000.0, 44100.0, 96000.0}) for (bool esp : {false, true}) {
        Engine e; e.prepare(sr); Params p = maxed(esp); std::mt19937 g(1);
        const int seconds = sr == 48000 ? 60 : 10;
        float peak = 0;
        for (int i = 0; i < int(seconds * sr); i++) {
            float x = i < sr / 2 ? std::uniform_real_distribution<float>(-1, 1)(g) : 0.f;
            auto y = e.tick(x, x, p);
            assert(std::isfinite(y[0]) && std::isfinite(y[1]));
            peak = std::max(peak, std::max(std::abs(y[0]), std::abs(y[1])));
        }
        assert(peak < 8.f);  // bounded: limiter ceiling 2, through mixes and a stereo FDN
        // Lowering feedback below 100% lets it die away.
        p.echo.feedback = .9f; p.space.decaySec = 1.5f;
        std::vector<float> tail;
        for (int i = 0; i < int(40 * sr); i++) tail.push_back(e.tick(0, 0, p)[0]);
        double end = rmsOf(tail, tail.size() - size_t(sr), tail.size());
        assert(esp ? end < 1e-3 : end < 1e-6);
        if (sr == 48000) std::cout << (esp ? "ESP" : "CLEAN") << " stability: peak " << peak << " over 60 s at max settings, tail after release " << end << "\n";
    }
}

// Schroeder backward integration; RT60 from the -5 to -35 dB slope.
static double rt60(const std::vector<float>& ir, double sr) {
    std::vector<double> e(ir.size()); double acc = 0;
    for (size_t i = ir.size(); i-- > 0;) { acc += double(ir[i]) * ir[i]; e[i] = acc; }
    auto level = [&](size_t i) { return 10 * std::log10(e[i] / e[0]); };
    size_t a = 0, b = 0;
    while (a < e.size() && level(a) > -5) a++;
    b = a; while (b < e.size() && level(b) > -35) b++;
    return double(b - a) / sr * 2;  // 30 dB span -> 60 dB
}

static void reverb() {
    const double sr = 48000;
    for (bool esp : {false, true}) for (float decay : {1.f, 3.f}) {
        Engine e; e.prepare(sr); Params p; p.esp = esp; p.echoMix = 0; p.spaceMix = 1; p.mix = 1;
        p.space = {0.f, .6f, decay, .7f, 0.f, 0.f, false};
        // 50 ms noise burst, then measure the free decay (well above ESP's 16-bit floor).
        std::mt19937 g(4); std::vector<float> ir;
        for (int i = 0; i < int(sr * .05); i++) { float x = std::uniform_real_distribution<float>(-.7f, .7f)(g); e.tick(x, x, p); }
        for (int i = 0; i < int(sr * decay * 1.6); i++) ir.push_back(e.tick(0, 0, p)[0]);
        double t = rt60(ir, sr);
        std::cout << (esp ? "ESP" : "CLEAN") << " RT60 set " << decay << " s, measured " << t << " s\n";
        assert(std::abs(t - decay) < .15 * decay);
    }
    for (bool esp : {false, true}) {
        Engine e; e.prepare(sr); Params p; p.esp = esp; p.echoMix = 0; p.spaceMix = 1; p.mix = 1;
        p.space = {0.f, .7f, 4.f, .7f, .2f, .5f, false};
        std::mt19937 g(9);
        for (int i = 0; i < int(sr); i++) { float x = std::uniform_real_distribution<float>(-.3f, .3f)(g); e.tick(x, x, p); }
        p.space.freeze = true;
        std::vector<float> out;
        for (int i = 0; i < int(31 * sr); i++) out.push_back(e.tick(.9f, -.9f, p)[0]);  // input must be ignored
        double first = rmsOf(out, size_t(sr), size_t(2 * sr)), worst = 0;
        for (int s = 2; s < 31; s++) worst = std::max(worst, std::abs(db(rmsOf(out, size_t(s * sr), size_t((s + 1) * sr)), first)));
        std::cout << (esp ? "ESP" : "CLEAN") << " FREEZE: level drift over 30 s " << worst << " dB\n";
        assert(worst < (esp ? 3.0 : 1.0));
    }
}

static void espBandLimit() {
    const double sr = 48000;
    auto through = [&](double hz) {
        Engine e; e.prepare(sr); Params p; p.esp = true; p.echo.timeSec = .01f; p.echo.feedback = 0; p.echoMix = 1; p.spaceMix = 0; p.mix = 1;
        std::vector<float> out;
        for (int i = 0; i < 96000; i++) { float y = e.tick(.5f * float(std::sin(2 * PI * hz * i / sr)), 0, p)[0]; if (i >= 48000) out.push_back(y); }
        return out;
    };
    auto pass = through(1000), high = through(20000);
    double passDb = db(rmsOf(pass, 0, pass.size()), .5 / std::sqrt(2.0));
    double stopDb = db(rmsOf(high, 0, high.size()), .5 / std::sqrt(2.0));
    double alias = db(goertzel(high, espRates[0] - 20000, sr), goertzel(pass, 1000, sr));  // where 20 kHz would fold (~9.76 kHz)
    std::cout << "ESP 29.76 kHz path: 1 kHz " << passDb << " dB, 20 kHz input " << stopDb << " dB, folded image " << alias << " dB\n";
    assert(std::abs(passDb) < .5 && stopDb < -40 && alias < -40);
}

static void echoTimingAndBudget() {
    const double sr = 48000;
    for (bool esp : {false, true}) for (float t : {.123f, .5f, 2.f}) {
        Engine e; e.prepare(sr); Params p; p.esp = esp; p.espBudget = true; p.echo.timeSec = t; p.echo.feedback = 0; p.echoMix = 1; p.spaceMix = 0; p.mix = 1;
        std::vector<float> out;
        for (int i = 0; i < int(2.5 * sr); i++) out.push_back(e.tick(i == 0 ? .5f : 0.f, 0, p)[0]);
        size_t peak = 0; for (size_t i = 1; i < out.size(); i++) if (std::abs(out[i]) > std::abs(out[peak])) peak = i;
        double expect = std::min(double(t), e.echoLimitSeconds(p));
        double got = double(peak) / sr;
        assert(std::abs(got - expect) < (esp ? .003 : .0002));  // ESP adds converter latency (~2 ms)
    }
    Engine e; e.prepare(48000); Params p; p.esp = true; p.espBudget = true;
    assert(std::abs(e.echoLimitSeconds(p) - 32768 / espRates[0]) < 1e-9);
    p.espBudget = false; assert(e.echoLimitSeconds(p) == 2.0);
    // Ping-pong: an impulse in L comes back L, then R, then L.
    Engine pp; pp.prepare(48000); Params q; q.echo.timeSec = .1f; q.echo.feedback = .8f; q.echo.spread = 1; q.echoMix = 1; q.spaceMix = 0; q.mix = 1;
    std::vector<std::array<float, 2>> out;
    for (int i = 0; i < 48000; i++) out.push_back(pp.tick(i == 0 ? .5f : 0.f, 0, q));
    auto energy = [&](int c, int from) { double s = 0; for (int i = from; i < from + 2400; i++) s += out[i][c] * out[i][c]; return s; };
    assert(energy(0, 3600) > 10 * energy(1, 3600) && energy(1, 8400) > 10 * energy(0, 8400) && energy(0, 13200) > 10 * energy(1, 13200));
    std::cout << "echo: timing within 0.2 ms (CLEAN) / 3 ms (ESP), ESP budget " << 32768 / espRates[0] << " s, ping-pong alternates\n";
}

static void captureBuffer() {
    Capture c; c.prepare(1000);
    assert(!c.trigger(100));  // nothing recorded yet
    for (int i = 0; i < 1500; i++) c.write(float(i % 997) / 32768.f);
    assert(c.trigger(300));
    assert(!c.trigger(300));  // still waiting for the UI
    std::vector<int16_t> got; assert(c.collect(got) && got.size() == 300);
    for (int i = 0; i < 300; i++) assert(got[i] == int16_t((1200 + i) % 997));
    assert(!c.collect(got));
    for (int i = 0; i < 50; i++) c.write(-.5f);
    assert(c.trigger(400) && c.collect(got) && got.size() == 50 && got[0] == -16384);  // only what the new ring holds
    std::cout << "capture: oldest-first copy of the last N frames, one pending capture at a time\n";
}

static void silenceSettles() {
    Engine e; e.prepare(48000); Params p; p.echo.feedback = .5f; p.space.decaySec = .5f; p.echoMix = p.spaceMix = p.mix = 1;
    for (int i = 0; i < 4800; i++) e.tick(.5f, -.5f, p);
    for (int i = 0; i < 48000 * 60; i++) {
        e.tick(0, 0, p);
        if (i % 48000 == 0) for (auto& l : e.spaceClean.line) for (float v : l.buf) assert(std::fpclassify(v) != FP_SUBNORMAL);
    }
    for (auto& l : e.echoClean.line) for (float v : l.buf) assert(v == 0);
    for (auto& l : e.spaceClean.line) for (float v : l.buf) assert(v == 0);
    std::cout << "CLEAN: silence settles to exact zero (no denormals)\n";
}

static void cpu() {
    for (bool esp : {false, true}) {
        Engine e; e.prepare(48000); Params p; p.esp = esp; p.echo.shiftHz = 30; p.echoMix = p.spaceMix = p.mix = 1;
        std::mt19937 g(3); auto t0 = std::chrono::steady_clock::now(); float acc = 0;
        for (int i = 0; i < 48000 * 5; i++) { float x = std::uniform_real_distribution<float>(-.2f, .2f)(g); acc += e.tick(x, x, p)[0]; }
        double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 5;
        std::cout << (esp ? "ESP" : "CLEAN") << " CPU: " << 100 * share << "% of one core at 48 kHz (" << acc << ")\n";
        assert(share < .10);
    }
}

int main() {
    std::cout << std::unitbuf;  // keep progress output if an assertion aborts
    fixedPointParity();
    shifterAccuracy();
    echoTimingAndBudget();
    captureBuffer();
    espBandLimit();
    reverb();
    silenceSettles();
    stability();
    cpu();
    std::cout << "PASS: ESPish engine\n";
}
