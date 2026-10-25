// SAMPLE pitch wave and the shared WAV/patch serialization helpers.
#include "../src/engine/Engine.hpp"
#include "../../shared/SampleFile.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
using namespace vastish;

static void writeStereo16(const char* path, const std::vector<int16_t>& l, const std::vector<int16_t>& r, uint32_t rate) {
    FILE* f = std::fopen(path, "wb"); assert(f);
    auto put16 = [&](uint16_t n) { std::fputc(n & 255, f); std::fputc(n >> 8, f); };
    auto put32 = [&](uint32_t n) { put16(uint16_t(n)); put16(uint16_t(n >> 16)); };
    uint32_t bytes = uint32_t(l.size() * 4);
    std::fwrite("RIFF", 1, 4, f); put32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    put32(16); put16(1); put16(2); put32(rate); put32(rate * 4); put16(4); put16(16);
    std::fwrite("data", 1, 4, f); put32(bytes);
    for (size_t i = 0; i < l.size(); i++) { put16(uint16_t(l[i])); put16(uint16_t(r[i])); }
    std::fclose(f);
}

int main() {
    std::vector<int16_t> pcm(4800);
    for (size_t i = 0; i < pcm.size(); i++) pcm[i] = int16_t(20000 * std::sin(i * 0.05) + (i % 7) * 300);
    SampleView view{pcm.data(), pcm.size(), 48000, false};

    // Root note at matching rates reproduces the sample exactly, then stops (one-shot).
    {
        Voice v; v.samplePlaying = true; v.samplePos = 0;
        float root = mtof(SAMPLE_ROOT_NOTE);
        for (size_t i = 0; i < pcm.size(); i++) assert(v.sampleTick(&view, root, 48000) == pcm[i] / 32768.f); // every frame, including the last
        for (int i = 0; i < 100; i++) assert(v.sampleTick(&view, root, 48000) == 0);
        assert(!v.samplePlaying);
    }
    // Two frames: one-shot plays both, a loop alternates them, nothing is dropped.
    {
        int16_t two[] = {0, 32767}; SampleView tv{two, 2, 48000, false};
        float root = mtof(SAMPLE_ROOT_NOTE);
        Voice v; v.samplePlaying = true;
        assert(v.sampleTick(&tv, root, 48000) == 0 && v.sampleTick(&tv, root, 48000) == 32767 / 32768.f);
        assert(v.sampleTick(&tv, root, 48000) == 0 && !v.samplePlaying);
        tv.loop = true; Voice w; w.samplePlaying = true;
        for (int i = 0; i < 10; i++) assert(w.sampleTick(&tv, root, 48000) == two[i % 2] / 32768.f);
    }
    // The loop period is exactly `frames` at the root pitch (not frames-1, ~6.8 cents sharp for 256).
    {
        std::vector<int16_t> cyc(256); for (int i = 0; i < 256; i++) cyc[i] = int16_t(30000 * std::sin(2 * M_PI * i / 256));
        SampleView lv{cyc.data(), cyc.size(), 48000, true};
        Voice v; v.samplePlaying = true; std::vector<float> out(256 * 4);
        for (auto& y : out) y = v.sampleTick(&lv, mtof(SAMPLE_ROOT_NOTE), 48000);
        for (size_t i = 0; i + 256 < out.size(); i++) assert(out[i] == out[i + 256]);
        // Fractional reads across the seam interpolate through the wrap, not a clamped edge.
        Voice h; h.samplePlaying = true; h.samplePos = 255.5;
        float y = h.sampleTick(&lv, mtof(SAMPLE_ROOT_NOTE), 48000);
        assert(std::abs(y - 30000.f / 32768 * float(std::sin(2 * M_PI * 255.5 / 256))) < 2e-3f);
    }
    // An octave up plays through in half the time; looping keeps going.
    {
        Voice v; v.samplePlaying = true;
        size_t count = 0;
        while (v.samplePlaying && count < pcm.size()) { v.sampleTick(&view, mtof(SAMPLE_ROOT_NOTE + 12), 48000); count++; }
        assert(count >= pcm.size() / 2 - 2 && count <= pcm.size() / 2 + 2);
        SampleView looped = view; looped.loop = true;
        Voice w; w.samplePlaying = true; double energy = 0;
        for (size_t i = 0; i < pcm.size() * 3; i++) { float y = w.sampleTick(&looped, mtof(SAMPLE_ROOT_NOTE), 48000); energy += y * y; }
        assert(w.samplePlaying && energy > pcm.size() * 2 * 0.05);
    }
    // A different sample rate is resampled: a 24 kHz sample at the root note lasts twice as long.
    {
        SampleView slow = view; slow.rate = 24000;
        Voice v; v.samplePlaying = true; size_t count = 0;
        while (v.samplePlaying && count < pcm.size() * 3) { v.sampleTick(&slow, mtof(SAMPLE_ROOT_NOTE), 48000); count++; }
        assert(count >= pcm.size() * 2 - 4 && count <= pcm.size() * 2 + 2);
    }
    // Through the whole voice: a gate rise restarts the sample; no sample means silence.
    {
        auto p = programs()[0]; p[PITCH_WAVE] = SAMPLE_WAVE;
        Engine e; std::array<VoiceInput, 8> in{}; in[0].note = SAMPLE_ROOT_NOTE; in[0].gate = true;
        double energy = 0;
        for (int i = 0; i < 2400; i++) { auto y = e.tick(p, in, 1, &view); assert(std::isfinite(y[0])); energy += y[0] * y[0]; }
        assert(energy > 1e-3);
        in[0].gate = false; e.tick(p, in, 1, &view);
        in[0].gate = true; e.tick(p, in, 1, &view);
        assert(e.voices[0].samplePos <= 2.0 && e.voices[0].samplePlaying);
        Engine silent; std::array<VoiceInput, 8> on{}; on[0].gate = true; double s = 0;
        for (int i = 0; i < 2400; i++) { auto y = silent.tick(p, on, 1, nullptr); assert(std::isfinite(y[0])); s += std::abs(y[0]); }
        assert(s == 0);
        // Other waves ignore the sample entirely.
        auto saw = programs()[1]; Engine a, b; std::array<VoiceInput, 8> g{}; g[0].gate = true;
        for (int i = 0; i < 4800; i++) { auto x = a.tick(saw, g, 1, nullptr), y = b.tick(saw, g, 1, &view); assert(x[0] == y[0] && x[1] == y[1]); }
    }
    // Stereo samples (2.4.0): each side runs through its own copy of the blocks with the same controls,
    // so the left output equals a mono voice playing the left channel, and the right output one playing
    // the right channel, bit for bit, in every program, with several voices, transposed and looped.
    {
        std::vector<int16_t> L(6000), R(6000);
        for (size_t i = 0; i < L.size(); i++) { L[i] = int16_t(15000 * std::sin(i * .031) + (i % 11) * 200); R[i] = int16_t(12000 * std::sin(i * .007 + 1)); }
        for (int prog = 0; prog < 6; prog++) for (bool loop : {false, true}) {
            auto p = programs()[prog]; p[PITCH_WAVE] = SAMPLE_WAVE;
            SampleView st{L.data(), L.size(), 44100, loop, R.data()}, ml{L.data(), L.size(), 44100, loop}, mr{R.data(), R.size(), 44100, loop};
            Engine es, el, er; std::array<VoiceInput, 8> in{};
            for (int i = 0; i < 9600; i++) {
                for (int v = 0; v < 3; v++) { in[v].note = SAMPLE_ROOT_NOTE + v * 5 - 3; in[v].gate = (i / (1500 + 400 * v)) % 2 == 0; in[v].velocity = .5f + .2f * v; }
                auto ys = es.tick(p, in, 3, &st), yl = el.tick(p, in, 3, &ml), yr = er.tick(p, in, 3, &mr);
                assert(ys[0] == yl[0] && ys[1] == yr[1] && std::isfinite(ys[0]) && std::isfinite(ys[1]));
            }
            assert(!es.faults);
        }
        // Identical channels sound exactly like the mono sample, even when stereo starts mid-note.
        auto p = programs()[2]; p[PITCH_WAVE] = SAMPLE_WAVE;
        SampleView mono{L.data(), L.size(), 48000, true}, same{L.data(), L.size(), 48000, true, L.data()};
        Engine a, b; std::array<VoiceInput, 8> in{}; in[0].gate = true; in[0].note = 64;
        for (int i = 0; i < 9600; i++) {
            auto ya = a.tick(p, in, 1, &mono), yb = b.tick(p, in, 1, i < 3000 ? &mono : &same);
            assert(ya[0] == yb[0] && ya[1] == yb[1]);
        }
        assert(b.voices[0].stereoActive && !a.voices[0].stereoActive);
        // Back to mono: the right path stops.
        b.tick(p, in, 1, &mono); assert(!b.voices[0].stereoActive);
    }
    // WAV import keeps a two-channel file stereo on request.
    {
        std::vector<int16_t> l(900), r(900);
        for (int i = 0; i < 900; i++) { l[i] = int16_t(i * 31); r[i] = int16_t(-i * 17); }
        const char* path = "vastish_stereo_test.wav";
        writeStereo16(path, l, r, 32000);
        auto st = dspemu::loadWav(path, 1 << 20, true);
        assert(st.stereo() && st.pcm == l && st.right == r && st.rate == 32000);
        auto mono = dspemu::loadWav(path, 1 << 20, false);
        assert(!mono.stereo() && mono.pcm == dspemu::loadWavMono(path, 1 << 20).pcm);
        auto cut = dspemu::loadWav(path, 300, true); assert(cut.truncated && cut.pcm.size() == 300 && cut.right.size() == 300);
        std::remove(path);
    }
    // WAV import: stereo mixed to mono, rate kept, name from the path, truncation flagged.
    {
        std::vector<int16_t> l(1000), r(1000);
        for (int i = 0; i < 1000; i++) { l[i] = int16_t(i * 30); r[i] = int16_t(-i * 10); }
        const char* path = "vastish_sample_test.wav";
        writeStereo16(path, l, r, 22050);
        auto m = dspemu::loadWavMono(path, 1 << 20);
        assert(m.rate == 22050 && m.pcm.size() == 1000 && !m.truncated && m.name == "vastish_sample_test.wav");
        for (int i = 0; i < 1000; i++) assert(std::abs(m.pcm[i] - (l[i] + r[i]) / 2) <= 1);
        auto cut = dspemu::loadWavMono(path, 500);
        assert(cut.pcm.size() == 500 && cut.truncated);
        std::remove(path);
        bool threw = false;
        try { dspemu::loadWavMono("does-not-exist.wav", 100); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    // Patch serialization round-trips every value, including odd lengths and extremes.
    for (size_t n : {size_t(2), size_t(3), size_t(1001), pcm.size()}) {
        std::vector<int16_t> x(pcm.begin(), pcm.begin() + n); x[0] = -32768; x[n - 1] = 32767;
        auto text = dspemu::encodePcm16(x.data(), x.size());
        assert(dspemu::decodePcm16(text, 1 << 20) == x);
    }
    {
        bool threw = false;
        try { dspemu::decodePcm16("####", 10); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    std::cout << "PASS: sample playback exact at root (every frame), two-frame and loop-period exactness, seam interpolation, transposition, resampling, loop, gate restart, silence without a sample, other waves unaffected, stereo sides equal per-channel mono voices in all programs, identical channels equal mono mid-note, stereo WAV import, WAV import, patch round-trip\n";
}
