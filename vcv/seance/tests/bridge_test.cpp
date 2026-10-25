// Portable properties of the spectral/byte bridge, including measured FFT levels.
#include "../src/engine/Bridge.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

static size_t allocations = 0;
void* operator new(size_t n) { ++allocations; if (auto* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
using namespace seance;

static void allMasks() {
    FrameV1 f; f.size = 512; f.bins = 257; f.hop = 128; f.sampleRate = 64000;
    SpectralBridge b; BridgeParams p; p.slewMs = 0; p.thresholdDb = -72;
    for (int word = 0; word < 256; ++word) {
        for (int k = 0; k < f.bins; ++k) { f.mag[k] = 1; f.phase[k] = k * .01f; f.magB[k] = 2; }
        assert(b.process(f, uint8_t(word), p) == 255);  // detection precedes every possible mask
        for (int k = 0; k < f.bins; ++k) {
            assert(f.mag[k] == (word & (1 << SpectralBridge::bandAt(k * 125.f)) ? 1.f : 0.f));
            assert(f.phase[k] == k * .01f && f.magB[k] == 2);
        }
    }
    for (int edge = 0; edge < 7; ++edge) {
        assert(SpectralBridge::bandAt(std::nextafter(SpectralBridge::EDGES[edge], 0.f)) == edge);
        assert(SpectralBridge::bandAt(SpectralBridge::EDGES[edge]) == edge + 1);
    }
}
static void detector() {
    FrameV1 f; f.size = 512; f.bins = 257; f.hop = 128; f.sampleRate = 64000;
    SpectralBridge b; BridgeParams p; p.slewMs = 0; p.thresholdDb = -20; p.hysteresisDb = 6;
    auto level = [&](double rms) {
        std::fill(f.mag, f.mag + f.bins, 0.f);
        f.mag[8] = float(rms * std::sqrt(512. * 512. * .375 / 2));  // 1 kHz -> bit 4
        return b.process(f, 0, p);
    };
    assert(level(.11) == 16); assert(level(.075) == 16); assert(level(.049) == 0);
    assert(level(.075) == 0); assert(level(.11) == 16); assert(level(0) == 0);
    p.hysteresisDb = 0; assert(level(.11) == 16); assert(level(.099) == 0);
    std::fill(f.mag, f.mag + f.bins, std::numeric_limits<float>::quiet_NaN());
    f.mag[1] = std::numeric_limits<float>::infinity(); f.mag[2] = -1;
    assert(b.process(f, 0, p) == 0);
    for (int k = 0; k < f.bins; ++k) assert(f.mag[k] == 0);  // closed bands stay zero, even for invalid bins
    // Nyquist does not acquire a second copy; empty high bands remain low.
    f.sampleRate = 8000; std::fill(f.mag, f.mag + f.bins, 0.f);
    f.mag[f.bins - 1] = float(.2 * std::sqrt(512. * 512. * .375));
    assert(b.process(f, 255, p) == 64 && std::abs(b.power[6] - .04) < 1e-8);
}
static void measuredRms() {
    for (float sr : {44100.f, 48000.f, 96000.f}) for (int size = 0; size < SIZES; ++size) {
        Core core; core.prepare(sr); core.setSize(size); CoreParams cp;
        SpectralBridge b; BridgeParams p; p.slewMs = 0;
        int n = sizeOf(size);
        for (int kind = 0; kind < 3; ++kind) {
            for (int t = 0; t < 5 * n; ++t) {
                float x = kind == 0 ? .5f * float(std::sin(2 * PI * 17 * t / n)) : kind == 1 ? .25f : (t & 1) ? -.25f : .25f;
                core.tick(x, 0, cp, true);
                serve(core.frame, 1, true, false, [&](FrameV1& f) { b.process(f, 255, p); });
            }
            double total = 0; for (double power : b.power) total += power;
            assert(std::abs(total - (kind == 0 ? .125 : .0625)) < 1e-6);
        }
    }
}
static void slewAndLifecycle() {
    FrameV1 f; f.size = 2048; f.bins = 1025; f.hop = 512; f.sampleRate = 48000;
    SpectralBridge b; BridgeParams p;
    for (int t = 1; t <= 10; ++t) {
        std::fill(f.mag, f.mag + f.bins, 1.f); b.process(f, 0, p);
        for (float g : b.gain) assert(std::abs(g - std::exp(-t * 512. / (48000 * .020))) < 1e-7);
    }
    p.slewMs = 0; b.process(f, 255, p); for (float g : b.gain) assert(g == 1);
    b.process(f, 0, p); for (float g : b.gain) assert(g == 0);
    p.slewMs = 20; f.size = 1024; f.bins = 513; f.hop = 256;
    b.process(f, 255, p); for (float g : b.gain) assert(g == 1);  // size changes start open
    f.sampleRate = 96000; b.process(f, 0, p);
    for (float g : b.gain) assert(std::abs(g - std::exp(-256. / (96000 * .020))) < 1e-7);
    p.slewMs = NAN; p.thresholdDb = NAN; p.hysteresisDb = INFINITY;
    b.process(f, 255, p); for (float g : b.gain) assert(std::isfinite(g));
    size_t before = allocations;
    for (int t = 0; t < 10000; ++t) b.process(f, uint8_t(t), p);
    assert(allocations == before);
}
int main() {
    allMasks(); detector(); measuredRms(); slewAndLifecycle();
    std::cout << "PASS: all 256 masks, band boundaries, pre-mask activity, hysteresis, invalid bins, DC/Nyquist and measured RMS at all FFT sizes/three rates, exponential slew, lifecycle and allocation-free processing\n";
}
