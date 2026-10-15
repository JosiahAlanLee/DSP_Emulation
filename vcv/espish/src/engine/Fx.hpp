// SPDX-License-Identifier: BSD-3-Clause
// ESPish DSP: echo with an in-loop frequency shifter, an FDN space, and the two
// numeric engines. Portable C++17, no Rack headers. Signals are in "units" where
// 1.0 is full scale (Rack ±10 V maps to ±1).
#pragma once
#include "FixedPoint.hpp"
#include "RateConverter.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace espish {

constexpr double PI = 3.14159265358979323846;
inline float clampf(float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// Flush values far below audibility to zero, so silent feedback paths never go denormal.
inline float flush(float x) { return std::abs(x) < 1e-20f ? 0.f : x; }

// Circular delay line with fractional (4-point Hermite) reads.
struct DelayLine {
    std::vector<float> buf;
    size_t mask = 0, pos = 0;
    void allocate(size_t minLength) {
        size_t n = 16;
        while (n < minLength + 8) n <<= 1;
        buf.assign(n, 0.f);
        mask = n - 1;
        pos = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); pos = 0; }
    size_t capacity() const { return buf.empty() ? 0 : buf.size() - 8; }
    void write(float x) { buf[pos] = x; pos = (pos + 1) & mask; }
    float tap(size_t n) const { return buf[(pos - 1 - n) & mask]; }  // n samples ago (0 = newest)
    // `delay` samples ago, >= 1.
    float read(float delay) const {
        delay = clampf(delay, 1.f, float(capacity()) - 2.f);
        size_t i = size_t(delay);
        float f = delay - float(i);
        float y0 = tap(i - 1), y1 = tap(i), y2 = tap(i + 1), y3 = tap(i + 2);
        float c1 = .5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2 * y2 - .5f * y3, c3 = .5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

// Second-order Butterworth low-pass (no peaking: gain <= 1 everywhere).
struct Lowpass2 {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(float hz, float sr) {
        double w = std::tan(PI * std::min(double(hz), .45 * sr) / sr), q = std::sqrt(.5), n = 1 / (1 + w / q + w * w);
        b0 = float(w * w * n); b1 = 2 * b0; b2 = b0; a1 = float(2 * (w * w - 1) * n); a2 = float((1 - w / q + w * w) * n);
    }
    void clear() { z1 = z2 = 0; }
    float tick(float x) { float y = b0 * x + z1; z1 = flush(b1 * x - a1 * y + z2); z2 = flush(b2 * x - a2 * y); return y; }
};
struct OnePole {  // low-pass
    float z = 0;
    float tick(float x, float c) { z += c * (x - z); return z = flush(z); }
};
inline float onePoleCoef(float hz, float sr) { return float(1 - std::exp(-2 * PI * double(hz) / double(sr))); }

// Hilbert pair: two chains of second-order all-passes (in z^-2) whose outputs differ
// by ~90 degrees over most of the band. Coefficients: Olli Niemitalo's published
// 8th-order design (public domain). The first path is delayed one sample.
struct Hilbert {
    static constexpr double a1[4] = {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737};
    static constexpr double a2[4] = {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278};
    struct Section { float k = 0; float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float tick(float x) { float y = k * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = flush(y); return y; } };
    std::array<Section, 4> p1, p2; float delay = 0;
    Hilbert() { for (int i = 0; i < 4; i++) { p1[i].k = float(a1[i] * a1[i]); p2[i].k = float(a2[i] * a2[i]); } }
    void clear() { for (auto* p : {&p1, &p2}) for (auto& s : *p) { s.x1 = s.x2 = s.y1 = s.y2 = 0; } delay = 0; }
    // Returns {in-phase, quadrature}.
    std::array<float, 2> tick(float x) {
        float a = x, b = x;
        for (auto& s : p1) a = s.tick(a);
        for (auto& s : p2) b = s.tick(b);
        float i = delay; delay = a;
        return {{i, b}};
    }
};

// Single-sideband frequency shifter: every component moves by `hz` (may be negative).
struct FreqShifter {
    // Quadrature oscillator as a rotating phasor (no per-sample trig); the rotation
    // is recomputed only when the shift changes and the phasor is renormalized.
    Hilbert h; double c = 1, s = 0, rc = 1, rs = 0; float lastHz = 0, lastSr = 0; int sinceNorm = 0;
    void clear() { h.clear(); c = 1; s = 0; }
    float tick(float x, float hz, float sr) {
        if (hz != lastHz || sr != lastSr) { lastHz = hz; lastSr = sr; double w = 2 * PI * double(hz) / double(sr); rc = std::cos(w); rs = std::sin(w); }
        auto iq = h.tick(x);
        float y = iq[0] * float(c) + iq[1] * float(s);  // sign chosen so positive hz moves components up (tested)
        double nc = c * rc - s * rs; s = s * rc + c * rs; c = nc;
        if (++sinceNorm >= 256) { sinceNorm = 0; double m = 1 / std::sqrt(c * c + s * s); c *= m; s *= m; }
        return y;
    }
};

// Bounded feedback: smooth odd saturation, then a hard ceiling. Unity gain near zero.
struct SoftLimit {
    static constexpr float knee = 1.2f, ceiling = 2.f;
    static float tick(float x) { return clampf(knee * std::tanh(x / knee), -ceiling, ceiling); }
};

struct EchoParams {
    float timeSec = .3f, feedback = .4f, tone = 0, shiftHz = 0, spread = 0;
    bool shiftPost = false;
};

template <class M>
struct Echo {
    DelayLine line[2]; OnePole hp[2], lp[2]; Lowpass2 guard[2]; FreqShifter shift[2];
    float sr = 48000, smooth[2] = {0, 0}; bool primed = false; size_t maxSamples = 0;
    float glide = 0, hpC = 0, lpC = 0;  // per-rate constants, set in prepare()
    float limiterActivity = 0;  // 0..1, for a panel light
    void prepare(float rate, size_t maxDelaySamples) {
        sr = rate; maxSamples = maxDelaySamples;
        glide = 1 - std::exp(-1.f / (.05f * sr));  // tape-style 50 ms glide
        hpC = onePoleCoef(40, sr); lpC = onePoleCoef(2500, sr);
        for (auto& g : guard) g.set(std::min(18000.f, .4f * sr), sr);
        for (auto& l : line) l.allocate(maxDelaySamples + 4);
        clear();
    }
    void clear() { for (int c = 0; c < 2; c++) { line[c].clear(); hp[c].z = lp[c].z = 0; guard[c].clear(); shift[c].clear(); } primed = false; limiterActivity = 0; }
    std::array<float, 2> tick(float inL, float inR, const EchoParams& p) {
        float target = clampf(p.timeSec * sr, 2.f, float(maxSamples));
        if (!primed) { smooth[0] = smooth[1] = target; primed = true; }
        const float spread = clampf(p.spread, 0, 1), fb = clampf(p.feedback, 0, 1.1f), tone = clampf(p.tone, -1, 1);
        float wet[2], loop[2];
        for (int c = 0; c < 2; c++) {
            smooth[c] += glide * (target - smooth[c]);
            float y = line[c].read(smooth[c]);
            float x = y - hp[c].tick(y, hpC);                  // 40 Hz high-pass
            float low = lp[c].tick(x, lpC);                    // tone tilt
            // Tilt that never adds gain: dark blends toward the low-pass, bright removes
            // lows. Loop gain is therefore at most FEEDBACK, so below 100% it always decays.
            x = tone < 0 ? lerp(x, low, -tone) : x - tone * low;
            // Keep content away from Nyquist before shifting: a component there folds back
            // onto its own image and can double in power each pass (40 Hz high-pass above
            // does the same job at DC for downward shifts).
            x = guard[c].tick(x);
            if (!p.shiftPost) x = shift[c].tick(x, p.shiftHz, sr);
            x = M::node(x * fb);
            float lim = SoftLimit::tick(x);
            limiterActivity = std::max(limiterActivity * .9995f, std::abs(x) > .9f ? 1.f : 0.f);
            loop[c] = M::node(lim);
            wet[c] = p.shiftPost ? M::node(shift[c].tick(y, p.shiftHz, sr)) : y;
        }
        // SPREAD 0: two independent echoes; 1: ping-pong (mono input into L, crossed feedback).
        float mono = .5f * (inL + inR);
        float wl = lerp(inL, mono, spread) + lerp(loop[0], loop[1], spread);
        float wr = lerp(inR, 0.f, spread) + lerp(loop[1], loop[0], spread);
        line[0].write(M::store(M::node(wl)));
        line[1].write(M::store(M::node(wr)));
        return {{wet[0], wet[1]}};
    }
};

struct SpaceParams {
    float predelaySec = .02f, size = .5f, decaySec = 2.5f, diffusion = .6f, damping = .4f, modulation = .3f;
    bool freeze = false;
};

template <class M>
struct Space {
    static constexpr int N = 8;
    static constexpr double baseMs[N] = {31.7, 37.1, 41.1, 43.7, 53.0, 59.9, 67.7, 73.1};
    static constexpr double diffMs[2][4] = {{4.77, 3.59, 12.73, 9.31}, {4.53, 3.81, 12.37, 9.67}};
    static constexpr float maxSizeFactor = 2.f, maxModMs = 1.2f, maxPredelay = .25f;
    DelayLine pre[2], diff[2][4], line[N]; OnePole damp[N];
    float sr = 48000, len[N] = {}; bool primed = false;
    float glide = 0, gain[N] = {}, gainLen[N] = {}, gainRt = -1, dampSetting = -1, dampCoef = 1;
    size_t diffLen[2][4] = {}; int lfoNorm = 0;
    double lfoC[N] = {}, lfoS[N] = {}, lfoRc[N] = {}, lfoRs[N] = {};  // rotating-phasor LFOs
    void prepare(float rate) {
        sr = rate; glide = 1 - std::exp(-1.f / (.2f * sr));
        for (auto& p : pre) p.allocate(size_t(maxPredelay * sr) + 8);
        for (int c = 0; c < 2; c++) for (int k = 0; k < 4; k++) { diffLen[c][k] = size_t(diffMs[c][k] * 1e-3 * sr); diff[c][k].allocate(diffLen[c][k] + 8); }
        for (int i = 0; i < N; i++) line[i].allocate(size_t((baseMs[i] * maxSizeFactor + maxModMs) * 1e-3 * sr) + 8);
        for (int i = 0; i < N; i++) {
            double ph = 2 * PI * i / N, w = 2 * PI * (.1 + .07 * i) / sr;
            lfoC[i] = std::cos(ph); lfoS[i] = std::sin(ph); lfoRc[i] = std::cos(w); lfoRs[i] = std::sin(w);
        }
        clear();
    }
    void clear() {
        for (auto& p : pre) p.clear();
        for (auto& c : diff) for (auto& d : c) d.clear();
        for (int i = 0; i < N; i++) { line[i].clear(); damp[i].z = 0; }
        primed = false; gainRt = -1; dampSetting = -1;
    }
    std::array<float, 2> tick(float inL, float inR, const SpaceParams& p) {
        const float sf = .25f + 1.75f * clampf(p.size, 0, 1);
        if (p.freeze) inL = inR = 0;
        // Predelay, then series all-pass diffusion per channel.
        float x[2] = {inL, inR};
        const float pd = clampf(p.predelaySec, 0, maxPredelay) * sr;
        const float g = M::coef(.75f * clampf(p.diffusion, 0, 1));
        for (int c = 0; c < 2; c++) {
            pre[c].write(M::store(x[c]));
            float v = pd < 1 ? x[c] : pre[c].read(pd);
            for (int k = 0; k < 4; k++) {
                float z = diff[c][k].tap(diffLen[c][k] - 1);
                float w = M::node(v + g * z);
                diff[c][k].write(M::store(w));
                v = M::node(z - g * w);
            }
            x[c] = v;
        }
        // FDN: read, damp, scale for RT60, mix with a Householder matrix, inject, write.
        const float rt60 = std::max(.05f, p.decaySec);
        const float dampIn = clampf(p.damping, 0, 1);
        if (dampIn != dampSetting) { dampSetting = dampIn; dampCoef = onePoleCoef(500 + 19500 * (1 - dampIn) * (1 - dampIn), sr); }
        const float dampC = p.freeze ? 1.f : dampCoef;
        const bool newRt = rt60 != gainRt; gainRt = rt60;
        const float modDepth = p.freeze ? 0.f : clampf(p.modulation, 0, 1) * maxModMs * 1e-3f * sr;
        if (++lfoNorm >= 4096) { lfoNorm = 0; for (int i = 0; i < N; i++) { double m = 1 / std::sqrt(lfoC[i] * lfoC[i] + lfoS[i] * lfoS[i]); lfoC[i] *= m; lfoS[i] *= m; } }
        float v[N], sum = 0;
        for (int i = 0; i < N; i++) {
            float target = float(baseMs[i] * sf * 1e-3 * sr);
            if (!primed) len[i] = target;
            len[i] += glide * (target - len[i]);
            float d = len[i];
            if (i % 2 == 0) {  // four modulated lines, staggered phases and rates
                double nc = lfoC[i] * lfoRc[i] - lfoS[i] * lfoRs[i]; lfoS[i] = lfoS[i] * lfoRc[i] + lfoC[i] * lfoRs[i]; lfoC[i] = nc;
                if (modDepth > 0) d += modDepth * (1 + float(lfoS[i])) * .5f;
            }
            if (p.freeze) d = std::round(d);  // integer reads: frozen tails keep their energy
            float z = line[i].read(d);
            z = damp[i].tick(z, dampC);
            // Per-line RT60 gain from the unmodulated length; recomputed only when it moves.
            if (newRt || std::abs(len[i] - gainLen[i]) > .5f) { gainLen[i] = len[i]; gain[i] = float(std::pow(10.0, -3.0 * double(len[i]) / (double(rt60) * sr))); }
            v[i] = M::node(z * (p.freeze ? 1.f : gain[i]));
            sum += v[i];
        }
        primed = true;
        const float h = M::node(sum * (2.f / N));
        float out[2] = {0, 0};
        static constexpr int signL[N] = {1, 1, -1, 1, 1, -1, 1, -1}, signR[N] = {1, -1, 1, 1, -1, 1, -1, -1};
        for (int i = 0; i < N; i++) {
            float y = M::node(v[i] - h);
            float inject = (i < N / 2 ? x[0] : x[1]) * .5f;
            line[i].write(M::store(M::node(y + inject)));
            out[0] += signL[i] * v[i];
            out[1] += signR[i] * v[i];
        }
        return {{M::node(out[0] * .35f), M::node(out[1] * .35f)}};
    }
};

struct Params {
    EchoParams echo; SpaceParams space;
    float echoMix = .5f, spaceMix = .35f, mix = .5f;
    bool esp = false;     // false: CLEAN, true: ESP CHARACTER
    int espRate = 0;      // 0: 29.76 kHz, 1: 44.64 kHz, 2: 78.125 kHz
    bool espBudget = true;  // limit echo memory to the period-plausible budget
};

// OTIS's chip rates: 10 MHz / (16 * oscillators) for 21, 14 and 8 oscillators.
constexpr double espRates[3] = {10000000.0 / 336.0, 10000000.0 / 224.0, 10000000.0 / 128.0};
constexpr size_t ESP_BUDGET_WORDS = 65536;  // shared by both echo lines in budget mode

// Retroactive capture: the audio thread writes the post-mix mono signal into one
// of two rings. CAPTURE freezes the current ring and switches to the other; the
// UI thread copies the frozen one and releases it. No allocation or waiting on
// the audio thread.
struct Capture {
    std::vector<int16_t> ring[2];
    size_t size = 0, writePos = 0, filled = 0;
    int writing = 0;
    std::atomic<int> state{0};  // 0 idle, 1 frozen and waiting for the UI
    int frozen = 0; size_t frozenEnd = 0, frozenLen = 0;
    void prepare(size_t frames) { ring[0].assign(frames, 0); ring[1].assign(frames, 0); size = frames; writePos = filled = 0; writing = 0; state = 0; }
    void write(float x) {
        if (!size) return;
        ring[writing][writePos] = int16_t(std::lround(clampf(x, -1, 32767.f / 32768) * 32768));
        writePos = (writePos + 1) % size;
        filled = std::min(size, filled + 1);
    }
    // Audio thread. Returns false when the previous capture has not been collected yet.
    bool trigger(size_t lengthFrames) {
        if (!size || state.load(std::memory_order_acquire) != 0 || filled < 2) return false;
        frozen = writing; frozenEnd = writePos; frozenLen = std::min(filled, std::max<size_t>(2, lengthFrames));
        writing ^= 1; writePos = 0; filled = 0;
        state.store(1, std::memory_order_release);
        return true;
    }
    // UI thread: copy the frozen capture (oldest first) and release the ring.
    bool collect(std::vector<int16_t>& out) {
        if (state.load(std::memory_order_acquire) != 1) return false;
        out.resize(frozenLen);
        for (size_t i = 0; i < frozenLen; i++) out[i] = ring[frozen][(frozenEnd + size - frozenLen + i) % size];
        state.store(0, std::memory_order_release);
        return true;
    }
};

struct Engine {
    Echo<CleanMath> echoClean; Space<CleanMath> spaceClean;
    Echo<EspMath> echoEsp; Space<EspMath> spaceEsp;
    RateConverter down[2], up[2];
    double hostRate = 48000, phase = 0;  // ESP-rate clock, in ESP samples, kept in [0,1)
    int preparedRate = -1; bool wasEsp = false; size_t echoMaxClean = 0;
    Capture capture;
    float limiter = 0;
    // 5 Hz DC blockers on the wet signal. ESP memory writes round toward -infinity,
    // which leaves reverb tails on a -1 LSB (about -90 dBFS) DC limit cycle; the
    // hardware would pass that on, but there is no reason to send it downstream.
    OnePole dcTrack[2]; float dcCoef = 0;
    void prepare(double rate, int espRate = 0, double captureSeconds = 20) {
        hostRate = rate; dcCoef = onePoleCoef(5, float(rate));
        echoMaxClean = size_t(2.0 * rate) + 8;
        echoClean.prepare(float(rate), echoMaxClean);
        spaceClean.prepare(float(rate));
        prepareEsp(espRate);
        capture.prepare(size_t(captureSeconds * rate));
    }
    void prepareEsp(int espRate) {
        preparedRate = std::max(0, std::min(2, espRate));
        double r = espRates[preparedRate];
        echoEsp.prepare(float(r), size_t(2.0 * r) + 8);
        spaceEsp.prepare(float(r));
        for (int c = 0; c < 2; c++) { down[c].configure(hostRate, r); up[c].configure(r, hostRate); }
        phase = 0;
    }
    void clear() { echoClean.clear(); spaceClean.clear(); echoEsp.clear(); spaceEsp.clear(); for (int c = 0; c < 2; c++) { down[c].reset(); up[c].reset(); dcTrack[c].z = 0; } phase = 0; }
    double espRate() const { return espRates[preparedRate]; }
    // Echo memory limit in seconds for the current settings.
    double echoLimitSeconds(const Params& p) const {
        if (!p.esp) return 2.0;
        double r = espRate();
        return p.espBudget ? std::min(2.0, double(ESP_BUDGET_WORDS / 2) / r) : 2.0;
    }

    template <class E, class S>
    static std::array<float, 2> chain(E& echo, S& space, float l, float r, const Params& p, float limitSec) {
        EchoParams ep = p.echo; ep.timeSec = std::min(ep.timeSec, limitSec);
        auto e = echo.tick(l, r, ep);
        float a = lerp(l, e[0], p.echoMix), b = lerp(r, e[1], p.echoMix);
        auto s = space.tick(a, b, p.space);
        return {{lerp(a, s[0], p.spaceMix), lerp(b, s[1], p.spaceMix)}};
    }

    // One host-rate frame. Changing ESP rate (prepareEsp) is not real-time safe;
    // the module does that off the audio path.
    std::array<float, 2> tick(float inL, float inR, const Params& p) {
        if (!std::isfinite(inL)) inL = 0;
        if (!std::isfinite(inR)) inR = 0;
        if (p.esp != wasEsp) { clear(); wasEsp = p.esp; }
        std::array<float, 2> wet;
        float limitSec = float(echoLimitSeconds(p));
        if (!p.esp) {
            wet = chain(echoClean, spaceClean, inL, inR, p, limitSec);
            limiter = echoClean.limiterActivity;
        } else {
            // Host -> ESP rate, process, ESP rate -> host.
            // Each ESP tick that falls inside this host period reads the input converter
            // at its position in the period; there may be zero, one or several.
            down[0].push(inL); down[1].push(inR);
            const double step = espRate() / hostRate, a0 = phase, a1 = phase + step;
            for (double k = std::floor(a0) + 1; k <= a1; k += 1) {
                double f = std::min(.999999, (k - a0) / step);
                auto y = chain(echoEsp, spaceEsp, down[0].read(f), down[1].read(f), p, limitSec);
                up[0].push(y[0]); up[1].push(y[1]);
            }
            phase = a1 - std::floor(a1);  // time since the latest ESP tick, in ESP samples
            wet = {{up[0].read(phase), up[1].read(phase)}};
            limiter = echoEsp.limiterActivity;
        }
        for (int c = 0; c < 2; c++) wet[c] -= dcTrack[c].tick(wet[c], dcCoef);
        std::array<float, 2> out = {{lerp(inL, wet[0], p.mix), lerp(inR, wet[1], p.mix)}};
        for (auto& o : out) if (!std::isfinite(o)) { clear(); out = {{0, 0}}; break; }
        capture.write(.5f * (out[0] + out[1]));
        return out;
    }
};

} // namespace espish
