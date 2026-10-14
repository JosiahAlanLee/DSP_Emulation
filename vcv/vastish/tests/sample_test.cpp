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
        for (size_t i = 0; i + 1 < pcm.size(); i++) assert(v.sampleTick(&view, root, 48000) == pcm[i] / 32768.f);
        for (int i = 0; i < 100; i++) assert(v.sampleTick(&view, root, 48000) == 0);
        assert(!v.samplePlaying);
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
    std::cout << "PASS: sample playback exact at root, transposition, resampling, loop, gate restart, silence without a sample, other waves unaffected, WAV import, patch round-trip\n";
}
