// Portable, exhaustive byte-map tests and an independent actual-Voice compiler reference.
#include "../src/engine/CompileVastish.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>

static std::atomic<size_t> allocations{0};
void* operator new(std::size_t n) { ++allocations; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
using namespace dspemu;
namespace lu = dspemu::lookup;

static bus::Sample input(uint8_t word) {
    bus::Sample s; s.connected = true; s.rawWord = s.word = word; s.updated = true; return s;
}
static void maps(const lu::Library& library) {
    lu::Engine engine; lu::Parameters p; p.family = lu::Curve;
    for (int n = 0; n < 256; ++n) assert(engine.tick(library,p,input(n),48000).word == n);
    p.shape = 32;
    for (int n = 0; n < 256; ++n) assert(engine.tick(library,p,input(n),48000).word == 255-n);
    p.shape = 0; p.phase = .5;
    for (int n = 0; n < 256; ++n) assert(engine.tick(library,p,input(n),48000).word == ((n+128)&255));
    p.phase = -1.;
    for (int n = 0; n < 256; ++n) assert(engine.tick(library,p,input(n),48000).word == n);
    // Interpolation is arithmetic, not a choice or an interpolation of bit masks.
    p.shape = 7.5; p.phase = 0;
    for (int n = 0; n < 256; ++n) {
        int a = library.table(lu::Curve,7)[n], b = library.table(lu::Curve,8)[n];
        assert(engine.tick(library,p,input(n),48000).word == (a+b+1)/2);
    }
    lu::Table custom{}; for (int n = 0; n < 256; ++n) custom[n] = 255-n;
    p.family = lu::Custom; p.shape = 32;
    for (int n = 0; n < 256; ++n) assert(engine.tick(library,p,input(n),48000,false,&custom).word == 255-n);
    p.shape = 16;
    for (int n = 0; n < 256; ++n) assert(std::abs(int(engine.tick(library,p,input(n),48000,false,&custom).word)-128) <= 1);
    p.shape = p.phase = std::numeric_limits<double>::quiet_NaN();
    assert(engine.tick(library,p,input(7),48000,false,&custom).word == 7);
    assert(engine.tick(library,p,{},48000).word == 128);
}

static void patterns(const lu::Library& library) {
    lu::Parameters p; p.family = lu::Walsh;
    for (int shape = 0; shape < 64; ++shape) for (int seed = 0; seed < 256; ++seed) {
        p.shape = shape; p.seed = seed;
        std::array<bool,256> seen{};
        for (int n = 0; n < 256; ++n) {
            int value = lu::evaluate(library,p,n,shape);
            assert(!seen[value]); seen[value] = true;
        }
    }
    // Pattern selection has hysteresis; numerical families are not quantized to it.
    lu::Selector select; assert(select.choose(0) == 0);
    assert(select.choose(.54) == 0 && select.choose(.56) == 1);
    assert(select.choose(.47) == 1 && select.choose(.44) == 0);
    assert(select.choose(63.8) == 0 && select.choose(63.4) == 63);
    p.family = lu::Rhythm;
    for (int shape = 0; shape < 64; ++shape) for (int seed = 0; seed < 256; ++seed) {
        p.shape = shape; p.seed = seed;
        for (int n = 0; n < 256; ++n) {
            uint8_t value = lu::evaluate(library,p,n,shape);
            if (n%8 >= 4) assert(value == 0);
            else assert((value & 3) == (library.table(lu::Rhythm,shape)[n] & 3));
        }
        // Kick and snare anchors survive every seed.
        assert(lu::evaluate(library,p,0,shape) & 1);
        assert(lu::evaluate(library,p,64,shape) & 2);
        assert(lu::evaluate(library,p,192,shape) & 2);
    }
}

static void audio(const lu::Library& library) {
    for (int family = 0; family < 3; ++family) for (int shape = 0; shape < 64; ++shape) {
        const auto* a = library.table(family,shape);
        const auto* b = library.table(family,(shape+1)&63);
        double dc = 0, power = 0, delta = 0;
        for (int n = 0; n < 256; ++n) {
            double x = bus::bipolar(a[n]); dc += x; power += x*x;
            double d = double(a[n]-b[n])/128.; delta += d*d;
            assert(std::abs(x) <= .95);
        }
        assert(std::abs(dc/256) < 1/128.);
        assert(std::sqrt(power/256) > .25 && std::sqrt(power/256) < .52);
        assert(std::sqrt(delta/256) < .1); // Includes shape 63 -> 0, not only interior neighbors.
    }
}

static void clocking(const lu::Library& library) {
    lu::Parameters p; p.family = lu::Curve; lu::Engine engine;
    auto s = input(20); s.clocked = true; s.updated = false;
    auto out = engine.tick(library,p,s,48000);
    assert(out.word == 128 && out.clocked && !out.clk);
    s.rising = s.clk = true; s.updated = true;
    out = engine.tick(library,p,s,48000); assert(out.word == 20 && out.clk);
    p.shape = 32; s.rawWord = 30; s.rising = false;
    assert(engine.tick(library,p,s,48000).word == 20);
    p.engineClock = true;
    out = engine.tick(library,p,s,48000); assert(out.word == 225 && !out.clocked && !out.clk);
}

static void tracking(const lu::Library& library) {
    for (double sr : {44100.,48000.,96000.}) for (bool falling : {false,true}) {
        lu::Engine engine; lu::Parameters p; p.family = lu::Curve;
        p.mode = falling ? lu::Mode::TrackFalling : lu::Mode::TrackRising;
        lu::Output out;
        for (int t = 0; t < 512*5; ++t) {
            int value = 32 + (t%512)*192/512;
            out = engine.tick(library,p,input(falling ? 255-value : value),sr);
            assert(!out.clocked && !out.clk);
            if (t >= 512*4) {
                assert(out.locked);
                // Numeric address interpolation crosses the ramp's discontinuity
                // between table point 255 and point 0 during the final half point.
                double phase = (t%512)/2.;
                int expected = int(std::floor((phase <= 255 ? phase : 255*(256-phase))+.5));
                assert(std::abs(int(out.word)-expected) <= 1);
            }
        }
        assert(std::abs(engine.follower.period - 512/sr) < 1/sr);
        // Abrupt pitch changes reacquire; constant input expires the lock.
        for (int t = 0; t < 256*5; ++t) {
            int value = 32 + (t%256)*192/256;
            out = engine.tick(library,p,input(falling ? 255-value : value),sr);
        }
        assert(out.locked && std::abs(engine.follower.period-256/sr) < 1/sr);
        for (int t = 0; t < 256*4; ++t) out = engine.tick(library,p,input(100),sr);
        assert(!out.locked && out.word == 100);
        engine.tick(library,p,input(100),sr,true); assert(!engine.follower.locked);
        engine.tick(library,p,{},sr); assert(!engine.follower.seen);
    }
}

static void compiler() {
    auto p = vastish::programs()[0];
    p[vastish::ALG] = 1; p[vastish::PITCH_ADJ] = 60; p[vastish::PITCH_KT] = p[vastish::PITCH_VT] = 0;
    p[vastish::PITCH_S1] = p[vastish::PITCH_S2] = 0;
    p[vastish::F1_TYPE] = p[vastish::F2_TYPE] = p[vastish::F3_TYPE] = 0;
    auto table = lu::compileVastish(p);
    for (int n = 0; n < 256; ++n) assert(table[n] == n);
    p[vastish::PITCH_ADJ] = 66; table = lu::compileVastish(p);
    for (int n = 0; n < 256; ++n) assert(table[n] == ((n+128)&255));
    p[vastish::PITCH_ADJ] = 60; p[vastish::PITCH_WAVE] = 0;
    p[vastish::AMP_ADJ] = 0; p[vastish::AMP_KT] = p[vastish::AMP_VT] = 0;
    p[vastish::AMP_S1] = p[vastish::AMP_S2] = 0;
    p[vastish::PAN_ADJ] = -1; p[vastish::PAN_KT] = p[vastish::PAN_VT] = 0;
    p[vastish::PAN_S1] = p[vastish::PAN_S2] = 0;
    lu::FrozenControls frozen; frozen.sources[vastish::SRC_AUX1] = .25f;
    frozen.sources[vastish::SRC_AUX1+1] = -.5f; frozen.sources[11] = 1;
    p[vastish::FUN1_A] = vastish::SRC_AUX1; p[vastish::FUN1_B] = vastish::SRC_AUX1+1; p[vastish::FUN1_EQ] = 2;
    for (int algo = 1; algo <= 6; ++algo) for (int type : {0,7,8,11,12}) {
        p[vastish::ALG] = algo;
        p[vastish::F1_TYPE] = type; p[vastish::F2_TYPE] = 11; p[vastish::F3_TYPE] = 0;
        p[vastish::F1_ADJ] = type == 12 ? 4 : 2;
        p[vastish::F1_KT] = p[vastish::F1_VT] = 0;
        p[vastish::F1_S1] = vastish::SRC_FUN1; p[vastish::F1_D1] = 1;
        p[vastish::F2_ADJ] = 0; p[vastish::F2_KT] = p[vastish::F2_VT] = 0;
        p[vastish::F2_S1] = p[vastish::F2_S2] = 0;
        p[vastish::F3_S1] = p[vastish::F3_S2] = 0;
        table = lu::compileVastish(p,frozen);
        // Compare to the existing full Voice::tick, bypassing no chain code.
        vastish::Voice voice; voice.gate = true; voice.velocity = 1;
        voice.env[0].stage = vastish::Envelope::Sustain;
        voice.env[0].ramp.value = voice.env[0].ramp.target = voice.env[0].sustain = 1;
        vastish::VoiceInput in; in.gate = true; in.aux[0] = .25f; in.aux[1] = -.5f;
        for (int n = 0; n < 256; ++n) {
            voice.pitch.phase = n/256.;
            auto y = voice.tick(p,in,48000);
            assert(table[n] == bus::fromBipolar(y[0]));
        }
    }
    // An AUDIO-wave program has no table: its input is live audio, not a byte.
    {
        auto audio = p; audio[vastish::PITCH_WAVE] = vastish::AUDIO_WAVE; bool rejected = false;
        try { lu::compileVastish(audio); } catch (const std::runtime_error& e) { rejected = std::string(e.what()).find("AUDIO") != std::string::npos; }
        assert(rejected);
    }
    for (int type = 1; type <= 14; ++type) if (!lu::memoryless(type)) {
        p[vastish::F1_TYPE] = type; bool rejected = false;
        try { lu::compileVastish(p); } catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
}

int main() {
    auto library = lu::Library::load("../res/lookup-v1.bin");
    maps(library); patterns(library); audio(library); clocking(library); tracking(library); compiler();
    bool rejected = false; try { lu::Library::load("../res/no-such-library.bin"); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    lu::Engine engine; lu::Parameters p; auto s = input(73);
    engine.tick(library,p,s,48000);
    size_t before = allocations.load();
    for (int i = 0; i < 100000; ++i) {
        p.family = i % 6; p.shape = (i%128)*.5; p.seed = uint8_t(i);
        engine.tick(library,p,s,48000);
    }
    assert(allocations.load() == before);
    std::cout << "PASS: Lookup identities, interpolation, 64x256 exhaustive Walsh permutations, rhythm gaps/anchors, audio bounds/seams, clocking, ramp tracking at 44.1/48/96 kHz, actual Vastish Voice compiler parity, no runtime allocations\n";
}
