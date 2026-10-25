// Portable checks on the Séance engine (no Rack): the FFT against a direct DFT, exact
// reconstruction through the frame at every size, the fixed latency, the hand-off protocol
// under arbitrary module ordering and under a stalled chain, RESET continuity, and each of
// the four operations (freeze, scramble, cross, wipe) measured on tones and noise.
#include "../src/engine/Spectral.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>
using namespace seance;

static const float SR = 48000;

static std::vector<float> noise(size_t n, uint32_t seed, float amp = .5f) {
    std::mt19937 rng(seed); std::uniform_real_distribution<float> u(-amp, amp);
    std::vector<float> v(n); for (auto& x : v) x = u(rng); return v;
}
static std::vector<float> sine(size_t n, float hz, float amp = .5f) {
    std::vector<float> v(n); for (size_t i = 0; i < n; i++) v[i] = amp * float(std::sin(2 * PI * hz * i / SR)); return v;
}
// Amplitude of a tone at `hz` over buf[start, start+len), by projection onto sin and cos.
static float tone(const std::vector<float>& buf, size_t start, size_t len, float hz) {
    double s = 0, c = 0;
    for (size_t i = 0; i < len; i++) { double ph = 2 * PI * hz * (start + i) / SR; s += buf[start + i] * std::sin(ph); c += buf[start + i] * std::cos(ph); }
    return float(2 * std::sqrt(s * s + c * c) / len);
}
static float rms(const std::vector<float>& buf, size_t start, size_t len) {
    double e = 0; for (size_t i = 0; i < len; i++) e += double(buf[start + i]) * buf[start + i]; return float(std::sqrt(e / len));
}
static float dB(float x) { return 20 * std::log10(std::max(x, 1e-12f)); }
// Fraction of the signal's power that lies between loHz and hiHz, from Hann-windowed
// 4096-point spectra of buf[start, start+len).
static float bandFraction(const std::vector<float>& buf, size_t start, size_t len, float loHz, float hiHz) {
    Fft fft; fft.setSize(4096);
    std::vector<float> re(4096), im(4096);
    double band = 0, total = 0;
    for (size_t s = start; s + 4096 <= start + len; s += 1024) {
        for (int i = 0; i < 4096; i++) { re[i] = buf[s + i] * float(.5 - .5 * std::cos(2 * PI * i / 4096)); im[i] = 0; }
        fft.transform(re.data(), im.data(), false);
        for (int k = 0; k <= 2048; k++) { double pw = double(re[k]) * re[k] + double(im[k]) * im[k]; float hz = k * SR / 4096; total += pw; if (hz >= loHz && hz <= hiHz) band += pw; }
    }
    return total > 0 ? float(band / total) : 0.f;
}

// Runs a core with no chain over `in`, returning the output.
static std::vector<float> runPlain(Core& core, const std::vector<float>& in, const CoreParams& p, const std::vector<float>* b = nullptr) {
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); i++) out[i] = core.tick(in[i], b ? (*b)[i] : 0.f, p, false);
    return out;
}
// Runs a core with one expander (always position 1 and last) applying `op` each frame.
template <class Op>
static std::vector<float> runChain(Core& core, const std::vector<float>& in, const CoreParams& p, Op&& op, const std::vector<float>* b = nullptr) {
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        out[i] = core.tick(in[i], b ? (*b)[i] : 0.f, p, true);
        serve(core.frame, 1, true, false, op);
    }
    return out;
}

static void testFft() {
    Fft fft;
    for (int n : {64, 512, 4096}) {
        fft.setSize(n);
        auto re = noise(n, 1), im = noise(n, 2);
        std::vector<float> r0 = re, i0 = im;
        fft.transform(re.data(), im.data(), false);
        // Direct DFT of a few bins.
        double worst = 0;
        for (int k : {0, 1, n / 3, n / 2, n - 1}) {
            double sr = 0, si = 0;
            for (int j = 0; j < n; j++) { double a = -2 * PI * double(k) * j / n; sr += r0[j] * std::cos(a) - i0[j] * std::sin(a); si += r0[j] * std::sin(a) + i0[j] * std::cos(a); }
            worst = std::max({worst, std::abs(sr - re[k]), std::abs(si - im[k])});
        }
        assert(worst < 2e-3 * std::sqrt(n));  // float accumulation over n terms
        fft.transform(re.data(), im.data(), true);
        double rt = 0; for (int j = 0; j < n; j++) rt = std::max({rt, double(std::abs(re[j] - r0[j])), double(std::abs(im[j] - i0[j]))});
        assert(rt < 1e-4);
        std::printf("  fft %4d: dft error %.2e, round trip %.2e\n", n, worst, rt);
    }
}

static void testReconstruction() {
    for (int s = 0; s < SIZES; s++) {
        Core core; CoreParams p; p.sizeIndex = s; core.setSize(s);
        auto in = noise(4 * SR, 10 + s);
        auto out = runPlain(core, in, p);
        int D = core.latency(); assert(D == sizeOf(s) - 1 + SLACK);
        double worst = 0;
        for (size_t i = D + 2 * sizeOf(s); i < in.size(); i++) worst = std::max(worst, double(std::abs(out[i] - in[i - D])));
        assert(worst < 2e-4);
        // Fully dry: the dry path is delayed to match, exactly.
        Core core2; core2.setSize(s); CoreParams dry = p; dry.mix = 0;
        auto out2 = runPlain(core2, in, dry);
        for (size_t i = D; i < in.size(); i++) assert(out2[i] == in[i - D]);
        // Latency: an impulse comes out exactly D later.
        Core core3; core3.setSize(s); std::vector<float> imp(20000 + 3 * sizeOf(s), 0.f); imp[2000] = 1;
        auto out3 = runPlain(core3, imp, p);
        size_t peak = 0; for (size_t i = 1; i < out3.size(); i++) if (std::abs(out3[i]) > std::abs(out3[peak])) peak = i;
        assert(peak == 2000 + size_t(D) && std::abs(out3[peak] - 1) < 1e-3);
        assert(!core.stalled && core.stalls == 0);
        std::printf("  size %4d: reconstruction error %.2e, latency %d samples (%.1f ms)\n", sizeOf(s), worst, D, 1000.f * D / SR);
    }
}

// Three identity expanders served in a random order each step: the frame still makes
// the round trip and the output is still the delayed input. Then nobody serves: the core
// times out after SLACK samples and resynthesises what it has.
static void testHandoff() {
    Core core; CoreParams p;
    auto in = noise(2 * SR, 20);
    std::vector<float> out(in.size());
    std::mt19937 rng(5);
    int served[3] = {0, 0, 0};
    for (size_t i = 0; i < in.size(); i++) {
        int order[3] = {0, 1, 2}; std::shuffle(order, order + 3, rng);
        // Sometimes the expanders run before the core in a step, sometimes after.
        bool coreFirst = rng() & 1;
        if (coreFirst) out[i] = core.tick(in[i], 0, p, true);
        for (int k : order) if (serve(core.frame, k + 1, k == 2, false, [](FrameV1&) {})) served[k]++;
        if (!coreFirst) out[i] = core.tick(in[i], 0, p, true);
    }
    int D = core.latency(); double worst = 0;
    for (size_t i = D + 2 * core.size; i < in.size(); i++) worst = std::max(worst, double(std::abs(out[i] - in[i - D])));
    assert(worst < 2e-4 && core.stalls == 0 && served[0] == served[1] && served[1] == served[2] && served[0] > 100);
    assert(core.frame.length.load() == 3);
    std::printf("  hand-off: %d frames through 3 expanders in random order, error %.2e, no stalls\n", served[0], worst);

    Core stalled; auto outS = runPlain(stalled, in, p);  // reference, no chain
    Core core2; std::vector<float> out2(in.size());
    for (size_t i = 0; i < in.size(); i++) out2[i] = core2.tick(in[i], 0, p, true);  // a chain that never answers
    worst = 0; for (size_t i = D + 2 * core2.size; i < in.size(); i++) worst = std::max(worst, double(std::abs(out2[i] - outS[i])));
    assert(core2.stalls > 100 && core2.stalled && worst < 1e-5);
    std::printf("  stall: %d frames timed out after %d samples, output identical to the unchained core\n", core2.stalls, SLACK);

    // A bypassed expander passes the frame without touching it.
    Core core3; auto out3 = runChain(core3, in, p, [](FrameV1& f) { for (int k = 0; k < f.bins; k++) f.mag[k] = 0; });
    assert(rms(out3, D + 4 * core3.size, SR) < 1e-4);  // the op silences everything
    Core core4; std::vector<float> out4(in.size());
    for (size_t i = 0; i < in.size(); i++) { out4[i] = core4.tick(in[i], 0, p, true); serve(core4.frame, 1, true, true, [](FrameV1& f) { for (int k = 0; k < f.bins; k++) f.mag[k] = 0; }); }
    worst = 0; for (size_t i = D + 2 * core4.size; i < in.size(); i++) worst = std::max(worst, double(std::abs(out4[i] - in[i - D])));
    assert(worst < 2e-4 && core4.stalls == 0);
    std::printf("  bypass: a bypassed expander passes the frame unchanged (error %.2e)\n", worst);
}

static void testReset() {
    Core core; CoreParams p;
    auto in = noise(3 * SR, 30);
    std::vector<float> out(in.size());
    size_t resetAt = SR + 333;  // off the frame grid
    for (size_t i = 0; i < in.size(); i++) { if (i == resetAt) core.reset(); out[i] = core.tick(in[i], 0, p, false); }
    int D = core.latency(); double worst = 0, after = 0;
    for (size_t i = D + 2 * core.size; i < in.size(); i++) {
        double e = std::abs(out[i] - in[i - D]);
        if (i >= resetAt + D + core.size) after = std::max(after, e); else worst = std::max(worst, e);
    }
    // Through the transition the window sum deviates from nominal and is divided out; the
    // floor limits the correction, so a short dip of at most 6 dB is accepted.
    assert(worst < .5 * .5 + 1e-3 && after < 2e-4);
    // The next frame after RESET is frame 0, hop samples later.
    Core core2; std::vector<float> seen;
    for (size_t i = 0; i < size_t(4 * core2.size); i++) { if (i == 1000) core2.reset(); core2.tick(0, 0, p, false); if (i >= 1000 && core2.frame.serial == 0 && core2.frameTime == (long long)i) seen.push_back(float(i)); }
    assert(!seen.empty() && seen[0] == 1000 + core2.hop - 1);
    std::printf("  reset: continuity through the restart (worst %.3f during, %.2e after); frame 0 lands %d samples after RESET\n", worst, after, core2.hop);
}

static void testFreeze() {
    Core core; CoreParams p; Freeze fz;
    size_t n = 4 * SR, freezeAt = SR;
    auto in = sine(n, 440, .5f);
    for (size_t i = freezeAt + 2 * core.size; i < n; i++) in[i] = 0;  // the source stops after the hold
    std::vector<float> out(n);
    for (size_t i = 0; i < n; i++) {
        out[i] = core.tick(in[i], 0, p, true);
        bool hold = i >= freezeAt;
        serve(core.frame, 1, true, false, [&](FrameV1& f) { fz.process(f, hold, 0); });
    }
    // Two seconds into the hold the input has long been silent, yet the tone continues.
    size_t at = 3 * SR, len = SR / 2;
    float a = tone(out, at, len, 440), r = rms(out, at, len);
    float expect = .5f, expectRms = .5f / std::sqrt(2.f);
    assert(std::abs(dB(a / expect)) < 1.5f && std::abs(dB(r / expectRms)) < 1.5f);
    // Frequency from zero crossings.
    int zc = 0; for (size_t i = at + 1; i < at + len; i++) if ((out[i - 1] < 0) != (out[i] < 0)) zc++;
    float hz = zc * SR / (2.f * len);
    assert(std::abs(hz - 440) < 2);
    // Unfreezing with silence in: the output falls silent.
    Core core2; Freeze fz2; std::vector<float> in2 = in; std::vector<float> out2(n);
    for (size_t i = 0; i < n; i++) { out2[i] = core2.tick(in2[i], 0, p, true); bool hold = i >= freezeAt && i < 2 * SR; serve(core2.frame, 1, true, false, [&](FrameV1& f) { fz2.process(f, hold, 0); }); }
    assert(rms(out2, size_t(2.5 * SR), SR) < 1e-3f);
    std::printf("  freeze: held 440 Hz tone after the source stopped: %.1f dB, %.1f Hz by zero crossings; releases to silence\n", dB(a / expect), hz);
}

static void testScramble() {
    CoreParams p; p.seed = 7;
    auto in = noise(2 * SR, 40);
    Core core; Scramble sc;
    auto out = runChain(core, in, p, [&](FrameV1& f) { sc.process(f, 0, 0, 1); });
    int D = core.latency(); double worst = 0;
    for (size_t i = D + 2 * core.size; i < in.size(); i++) worst = std::max(worst, double(std::abs(out[i] - in[i - D])));
    assert(worst < 2e-4);  // range 0 and no phase offset is an identity
    Core c1, c2, c3; Scramble s1, s2, s3; CoreParams p2 = p; p2.seed = 8;
    auto o1 = runChain(c1, in, p, [&](FrameV1& f) { s1.process(f, .3f, .5f, 4); });
    auto o2 = runChain(c2, in, p, [&](FrameV1& f) { s2.process(f, .3f, .5f, 4); });
    auto o3 = runChain(c3, in, p2, [&](FrameV1& f) { s3.process(f, .3f, .5f, 4); });
    assert(o1 == o2);  // same seed: bit-identical
    double diff = 0; for (size_t i = D; i < in.size(); i++) diff += double(o1[i] - o3[i]) * (o1[i] - o3[i]);
    assert(std::sqrt(diff / in.size()) > .05);  // another seed: a different texture
    // A held tone keeps its level when its peak is moved coherently; at range 1 the tone is
    // somewhere else (here an octave band around 1 kHz, by the seeded offset), not at 1 kHz.
    Core c4; Scramble s4; auto tin = sine(2 * SR, 1000, .5f);
    auto o4 = runChain(c4, tin, p, [&](FrameV1& f) { s4.process(f, 1, 0, 1000); });
    size_t at = D + 2 * c4.size;
    float still = tone(o4, at, SR / 2, 1000), level = rms(o4, at, SR / 2) / (.5f / std::sqrt(2.f));
    assert(still < .05f && std::abs(dB(level)) < .5f);
    // Noise is not made of coherent partials; moving its peaks costs some level. Bounded.
    float e0 = rms(in, 0, in.size()), e1 = rms(o1, D + 2 * c1.size, SR);
    assert(dB(e1 / e0) > -6 && dB(e1 / e0) < 1);
    std::printf("  scramble: identity at range 0; seeded replay bit-identical, seeds differ (%.3f rms); held tone moved at %+.2f dB (%.0f dB left at 1 kHz); noise %.1f dB\n", std::sqrt(diff / in.size()), dB(level), dB(still / .5f), dB(e1 / e0));
}

static void testCross() {
    CoreParams p;
    auto a = noise(2 * SR, 50), b = sine(2 * SR, 1000, .5f);
    Core core; Cross cr;
    auto out = runChain(core, a, p, [&](FrameV1& f) { cr.process(f, 1, 0, false); }, &b);
    int D = core.latency(); size_t at = D + 2 * core.size, len = SR;
    // B's envelope is a line at 1 kHz: the noise is shaped into a narrow band around it.
    float frac = bandFraction(out, at, len, 900, 1100), total = rms(out, at, len);
    assert(frac > .9f);
    assert(dB(total / rms(a, 0, a.size())) > -12);  // level restored
    // Nothing patched to B: Cross passes the frame through at any depth.
    Core c5; Cross cr5; CoreParams noB = p; noB.hasB = false;
    auto o5 = runChain(c5, a, noB, [&](FrameV1& f) { cr5.process(f, 1, .5f, false); });
    double w5 = 0; for (size_t i = at; i < a.size(); i++) w5 = std::max(w5, double(std::abs(o5[i] - a[i - D])));
    assert(w5 < 2e-4);
    // Depth 0 is an identity in both modes.
    Core c2; Cross cr2; auto o2 = runChain(c2, a, p, [&](FrameV1& f) { cr2.process(f, 0, 0, false); }, &b);
    Core c3; Cross cr3; auto o3 = runChain(c3, a, p, [&](FrameV1& f) { cr3.process(f, 0, 0, true); }, &b);
    double w2 = 0, w3 = 0;
    for (size_t i = at; i < a.size(); i++) { w2 = std::max(w2, double(std::abs(o2[i] - a[i - D]))); w3 = std::max(w3, double(std::abs(o3[i] - a[i - D]))); }
    assert(w2 < 2e-4 && w3 < 2e-4);
    // Morph at depth 1 with smoothing: B's line widened over ±7 bins under A's phases is a
    // band of noise a few hundred hertz wide around 1 kHz.
    Core c4; Cross cr4; auto o4 = runChain(c4, a, p, [&](FrameV1& f) { cr4.process(f, 1, .4f, true); }, &b);
    float frac4 = bandFraction(o4, at, len, 600, 1400), narrow4 = bandFraction(o4, at, len, 980, 1020);
    assert(frac4 > .8f && narrow4 < .5f && rms(o4, at, len) > 1e-3f);
    std::printf("  cross: multiply shapes noise to B's line (%.0f%% of power within ±100 Hz, level %.1f dB); depth 0 identities %.1e/%.1e; morph band %.0f%%\n", 100 * frac, dB(total / rms(a, 0, a.size())), w2, w3, 100 * frac4);
}

static void testWipe() {
    CoreParams p;
    auto lo = sine(2 * SR, 200, .4f), hi = sine(2 * SR, 5000, .4f);
    std::vector<float> in(lo.size()); for (size_t i = 0; i < in.size(); i++) in[i] = lo[i] + hi[i];
    Core core; auto up = runChain(core, in, p, [](FrameV1& f) { wipe(f, .5f, 0); });
    int D = core.latency(); size_t at = D + 2 * core.size, len = SR;
    float lowLeft = tone(up, at, len, 200), highLeft = tone(up, at, len, 5000);
    assert(dB(lowLeft / .4f) < -40 && std::abs(dB(highLeft / .4f)) < 1);  // edge at 750 Hz for 2048 @ 48k
    Core c2; auto down = runChain(c2, in, p, [](FrameV1& f) { wipe(f, -.5f, 0); });
    float lowLeft2 = tone(down, at, len, 200), highLeft2 = tone(down, at, len, 5000);
    assert(std::abs(dB(lowLeft2 / .4f)) < 1 && dB(highLeft2 / .4f) < -40);
    Core c3; auto same = runChain(c3, in, p, [](FrameV1& f) { wipe(f, 0, .5f); });
    double worst = 0; for (size_t i = at; i < in.size(); i++) worst = std::max(worst, double(std::abs(same[i] - in[i - D])));
    assert(worst < 2e-4);
    // A tone just above the edge (1 kHz, edge 750 Hz) passes a hard wall whole and a soft
    // edge (two octaves wide) in part.
    auto mid = sine(2 * SR, 1000, .4f); std::vector<float> in3(in.size()); for (size_t i = 0; i < in.size(); i++) in3[i] = in[i] + mid[i];
    Core c4; auto hard = runChain(c4, in3, p, [](FrameV1& f) { wipe(f, .5f, 0); });
    Core c5; auto soft = runChain(c5, in3, p, [](FrameV1& f) { wipe(f, .5f, 1); });
    float whole = tone(hard, at, len, 1000), part = tone(soft, at, len, 1000);
    assert(std::abs(dB(whole / .4f)) < .5f && dB(part / .4f) < -.5f && dB(part / .4f) > -6);
    std::printf("  wipe: +0.5 leaves 200 Hz at %.0f dB and 5 kHz at %.1f dB; -0.5 the reverse (%.1f / %.0f dB); 0 is an identity; soft edge takes 1 kHz to %.1f dB\n", dB(lowLeft / .4f), dB(highLeft / .4f), dB(lowLeft2 / .4f), dB(highLeft2 / .4f), dB(part / .4f));
}

int main() {
    std::cout << std::unitbuf;
    std::printf("FFT\n"); testFft();
    std::printf("Reconstruction and latency\n"); testReconstruction();
    std::printf("Hand-off\n"); testHandoff();
    std::printf("Reset\n"); testReset();
    std::printf("Freeze\n"); testFreeze();
    std::printf("Scramble\n"); testScramble();
    std::printf("Cross\n"); testCross();
    std::printf("Wipe\n"); testWipe();
    std::printf("PASS: Séance engine: FFT, reconstruction at four sizes, latency, hand-off under random order, stall and bypass, reset, freeze, scramble, cross, wipe\n");
}
