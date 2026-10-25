// SPDX-License-Identifier: BSD-3-Clause
// Séance DSP: one short-time Fourier frame held in common by a row of adjacent modules.
// The core analyses a frame, hands it down the chain, each expander changes it in place,
// and the core resynthesises whatever comes back. Portable C++17, no Rack headers.
// Signals are in "units" where 1.0 is full scale (Rack ±10 V maps to ±1).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace seance {

constexpr double PI = 3.14159265358979323846;
constexpr int MAX_SIZE = 4096, MAX_BINS = MAX_SIZE / 2 + 1;
constexpr int SIZES = 4;  // 512, 1024, 2048, 4096
constexpr int sizeOf(int index) { return 512 << (index < 0 ? 0 : index > SIZES - 1 ? SIZES - 1 : index); }
// A frame travels the chain one module per engine step at worst. The core waits this many
// samples for it to come back before resynthesising what it has and lighting STALL.
constexpr int SLACK = 64;
constexpr int RING = 8192;  // > MAX_SIZE + SLACK, power of two
constexpr int RING_MASK = RING - 1;

constexpr uint32_t FRAME_MAGIC = 0x53454146u;  // "SEAF"
constexpr uint32_t FRAME_VERSION = 1;
constexpr int STAGE_IDLE = 0, STAGE_DONE = -1;

inline float clampf(float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); }
// Wrap a phase to (-pi, pi].
inline float princarg(float x) { return float(x - 2 * PI * std::round(x / (2 * PI))); }

// Deterministic hashing (splitmix64 finaliser), so every random decision is a pure
// function of (seed, frame, bin): the same seed replays the same texture.
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline uint64_t key(uint64_t a, uint64_t b, uint64_t c = 0) { return mix64(mix64(mix64(a) ^ b) ^ c); }
inline float uniform(uint64_t k) { return float((k >> 40) * (1.0 / 16777216.0)); }  // [0, 1)
inline float bipolar(uint64_t k) { return 2 * uniform(k) - 1; }                    // [-1, 1)

// The frame that passes along the chain. Plain fields are written by the core before it
// publishes the frame and read by expanders after they acquire it; `stage` is the token.
struct FrameV1 {
    uint32_t magic = FRAME_MAGIC, version = FRAME_VERSION;
    // Who holds the frame: STAGE_IDLE (the core, between frames), k > 0 (the k-th expander
    // from the core), STAGE_DONE (the chain has finished; the core may resynthesise).
    std::atomic<int> stage{STAGE_IDLE};
    std::atomic<int> length{0};  // expanders in the chain, reported by the last one
    uint64_t serial = 0;         // frames since RESET; epochs of seeded randomness count from it
    int size = 2048, bins = 1025, hop = 512;
    float sampleRate = 48000;
    uint32_t seed = 0;
    float mag[MAX_BINS], phase[MAX_BINS];  // A, changed in place down the chain
    float magB[MAX_BINS];                   // B, a read-only reference
};
inline bool validFrame(const void* p) {
    auto* f = static_cast<const FrameV1*>(p);
    return f && f->magic == FRAME_MAGIC && f->version == FRAME_VERSION;
}

// In-place radix-2 complex FFT with twiddles tabulated once for MAX_SIZE.
struct Fft {
    std::vector<float> cosT, sinT;
    std::vector<uint16_t> rev;
    int size = 0;
    Fft() {
        cosT.resize(MAX_SIZE / 2); sinT.resize(MAX_SIZE / 2);
        for (int i = 0; i < MAX_SIZE / 2; i++) { cosT[i] = float(std::cos(2 * PI * i / MAX_SIZE)); sinT[i] = float(std::sin(2 * PI * i / MAX_SIZE)); }
    }
    void setSize(int n) {
        size = n; rev.assign(n, 0);
        int bits = 0; while ((1 << bits) < n) bits++;
        for (int i = 0; i < n; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[i] = uint16_t(r); }
    }
    // Forward uses e^{-i}; inverse uses e^{+i} and scales by 1/n, so the pair is an identity.
    void transform(float* re, float* im, bool inverse) const {
        int n = size;
        for (int i = 0; i < n; i++) if (i < rev[i]) { std::swap(re[i], re[rev[i]]); std::swap(im[i], im[rev[i]]); }
        for (int len = 2; len <= n; len <<= 1) {
            int half = len / 2, step = MAX_SIZE / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; j++) {
                    int t = j * step, a = i + j, b = a + half;
                    float wr = cosT[t], wi = inverse ? sinT[t] : -sinT[t];
                    float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                }
        }
        if (inverse) { float s = 1.f / n; for (int i = 0; i < n; i++) { re[i] *= s; im[i] *= s; } }
    }
};

struct CoreParams {
    int sizeIndex = 2;  // 2048
    uint32_t seed = 0;
    float mix = 1;      // 0 dry .. 1 wet
};

// Analysis, the hand-off, and overlap-add resynthesis. Hann windows on both sides at a
// quarter-frame hop; the output is divided by the windows actually summed at each sample,
// so a frame grid restarted by RESET or a frame returned late still reconstructs.
struct Core {
    Fft fft;
    FrameV1 frame;
    float sampleRate = 48000;
    int sizeIndex = 2, size = 2048, hop = 512, bins = 1025, pendingSizeIndex = -1;
    float nominalSum = 1.5f;
    std::vector<float> window, inA, inB, dry, out, wsum, re, im;
    long long t = 0, frameTime = 0; uint64_t serial = 0;
    int hopCounter = 0;
    bool pending = false;  // a frame is out in the chain
    bool stalled = false;  // the last frame came back late (or not at all)
    int stalls = 0;

    Core() {
        inA.assign(RING, 0); inB.assign(RING, 0); dry.assign(RING, 0); out.assign(RING, 0); wsum.assign(RING, 0);
        re.assign(MAX_SIZE, 0); im.assign(MAX_SIZE, 0);
        applySize(); prepare(48000);
    }
    int latency() const { return size - 1 + SLACK; }
    void prepare(float sr) { sampleRate = sr; frame.sampleRate = sr; clear(); }
    void clear() {
        std::fill(inA.begin(), inA.end(), 0.f); std::fill(inB.begin(), inB.end(), 0.f); std::fill(dry.begin(), dry.end(), 0.f);
        std::fill(out.begin(), out.end(), 0.f); std::fill(wsum.begin(), wsum.end(), 0.f);
        t = 0; hopCounter = 0; pending = false; stalled = false; serial = 0;
        frame.stage.store(STAGE_IDLE, std::memory_order_release);
    }
    // Takes effect at the next frame boundary, once no frame is out.
    void setSize(int index) { index = std::max(0, std::min(SIZES - 1, index)); if (index != sizeIndex) pendingSizeIndex = index; }
    // RESET: frames count from zero again and the frame grid restarts here, so a sequence
    // that resets the core replays the same frames at the same offsets.
    void reset() { serial = 0; hopCounter = 0; }

    void applySize() {
        size = sizeOf(sizeIndex); hop = size / 4; bins = size / 2 + 1;
        window.resize(size);
        for (int n = 0; n < size; n++) window[n] = float(.5 - .5 * std::cos(2 * PI * n / size));
        nominalSum = 0; for (int n = 0; n < size; n += hop) nominalSum += window[n] * window[n];
        fft.setSize(size);
        frame.size = size; frame.bins = bins; frame.hop = hop;
    }

    // Windowed FFT of the last `size` samples of A and B. A rides the real part and B the
    // imaginary part of one transform; the two spectra separate by Hermitian symmetry.
    void analyze() {
        int n = size;
        size_t start = size_t(t - n + 1);
        for (int i = 0; i < n; i++) { size_t r = (start + i) & RING_MASK; re[i] = inA[r] * window[i]; im[i] = inB[r] * window[i]; }
        fft.transform(re.data(), im.data(), false);
        for (int k = 0; k < bins; k++) {
            int j = (n - k) & (n - 1);
            float xr = re[k], xi = im[k], yr = re[j], yi = -im[j];  // X[k] and conj(X[n-k])
            float ar = .5f * (xr + yr), ai = .5f * (xi + yi);
            float br = .5f * (xi - yi), bi = -.5f * (xr - yr);
            frame.mag[k] = std::sqrt(ar * ar + ai * ai); frame.phase[k] = std::atan2(ai, ar);
            frame.magB[k] = std::sqrt(br * br + bi * bi);
        }
    }
    // Inverse transform of the (possibly changed) frame, overlap-added where its samples
    // fall in the output, `latency()` after the input they came from.
    void synthesize() {
        int n = size;
        for (int k = 0; k < bins; k++) { float m = frame.mag[k], ph = frame.phase[k]; re[k] = m * std::cos(ph); im[k] = m * std::sin(ph); }
        im[0] = 0; im[bins - 1] = 0;
        for (int k = 1; k < bins - 1; k++) { re[n - k] = re[k]; im[n - k] = -im[k]; }
        fft.transform(re.data(), im.data(), true);
        size_t base = size_t(frameTime - n + 1 + latency());
        for (int i = 0; i < n; i++) { size_t r = (base + i) & RING_MASK; float w = window[i]; out[r] += re[i] * w; wsum[r] += w * w; }
    }

    // One sample. `chain` says whether an expander sits to the right, in which case the
    // frame is handed over and collected when its stage reads STAGE_DONE.
    float tick(float a, float b, const CoreParams& p, bool chain) {
        size_t w = size_t(t) & RING_MASK;
        inA[w] = a; inB[w] = b; dry[w] = a;
        if (pending) {
            int st = frame.stage.load(std::memory_order_acquire);
            if (st == STAGE_DONE || t >= frameTime + SLACK) {
                if (st != STAGE_DONE) { stalled = true; stalls++; frame.stage.exchange(STAGE_IDLE, std::memory_order_acq_rel); }
                else { stalled = false; frame.stage.store(STAGE_IDLE, std::memory_order_relaxed); }
                synthesize(); pending = false;
            }
        }
        if (++hopCounter >= hop && !pending) {
            hopCounter = 0;
            if (pendingSizeIndex >= 0) { sizeIndex = pendingSizeIndex; pendingSizeIndex = -1; applySize(); }
            frame.seed = p.seed; frame.serial = serial++;
            analyze();
            frameTime = t;
            if (chain) { pending = true; frame.stage.store(1, std::memory_order_release); }
            else { stalled = false; frame.length.store(0, std::memory_order_relaxed); synthesize(); }
        }
        size_t r = size_t(t) & RING_MASK;
        float wet = out[r] / std::max(wsum[r], .5f * nominalSum);
        out[r] = 0; wsum[r] = 0;
        float d = dry[size_t(t - latency()) & RING_MASK];
        t++;
        return p.mix * wet + (1 - p.mix) * d;
    }
};

// The expander side of the hand-off. If the frame is at `position` (counted from the core,
// starting at 1), apply `op` unless bypassed and pass the frame on: to the next position,
// or back to the core when this is the last expander. Returns true if the frame was here.
template <class Op>
inline bool serve(FrameV1& f, int position, bool last, bool bypassed, Op&& op) {
    int expected = position;
    if (position < 1 || f.stage.load(std::memory_order_acquire) != position) return false;
    if (!bypassed) op(f);
    if (last) f.length.store(position, std::memory_order_relaxed);
    f.stage.compare_exchange_strong(expected, last ? STAGE_DONE : position + 1, std::memory_order_acq_rel);
    return true;
}

// VI Regulation (returns to set point): holds the frame it was given and lets each bin's
// phase keep turning at the rate it was measured at, so the held sound stays in motion.
struct Freeze {
    float heldMag[MAX_BINS], heldPhase[MAX_BINS], advance[MAX_BINS], prevPhase[MAX_BINS];
    bool frozen = false, havePrev = false;
    int prevSize = 0;
    // `drift` (0..1) adds seeded per-bin phase noise to the held frame, up to half a radian a hop.
    void process(FrameV1& f, bool freeze, float drift) {
        int bins = f.bins, n = f.size;
        if (n != prevSize) { havePrev = false; frozen = false; prevSize = n; }
        bool onset = false;
        if (!freeze) frozen = false;
        else if (!frozen) {
            // Phase-vocoder frequency estimate: the expected rotation per hop plus the measured
            // deviation from it, taken from the previous frame when there is one.
            for (int k = 0; k < bins; k++) {
                float expected = float(2 * PI * k * f.hop / n);
                float dev = havePrev ? princarg(f.phase[k] - prevPhase[k] - expected) : 0.f;
                advance[k] = expected + dev; heldMag[k] = f.mag[k]; heldPhase[k] = f.phase[k];
            }
            frozen = true; onset = true;
        }
        std::memcpy(prevPhase, f.phase, sizeof(float) * bins); havePrev = true;
        if (!frozen) return;
        if (!onset)
            for (int k = 0; k < bins; k++)
                heldPhase[k] = princarg(heldPhase[k] + advance[k] + drift * .5f * bipolar(key(f.seed, f.serial, uint64_t(k) + 1)));
        std::memcpy(f.mag, heldMag, sizeof(float) * bins);
        std::memcpy(f.phase, heldPhase, sizeof(float) * bins);
    }
};

// XII Learned invariant (hard walls, one door): every spectral peak is moved, with the
// bins of its lobe, by an offset that depends only on the seed, the epoch and the peak's
// bin, so one seed always opens onto the same scatter. Peaks are re-pitched coherently
// (a phase-locked vocoder: the lobe keeps its internal phase structure and advances at
// the new frequency), so partials land where they are put instead of cancelling between
// overlapping frames. `range` (0..1) is the largest offset in octaves, `hold` the frames
// per epoch, and `phaseDepth` (0..1 of ±pi) adds a constant random phase per bin, a
// diffuser that smears transients and can lower the level by up to about 3 dB, since
// energy spread past the window is lost.
struct Scramble {
    float offset[MAX_BINS], tmpM[MAX_BINS], tmpP[MAX_BINS], prevPhase[MAX_BINS], acc[MAX_BINS], re[MAX_BINS], im[MAX_BINS];
    int16_t disp[MAX_BINS], newDisp[MAX_BINS];  // offset of the peak at each bin, last frame and this one, or NO_PEAK
    static constexpr int16_t NO_PEAK = INT16_MIN;
    uint64_t cached = 0; bool valid = false, havePrev = false; int prevSize = 0;

    void process(FrameV1& f, float range, float phaseDepth, int hold) {
        int bins = f.bins, n = f.size; hold = std::max(1, hold); range = clampf(range, 0, 1);
        if (n != prevSize) { havePrev = false; prevSize = n; }
        uint64_t k = key(f.seed, f.serial / uint64_t(hold), uint64_t(bins));
        if (!valid || k != cached) {
            for (int i = 0; i < bins; i++) offset[i] = float(PI) * bipolar(key(k, uint64_t(i), 7));
            cached = k; valid = true; havePrev = false;  // a new epoch starts every peak afresh
        }
        if (!havePrev) for (int i = 0; i < bins; i++) disp[i] = NO_PEAK;
        for (int i = 0; i < bins; i++) newDisp[i] = NO_PEAK;
        std::memcpy(tmpM, f.mag, sizeof(float) * bins); std::memcpy(tmpP, f.phase, sizeof(float) * bins);
        std::memset(re, 0, sizeof(float) * bins); std::memset(im, 0, sizeof(float) * bins);
        // Walk the peaks; each one's region runs from the valley before it to the valley after.
        int start = 0, prevPeak = -1;
        for (int p = 0; p <= bins; p++) {
            bool peak = p < bins && (p == 0 || tmpM[p] > tmpM[p - 1]) && (p == bins - 1 || tmpM[p] >= tmpM[p + 1]);
            if (!peak && p < bins) continue;
            if (prevPeak >= 0) {
                int end = bins;
                if (p < bins) {  // the valley between the previous peak and this one
                    int v = prevPeak; for (int i = prevPeak + 1; i < p; i++) if (tmpM[i] < tmpM[v]) v = i;
                    end = v + 1;
                }
                place(f, prevPeak, start, end, k, range, phaseDepth);
                start = end;
            }
            prevPeak = p;
        }
        for (int i = 0; i < bins; i++) { f.mag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]); f.phase[i] = std::atan2(im[i], re[i]); }
        std::memcpy(prevPhase, tmpP, sizeof(float) * bins); std::memcpy(disp, newDisp, sizeof(int16_t) * bins); havePrev = true;
    }
    // Move the lobe [start, end) of the peak at `p` by its seeded offset, phase-locked to the peak.
    void place(FrameV1& f, int p, int start, int end, uint64_t k, float range, float phaseDepth) {
        int bins = f.bins, n = f.size;
        float ratio = std::exp2(range * bipolar(key(k, uint64_t(p), 11)));
        int d = int(std::lround(p * (ratio - 1)));
        float phase;
        if (havePrev && disp[p] == d) {
            // Coherent continuation: the peak's measured advance, re-centred on the target bin.
            float dev = princarg(tmpP[p] - prevPhase[p] - float(2 * PI * p * f.hop / n));
            phase = princarg(acc[p] + float(2 * PI * (p + d) * f.hop / n) + dev);
        }
        else phase = tmpP[p];
        acc[p] = phase; newDisp[p] = int16_t(d);
        for (int i = start; i < end; i++) {
            int j = i + d;
            if (j < 0 || j >= bins) continue;
            float ph = phase + (tmpP[i] - tmpP[p]) + phaseDepth * offset[j];
            re[j] += tmpM[i] * std::cos(ph); im[j] += tmpM[i] * std::sin(ph);
        }
    }
};

// IV Centre of decision (authority, contestable): B's spectrum decides A's levels, bin by
// bin, to a depth the knob contests. MULTIPLY scales A by B's normalised envelope raised
// to `depth`, then restores the overall level (up to 24 dB); MORPH walks A's magnitudes
// towards B's geometrically. `smooth` (0..1) widens B's envelope up to ±127 bins.
struct Cross {
    float env[MAX_BINS];
    double prefix[MAX_BINS + 1];
    void process(FrameV1& f, float depth, float smooth, bool morph) {
        int bins = f.bins; depth = clampf(depth, 0, 1);
        int s = int(std::pow(128.f, clampf(smooth, 0, 1)) + .5f) - 1;
        if (s <= 0) std::memcpy(env, f.magB, sizeof(float) * bins);
        else {
            prefix[0] = 0; for (int k = 0; k < bins; k++) prefix[k + 1] = prefix[k] + f.magB[k];
            for (int k = 0; k < bins; k++) { int lo = std::max(0, k - s), hi = std::min(bins, k + s + 1); env[k] = float((prefix[hi] - prefix[lo]) / (hi - lo)); }
        }
        if (morph) {
            for (int k = 0; k < bins; k++) f.mag[k] = std::exp((1 - depth) * std::log(f.mag[k] + 1e-12f) + depth * std::log(env[k] + 1e-12f));
            return;
        }
        float peak = 0; for (int k = 0; k < bins; k++) peak = std::max(peak, env[k]);
        if (peak <= 0) { if (depth > 0) std::memset(f.mag, 0, sizeof(float) * bins); return; }
        double before = 0, after = 0;
        for (int k = 0; k < bins; k++) { before += double(f.mag[k]) * f.mag[k]; f.mag[k] *= std::pow(env[k] / peak, depth); after += double(f.mag[k]) * f.mag[k]; }
        float gain = after > 0 ? float(std::min(16.0, std::sqrt(before / after))) : 0.f;
        for (int k = 0; k < bins; k++) f.mag[k] *= gain;
    }
};

// V Bottleneck (fails the bypass test): a wall across the spectrum on a log-frequency axis.
// `w` > 0 clears everything below an edge that climbs with w (a high-pass sweeping up);
// `w` < 0 clears everything above an edge that descends as w falls (a low-pass sweeping
// down); 0 is a clean pass. `soft` (0..1) widens the edge up to two octaves.
inline void wipe(FrameV1& f, float w, float soft) {
    w = clampf(w, -1, 1);
    if (w == 0) return;
    int bins = f.bins;
    float p = w > 0 ? w : 1 + w, top = std::log2(float(bins - 1)), edge = p * top, width = 2 * clampf(soft, 0, 1);
    for (int k = 0; k < bins; k++) {
        float x = (k == 0 ? -1e9f : std::log2(float(k))) - edge, g;
        if (width <= 0) g = x >= 0 ? 1.f : 0.f;
        else { float u = clampf(x / width + .5f, 0, 1); g = u * u * (3 - 2 * u); }
        if (w < 0) g = 1 - g;
        f.mag[k] *= g;
    }
}

} // namespace seance
