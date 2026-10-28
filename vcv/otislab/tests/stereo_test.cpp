// Stereo samples: each Rack voice is a linked pair of chip voices, left and right.
// The pair must equal two mono engines playing each channel, bit for bit, in every mode; a stereo take
// must equal two mono takes; capacity, polyphony, WAV files, rework and the mono path are checked too.
#include "../src/engine/OtisEngine.hpp"
#include "../src/engine/LoopMutator.hpp"
#include "../src/engine/Wav.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

using otis::Engine;
static uint32_t rng = 7;
static uint32_t rnd() { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static float unit() { return float(rnd() % 100000) / 100000.f; }
static std::vector<int16_t> noise(size_t n) { std::vector<int16_t> v(n); for (auto& x : v) x = int16_t(rnd() & 65535); return v; }
static bool same(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }

static void configure(otis::Controls& c, uint32_t seed) {
    rng = seed;
    c.engineMode = rnd() % 3; c.filterMode = rnd() % 4; c.loop = rnd() % 3; c.reverse = rnd() % 4 == 0;
    c.gated = rnd() % 2; c.declick = rnd() % 2; c.linearLevel = rnd() % 2; c.windowMode = rnd() % 3;
    const int bits[4] = {9, 9, 5, 0}; const float clocks[4] = {0, 0, -1.3f, .7f};
    c.interpBits = bits[rnd() % 4]; c.clockOctaves = clocks[rnd() % 4];
    c.varispeed = rnd() % 2; c.outputGain = .5f + unit();
    for (int i = 0; i < 16; ++i) {
        c.pitch[i] = (unit() - .5f) * 3; c.k1v[i] = unit(); c.k2v[i] = unit(); c.level[i] = unit();
        c.startv[i] = unit() * .3f; c.endv[i] = .6f + unit() * .4f; c.position[i] = unit(); c.sizeOctaves[i] = 2 + unit() * 8;
    }
}

// The pair equals two mono engines, one per channel, in random scenarios across all modes.
static void lockstep() {
    for (int scenario = 0; scenario < 60; ++scenario) {
        rng = 1000 + scenario;
        const double hosts[3] = {44100, 48000, 96000};
        size_t n = 600 + rnd() % 5000; double rate = 8000 + rnd() % 40000, host = hosts[rnd() % 3];
        auto L = noise(n), R = noise(n);
        auto st = std::make_unique<Engine>(), ml = std::make_unique<Engine>(), mr = std::make_unique<Engine>();
        for (auto* e : {st.get(), ml.get(), mr.get()}) e->setHostRate(host);
        auto image = otis::stereoImage(L.data(), R.data(), n);
        st->install(image, rate, n, true); ml->install(L, rate); mr->install(R, rate);
        for (auto* e : {st.get(), ml.get(), mr.get()}) configure(e->controls, 77 + scenario);
        int limit = st->voiceLimit(otis::clamp(st->controls.engineMode, 0, 2));
        assert(st->sample.stereo && limit == otis::voiceLimits[st->controls.engineMode] / 2);
        int voices = 1 + int(rnd() % limit);
        for (auto* e : {st.get(), ml.get(), mr.get()}) for (int i = 0; i < voices; ++i) e->trigger(i);
        for (int t = 0; t < 3000; ++t) {
            int retrigger = t % 500 == 200 ? int(t / 500) % voices : -1, release = t % 700 == 350 ? int(t / 700) % voices : -1;
            for (auto* e : {st.get(), ml.get(), mr.get()}) { if (retrigger >= 0) e->trigger(retrigger); if (release >= 0) e->release(release); }
            float l = st->process(0), r = st->rightOutput, a = ml->process(0), b = mr->process(0);
            assert(same(l, a) && same(r, b));
            assert(same(ml->rightOutput, a)); // a mono sample plays the same on both outputs
            assert(st->position() == ml->position());
        }
    }
}

// Each Rack voice of a stereo sample costs two chip voices.
static void polyphony() {
    auto e = std::make_unique<Engine>(); auto L = noise(4000), R = noise(4000);
    auto image = otis::stereoImage(L.data(), R.data(), L.size()); e->install(image, 29761.9, L.size(), true);
    const int expected[3] = {8, 7, 4};
    for (int mode = 0; mode < 3; ++mode) {
        e->stopAll(); e->controls.engineMode = mode; e->process(0);
        for (int i = 0; i < 16; ++i) e->trigger(i);
        int running = 0; for (auto& v : e->voices) running += v.running;
        assert(running == expected[mode] && e->voiceLimit(mode) == expected[mode]);
    }
    // A mono sample keeps the full count; switching to stereo stops everything first.
    auto m = noise(4000); e->install(m, 29761.9); e->controls.engineMode = 0; e->process(0);
    for (int i = 0; i < 16; ++i) e->trigger(i);
    int running = 0; for (auto& v : e->voices) running += v.running;
    assert(running == 16);
}

// Capacity: one channel of a stereo sample holds half the memory.
static void capacity() {
    std::vector<int16_t> big(otis::memoryWords, 1);
    auto e = std::make_unique<Engine>(); auto image = otis::stereoImage(big.data(), big.data(), big.size());
    e->install(image, 44100, big.size(), true);
    assert(e->frames == otis::stereoFrames && otis::stereoFrames * 2 == otis::memoryWords);
    std::vector<int16_t> l, r; double rate; uint32_t rev;
    assert(e->snapshotStereo(l, r, rate, rev) && l.size() == otis::stereoFrames && r.size() == otis::stereoFrames);
    std::vector<int16_t> mono; e->snapshot(mono, rate, rev); assert(mono == l);
    size_t frames; bool stereo; std::vector<int16_t> img; e->snapshotImage(img, rate, rev, frames, stereo);
    assert(stereo && frames == otis::stereoFrames && img.size() == otis::stereoOffset + frames);
    // A stereo take stops at stereoFrames and lights FULL.
    e->controls.recordStereo = true; e->controls.captureIndex = 6; e->setHostRate(384000); e->startRecord();
    for (size_t i = 0; i < otis::stereoFrames * 9 && e->recording; ++i) e->process(.1f, -.1f);
    assert(!e->recording && e->recordFull && e->frames == otis::stereoFrames && e->sample.stereo);
}

// A stereo take equals two mono takes of each input, sample for sample, for every record source.
static void recording() {
    for (int source = 0; source < 3; ++source) for (int capture = 0; capture < 7; capture += 3) {
        auto st = std::make_unique<Engine>(), ml = std::make_unique<Engine>(), mr = std::make_unique<Engine>();
        // Same starting sample on all three (silent, so "own output" is zero and the takes stay comparable).
        std::vector<int16_t> z(100, 0);
        auto image = otis::stereoImage(z.data(), z.data(), z.size()); st->install(image, 29761.9, z.size(), true);
        ml->install(z, 29761.9); mr->install(z, 29761.9);
        for (auto* e : {st.get(), ml.get(), mr.get()}) { e->controls.recordSource = source; e->controls.captureIndex = capture; e->controls.recordGain = 1.3f; }
        st->controls.recordStereo = true;
        for (auto* e : {st.get(), ml.get(), mr.get()}) e->startRecord();
        for (int t = 0; t < 20000; ++t) {
            float a = float(std::sin(t * .011)) * .4f, b = float(std::sin(t * .037 + 1)) * .3f;
            st->process(a, b); ml->process(a); mr->process(b);
        }
        for (auto* e : {st.get(), ml.get(), mr.get()}) e->stopRecord();
        std::vector<int16_t> l, r, a, b; double rate; uint32_t rev;
        assert(st->snapshotStereo(l, r, rate, rev)); ml->snapshot(a, rate, rev); mr->snapshot(b, rate, rev);
        assert(l == a && r == b && st->frames == ml->frames);
        if (source != 1) assert(l != r);   // source 1 records the (silent) own output only
    }
    // Without recordStereo the take is mono and ignores inputR, as before.
    auto e = std::make_unique<Engine>(); e->startRecord();
    for (int t = 0; t < 5000; ++t) e->process(.2f, -.7f);
    e->stopRecord(); assert(!e->sample.stereo && e->frames > 100);
}

static void wavFiles() {
    std::string dir = "tests/build/";
    auto L = noise(3000), R = noise(3000);
    otis::writeWav(dir + "st.wav", L.data(), L.size(), 44100, R.data());
    auto w = otis::loadWav(dir + "st.wav");
    assert(w.stereo && w.pcm == L && w.right == R && w.rate == 44100 && !w.truncated);
    auto m = otis::loadWav(dir + "st.wav", false);   // "Load WAV as mono": the channels averaged, as before 2.6
    assert(!m.stereo && m.right.empty() && m.pcm.size() == L.size());
    for (size_t i = 0; i < L.size(); ++i) assert(m.pcm[i] == otis::quantize(float((L[i] / 32768.0 + R[i] / 32768.0) / 2)));
    otis::writeWav(dir + "mono.wav", L.data(), L.size(), 22050);
    auto mm = otis::loadWav(dir + "mono.wav"); assert(!mm.stereo && mm.pcm == L);
    // Three channels still average to mono.
    {
        FILE* f = std::fopen((dir + "three.wav").c_str(), "wb");
        auto p16 = [&](uint16_t v) { std::fputc(v & 255, f); std::fputc(v >> 8, f); };
        auto p32 = [&](uint32_t v) { p16(uint16_t(v)); p16(uint16_t(v >> 16)); };
        std::fwrite("RIFF", 1, 4, f); p32(36 + 4 * 6); std::fwrite("WAVEfmt ", 1, 8, f); p32(16); p16(1); p16(3); p32(8000); p32(48000); p16(6); p16(16);
        std::fwrite("data", 1, 4, f); p32(4 * 6);
        for (int i = 0; i < 4; ++i) { p16(uint16_t(int16_t(3000))); p16(uint16_t(int16_t(-3000))); p16(uint16_t(int16_t(9000))); }
        std::fclose(f);
        auto t = otis::loadWav(dir + "three.wav"); assert(!t.stereo && t.pcm.size() == 4 && t.pcm[0] == 3000);
    }
    // A long stereo file is cut at stereoFrames and marked truncated.
    std::vector<int16_t> big(otis::stereoFrames + 10, 5);
    otis::writeWav(dir + "big.wav", big.data(), big.size(), 48000, big.data());
    auto bw = otis::loadWav(dir + "big.wav"); assert(bw.stereo && bw.truncated && bw.pcm.size() == otis::stereoFrames && bw.right.size() == otis::stereoFrames);
}

// Rework treats both channels with the same grains and one gain.
static void rework() {
    for (int k = 0; k < 30; ++k) {
        rng = 500 + k; size_t n = 3000 + rnd() % 9000; auto L = noise(n);
        auto image = otis::stereoImage(L.data(), L.data(), n); image.resize(otis::stereoOffset + n);
        size_t a = rnd() % (n / 2), b = a + 2 + rnd() % (n - a - 2); float s = unit(), amt = .2f + unit() * .8f; uint32_t seed = rnd();
        auto mono = L; otis::reworkLoop(mono, a, b, s, amt, seed);
        assert(otis::reworkLoop(image, a, b, s, amt, seed, nullptr, otis::stereoOffset));
        for (size_t i = 0; i < n; ++i) {
            assert(image[i] == image[otis::stereoOffset + i]);                     // identical channels stay identical
            assert(std::abs(int(image[i]) - int(mono[i])) <= 1);                   // and match a mono rework
            if (i < a || i >= b) assert(image[i] == L[i]);                         // nothing outside the window
        }
    }
    // Through the worker: a stereo sample's rework lands in both channels and keeps it stereo.
    auto e = std::make_unique<Engine>(); auto L = noise(20000), R = noise(20000);
    auto image = otis::stereoImage(L.data(), R.data(), L.size()); e->install(image, 29761.9, L.size(), true);
    otis::LoopMutator mutator(*e);
    assert(mutator.request(2000, 9000, .5f, 1.f));
    for (int t = 0; t < 2000000 && mutator.lastOutcome() != otis::LoopMutator::Applied; ++t) { mutator.service(); e->process(0); }
    assert(mutator.lastOutcome() == otis::LoopMutator::Applied);
    std::vector<int16_t> l, r; double rate; uint32_t rev; assert(e->snapshotStereo(l, r, rate, rev));
    uint32_t seed = 0x41535210; seed = otis::mutationRandom(seed);
    auto expected = otis::stereoImage(L.data(), R.data(), L.size()); expected.resize(otis::stereoOffset + L.size());
    otis::reworkLoop(expected, 2000, 9000, .5f, 1.f, seed, nullptr, otis::stereoOffset);
    assert(std::equal(l.begin(), l.end(), expected.begin()) && std::equal(r.begin(), r.end(), expected.begin() + otis::stereoOffset));
    assert(l != L && r != R);
    for (int t = 0; t < 48000; ++t) { mutator.service(); e->process(0); }
}

int main() {
    // Rate changes reset both converter histories, as two mono engines do.
    for(float clock:{0.f,-1.f}) {
        auto e=std::make_unique<Engine>(); std::vector<int16_t> pcm(2000,12000);
        auto image=otis::stereoImage(pcm.data(),pcm.data(),pcm.size());
        e->install(image,29761.9,pcm.size(),true); e->controls.clockOctaves=clock;
        e->controls.loop=1; e->trigger(0);
        for(int i=0;i<1000;++i) e->process(0);
        e->setHostRate(96000);
        for(int i=0;i<100;++i) { float left=e->process(0); assert(same(left,e->rightOutput)); }
    }
    lockstep(); polyphony(); capacity(); recording(); wavFiles(); rework();
    std::cout << "PASS: stereo pairs equal two mono engines (60 scenarios, all modes), half polyphony and memory, stereo takes equal two mono takes, stereo/mono/3-channel WAV, joint rework through the worker\n";
}
