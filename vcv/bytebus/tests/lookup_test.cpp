// Portable, exhaustive byte-map tests and an independent actual-Voice compiler reference.
#include "../src/engine/CompileVastish.hpp"
#include "../src/engine/Lookup2.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

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

// JUMP (docs/LOOKUP.md, "JUMP"): the BUS TRIG rule on a selected bit, through a real Reader.
struct Cable {
    int channels = 0; std::array<float,16> voltage{};
    int getChannels() { return channels; }
    float getVoltage(int k) { return voltage[k]; }
    void setVoltage(float v,int k = 0) { voltage[k] = v; }
};
static void put(Cable& c,int word,int channels,bool clk = false) {
    c.channels = channels; c.voltage = {};
    for (int k = 0; k < std::min(channels,8); ++k) c.voltage[k] = word >> k & 1 ? 10.f : 0.f;
    if (channels >= 9) c.voltage[8] = clk ? 10.f : 0.f;
}
static void jumps() {
    // Clocked: the first edge only arms; then each CLK edge with the bit high steps, including a
    // bit held high across publications and adjacent high words. Low bits and the low half don't.
    {
        bus::Reader r; lu::Jump j; Cable c; int steps = 0;
        auto publish = [&](int word,int bit) {
            put(c,word,9,true); steps += j.tick(r.process(c),bit);
            put(c,word,9,false); steps += j.tick(r.process(c),bit);
        };
        publish(0x01,0); assert(steps == 0 && j.steps == 0);          // arms; bit already high
        publish(0x01,0); publish(0x01,0); assert(steps == 2);          // held high: one per publication
        publish(0x00,0); publish(0xfe,0); assert(steps == 2);          // bit 0 low
        publish(0x08,3); publish(0x08,3); assert(steps == 4 && j.steps == 4);
        for (int k = 0; k < 64; ++k) publish(0xff,7);
        assert(j.steps == 4);                                          // 64 steps is a full turn
        // Disconnecting keeps the steps; reconnecting arms again.
        c.channels = 0; j.tick(r.process(c),0); assert(j.steps == 4);
        publish(0x01,0); assert(j.steps == 4); publish(0x01,0); assert(j.steps == 5);
    }
    // Unclocked (eight or fewer channels, or a gate on one): each rising edge of the bit steps. A bit
    // already high when the cable arrives, or when the selection moves onto it, does not.
    {
        bus::Reader r; lu::Jump j; Cable c;
        put(c,0x01,1); j.tick(r.process(c),0); assert(j.steps == 0);
        for (int k = 0; k < 10; ++k) j.tick(r.process(c),0);
        assert(j.steps == 0);
        for (int k = 0; k < 5; ++k) {
            put(c,0,1); j.tick(r.process(c),0);
            put(c,1,1); j.tick(r.process(c),0); j.tick(r.process(c),0);
        }
        assert(j.steps == 5);
        put(c,0x20,8); j.tick(r.process(c),0); assert(j.steps == 5);   // a wider cable, bit 0 low
        j.tick(r.process(c),5); assert(j.steps == 5);                   // select a high bit: no step
        put(c,0x00,8); j.tick(r.process(c),5); put(c,0x20,8); j.tick(r.process(c),5); assert(j.steps == 6);
        // Schmitt thresholds: 0.5 V neither rises nor falls.
        c.voltage[5] = .5f; j.tick(r.process(c),5); c.voltage[5] = 10.f; j.tick(r.process(c),5); assert(j.steps == 6);
        // Unclocked to clocked starts over: the new clock's first edge arms.
        put(c,0x20,9,true); j.tick(r.process(c),5); assert(j.steps == 6);
        put(c,0x20,9,false); j.tick(r.process(c),5); put(c,0x20,9,true); j.tick(r.process(c),5); assert(j.steps == 7);
        // And clocked to unclocked seeds: a high bit does not step.
        put(c,0x20,8); j.tick(r.process(c),5); assert(j.steps == 7);
    }
    // A stepped SHAPE is the same table as the knob moved by that many shapes, in every family.
    {
        auto library = lu::Library::load("../res/lookup-v1.bin");
        for (int family : {lu::Harmonic, lu::Fold, lu::Curve, lu::Walsh, lu::Rhythm})
            for (int knob : {0, 5, 63}) for (int steps : {1, 2, 33}) {
                lu::Engine a, b; lu::Parameters pa, pb; pa.family = pb.family = family;
                pa.shape = knob + steps; pb.shape = (knob + steps) % 64;
                for (int n = 0; n < 256; n += 7) assert(a.tick(library,pa,input(uint8_t(n)),48000).word == b.tick(library,pb,input(uint8_t(n)),48000).word);
            }
    }
}

// Library v2 (docs/LOOKUP.md): Envelope, Sequence and Density follow Custom; each reads its own
// table; Density is discrete like Rhythm; without v2 they are neutral and v1 is untouched.
static void libraryV2() {
    auto library = lu::Library::load("../res/lookup-v1.bin");
    assert(library.ready && !library.readyV2);
    { lu::Engine e; lu::Parameters p; p.family = lu::Envelope; assert(e.tick(library,p,input(40),48000).word == 128); }
    auto v1 = library.data;
    library.loadV2("../res/lookup-v2.bin"); assert(library.readyV2);
    assert(std::equal(v1.begin(), v1.begin() + lu::FAMILIES * lu::SHAPES * lu::POINTS, library.data.begin()));
    bool rejected = false; try { lu::Library other; other.loadV2("../res/lookup-v1.bin"); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    std::ifstream file("../res/lookup-v2.bin", std::ios::binary); std::vector<char> raw((std::istreambuf_iterator<char>(file)), {});
    for (int family : {lu::Envelope, lu::Sequence, lu::Density}) for (int shape : {0, 17, 63}) {
        const uint8_t* stored = reinterpret_cast<const uint8_t*>(raw.data()) + 12 + ((family - lu::Envelope) * 64 + shape) * 256;
        lu::Engine e; lu::Parameters p; p.family = family; p.shape = shape;
        for (int n = 0; n < 256; ++n) assert(e.tick(library,p,input(uint8_t(n)),48000).word == stored[n]);
    }
    // Numeric morph for Envelope and Sequence: halfway between two shapes is between their bytes.
    {
        lu::Engine e; lu::Parameters p; p.family = lu::Envelope; p.shape = 2.5;
        for (int n = 0; n < 256; n += 5) {
            int a = library.table(lu::Envelope,2)[n], b = library.table(lu::Envelope,3)[n], v = e.tick(library,p,input(uint8_t(n)),48000).word;
            assert(v >= std::min(a,b) && v <= std::max(a,b));
        }
    }
    // Density is discrete with Rhythm's hysteresis: 10.4 reads shape 10, never a blend.
    {
        lu::Engine e; lu::Parameters p; p.family = lu::Density; p.shape = 10.4;
        for (int n = 0; n < 256; ++n) assert(e.tick(library,p,input(uint8_t(n)),48000).word == library.table(lu::Density,10)[n]);
        p.shape = 10.6; assert(e.tick(library,p,input(0),48000).word == library.table(lu::Density,11)[0]);
    }
    // FAMILY beyond the last family clamps to it: Ratio since 2.11, neutral here without v3.
    { lu::Engine e; lu::Parameters p; p.family = 42; p.shape = 5; assert(e.tick(library,p,input(0),48000).word == 128); }
}

// Lookup2 (docs/LOOKUP2.md): every (A, B) through Logic and Arithmetic against independent
// references; Walsh linear over GF(2) and one-to-one in each argument; Fold the identity at 0.
// Library v3 (docs/LOOKUP.md, Ratio): FAMILY 10 reads its own tables; ratio words are never blended,
// across shapes or across fractional addresses; without v3 it is neutral; v1 and v2 are untouched.
static void libraryV3() {
    auto library = lu::Library::load("../res/lookup-v1.bin"); library.loadV2("../res/lookup-v2.bin");
    { lu::Engine e; lu::Parameters p; p.family = lu::Ratio; assert(e.tick(library,p,input(40),48000).word == 128); }
    auto before = library.data;
    library.loadV3("../res/lookup-v3.bin"); assert(library.readyV3);
    assert(std::equal(before.begin(), before.begin() + (lu::FAMILIES + lu::V2_FAMILIES) * lu::SHAPES * lu::POINTS, library.data.begin()));
    bool rejected = false; try { lu::Library other; other.loadV3("../res/lookup-v2.bin"); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    std::ifstream file("../res/lookup-v3.bin", std::ios::binary); std::vector<char> raw((std::istreambuf_iterator<char>(file)), {});
    for (int shape = 0; shape < 64; ++shape) {
        const uint8_t* stored = reinterpret_cast<const uint8_t*>(raw.data()) + 12 + shape * 256;
        lu::Engine e; lu::Parameters p; p.family = lu::Ratio; p.shape = shape;
        for (int n = 0; n < 256; ++n) assert(e.tick(library,p,input(uint8_t(n)),48000).word == stored[n]);
        // A fractional PHASE moves the address; the result is still one whole word of the table.
        p.phase = 0.37 / 256;
        std::set<int> words(stored, stored + 256);
        for (int n = 0; n < 256; n += 3) assert(words.count(e.tick(library,p,input(uint8_t(n)),48000).word));
    }
    // Shape 13.4 reads shape 13, never a blend; ratio words decode to n/d from 1/16 to 16.
    { lu::Engine e; lu::Parameters p; p.family = lu::Ratio; p.shape = 13.4; assert(e.tick(library,p,input(77),48000).word == library.table(lu::Ratio,13)[77]); }
    for (int w = 0; w < 256; ++w) assert(lu::ratioN(uint8_t(w)) == (w >> 4) + 1 && lu::ratioD(uint8_t(w)) == (w & 15) + 1);
    { lu::Engine e; lu::Parameters p; p.family = 99; p.shape = 2; assert(e.tick(library,p,input(0),48000).word == library.table(lu::Ratio,2)[0]); }
}

static void twoWords() {
    namespace l2 = dspemu::lookup2;
    // Logic: the curated order, each shape a distinct bitwise truth table over (a, b).
    const int truth[16] = {0b1100, 0b1000, 0b1110, 0b0110, 0b1010, 0b0111, 0b0001, 0b1001,
                           0b0100, 0b0010, 0b1101, 0b1011, 0b0011, 0b0101, 0b0000, 0b1111}; // bit (2a + b)
    std::set<int> tables;
    for (int f = 0; f < 16; ++f) {
        tables.insert(truth[f]);
        for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b) {
            int out = l2::logic(f, uint8_t(a), uint8_t(b));
            for (int k = 0; k < 8; ++k) assert((out >> k & 1) == (truth[f] >> (2 * (a >> k & 1) + (b >> k & 1)) & 1));
            assert(l2::logic(f + 16, uint8_t(a), uint8_t(b)) == out);              // four passes around SHAPE
        }
    }
    assert(tables.size() == 16);
    // Arithmetic against plain-integer and floating references.
    for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; ++b) {
        auto clip = [](double v) { return int(std::max(0., std::min(255., v))); };
        const int want[16] = {
            (a + b) & 255, clip(a + b - 128), (a - b) & 255, std::abs(a - b), int(std::lround(a * b / 255.)),
            clip(128 + std::floor((a - 128) * (b - 128) / 128. + .5)), (a + b + 1) / 2, std::max(a, b), std::min(a, b),
            a > b ? 255 : 0, a == b ? 255 : 0, clip(a + b), clip(a - b), (a * b) & 255, (a * b) >> 8, a % (b + 1)};
        for (int f = 0; f < 16; ++f) assert(l2::arithmetic(f, uint8_t(a), uint8_t(b)) == want[f]);
    }
    // Walsh: shape 0 is A XOR B; every shape is linear and one-to-one in A for each B and in B for each A.
    l2::Engine e;
    std::set<std::array<uint16_t, 8>> distinct;
    std::mt19937 rng(8);
    for (int s = 0; s < 64; ++s) {
        distinct.insert(e.masks[s]);
        auto f = [&](int a, int b) { return e.evaluate(l2::Walsh, s, uint8_t(a), uint8_t(b)); };
        e.reset();
        if (s == 0) for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; b += 17) assert(f(a, b) == (a ^ b));
        for (int fixed = 0; fixed < 256; ++fixed) {
            std::set<int> byA, byB;
            for (int v = 0; v < 256; ++v) { byA.insert(f(v, fixed)); byB.insert(f(fixed, v)); }
            assert(byA.size() == 256 && byB.size() == 256);
        }
        // Every shape is a fixed byte permutation of A XOR B, so equal words give 00 (LOOKUP2.md;
        // review 2026-10-27 #3, kept so saved patches sound the same).
        for (int a = 0; a < 256; ++a) {
            assert(f(a, a) == 0);
            for (int b = 0; b < 256; ++b) assert(f(a, b) == f(a ^ b, 0));
        }
        for (int k = 0; k < 200; ++k) {
            int a1 = rng() & 255, a2 = rng() & 255, b1 = rng() & 255, b2 = rng() & 255;
            assert(f(a1 ^ a2, b1 ^ b2) == (f(a1, b1) ^ f(a2, b2)));
        }
    }
    assert(distinct.size() == 64);
    // Fold: exactly A at shape 0 (and 64); continuous in SHAPE; B matters elsewhere.
    for (int a = 0; a < 256; ++a) for (int b = 0; b < 256; b += 5) {
        assert(l2::fold(0, uint8_t(a), uint8_t(b)) == a && l2::fold(64, uint8_t(a), uint8_t(b)) == a);
        for (double s = 0; s < 64; s += 1.37) assert(std::abs(int(l2::fold(s, uint8_t(a), uint8_t(b))) - int(l2::fold(s + .01, uint8_t(a), uint8_t(b)))) <= 3);
    }
    { int differ = 0; for (int b = 0; b < 256; ++b) differ += l2::fold(32, 100, uint8_t(b)) != l2::fold(32, 100, 128); assert(differ > 100); }
    // Discrete families select with Lookup's hysteresis: 2.4 reads OR, never a blend.
    { l2::Engine g; assert(g.evaluate(l2::Logic, 2.4, 0x0f, 0x30) == 0x3f); }
    // Clocking: A's CLK edge evaluates with B's raw word and the result holds; unclocked A every sample.
    {
        l2::Engine g; l2::Parameters p; p.family = l2::Logic; p.shape = 3;
        bus::Sample a; a.connected = a.clocked = a.rising = a.clk = true; a.rawWord = 0xf0;
        auto o = g.tick(p, a, 0x0f); assert(o.evaluated && o.word == 0xff && o.clocked && o.clk);
        a.rising = false; a.rawWord = 0; o = g.tick(p, a, 0x01); assert(!o.evaluated && o.word == 0xff && o.clk);
        a.clk = false; o = g.tick(p, a, 0x01); assert(o.word == 0xff && !o.clk);
        bus::Sample u; u.connected = true; u.rawWord = 0x33; o = g.tick(p, u, 0x0f); assert(o.evaluated && o.word == 0x3c && !o.clocked);
        p.family = 42; o = g.tick(p, u, 0x0f); assert(o.word == g.evaluate(l2::Walsh, 3, 0x33, 0x0f));       // clamped
    }
}

// Review 2026-10-27 #20: after a reset, Lookup2 with a clocked, stopped A publishes the result of its
// present inputs on the first sample, without a CLK edge.
static void lookup2ReloadRegression() {
    namespace l2 = dspemu::lookup2;
    l2::Engine g; l2::Parameters p; p.family = l2::Logic; p.shape = 2;               // OR
    bus::Sample a; a.connected = a.clocked = true; a.rawWord = 0x0f;
    auto o = g.tick(p, a, 0x30); assert(o.word == 0x3f && o.clocked && !o.clk);
    a.rawWord = 0x40; o = g.tick(p, a, 0x01); assert(o.word == 0x3f && !o.evaluated);  // held until A's edge
    a.clk = a.rising = true; o = g.tick(p, a, 0x01); assert(o.evaluated && o.word == 0x41 && o.clk);
    // After reset(), as on reload; an unpatched A first (Rack before its cables carry data).
    g.reset(); a = bus::Sample{}; g.tick(p, a, 0x30);
    a.connected = a.clocked = true; a.rawWord = 0x81; o = g.tick(p, a, 0x02);
    assert(o.word == 0x83 && !o.clk);
    a.rawWord = 0; o = g.tick(p, a, 0); assert(o.word == 0x83);
}

int main() {
    auto library = lu::Library::load("../res/lookup-v1.bin");
    maps(library); patterns(library); audio(library); clocking(library); tracking(library); compiler(); jumps(); libraryV2(); libraryV3(); twoWords(); lookup2ReloadRegression();
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
    std::cout << "PASS: Lookup identities, interpolation, 64x256 exhaustive Walsh permutations, rhythm gaps/anchors, audio bounds/seams, clocking, ramp tracking at 44.1/48/96 kHz, actual Vastish Voice compiler parity, JUMP clocked/unclocked steps, arming, seeding and wrap, library v2 Envelope/Sequence/Density tables, morphs and discrete selection, library v3 Ratio words never blended, Lookup2 Logic/Arithmetic on every (A, B), Walsh linearity and one-to-one shapes of A XOR B, Fold identity and continuity, A-clocked holds, no runtime allocations\n";
}
