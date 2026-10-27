// Real Rack module APIs, including a Register -> Vastish FUN -> Register loop, Counter
// CARRY chains and a Counter -> Lookup Rhythm chain.
// The test host supplies a Rack context; the plugin itself uses rack.hpp only.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Register.cpp"
#include "../src/Counter.cpp"
#include "../src/Switch.cpp"
#include "../src/Pipeline.cpp"
#include "../src/Lookup2.cpp"
#include "../src/Counter2.cpp"
#include "../src/Fan.cpp"
#include "../src/Lattice.cpp"
#include "../src/ParallelSum.cpp"
#include "../src/Wheel.cpp"
#include "../src/Lookup.cpp"
#include "../../vastish/src/Vastish.cpp"
#include <array>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
Plugin* pluginInstance = nullptr;

static Module::ProcessArgs args(float sr = 48000) {
    Module::ProcessArgs a{}; a.sampleRate = sr; a.sampleTime = 1 / sr; return a;
}
static void connectOutputs(Module& m) { for (auto& out : m.outputs) out.channels = 1; }
static void putBus(Input& input, uint8_t word, bool clocked = false, bool clk = false) {
    input.channels = clocked ? 9 : 8;
    dspemu::bus::Writer{}.write(input, word, clocked, clk);
}
static uint8_t word(Output& output) {
    uint8_t result = 0;
    for (int k = 0; k < 8; ++k) if (output.getVoltage(k) >= 1) result |= 1 << k;
    return result;
}
static uint8_t inputWord(Input& input) {
    uint8_t result = 0;
    for (int k = 0; k < 8; ++k) if (input.getVoltage(k) >= 1) result |= 1 << k;
    return result;
}
static void copy(Output& source, Input& dest) {
    dest.channels = source.getChannels();
    for (int k = 0; k < 16; ++k) dest.setVoltage(k < dest.getChannels() ? source.getVoltage(k) : 0, k);
}

static void portsAndState() {
    ClockedRegister r; auto a = args(); connectOutputs(r);
    r.model = modelClockedRegister;
    assert(r.params.size() == 4 && r.inputs.size() == 3 && r.outputs.size() == 2);
    r.params[ClockedRegister::CLOCK_MODE].setValue(1); // JACK
    r.inputs[ClockedRegister::CLOCK_IN].channels = 1;
    putBus(r.inputs[ClockedRegister::BUS_IN], 0xa5);
    r.process(a); assert(r.outputs[ClockedRegister::BUS_OUT].getChannels() == 9);
    r.process(a);
    r.inputs[ClockedRegister::CLOCK_IN].setVoltage(10); r.process(a);
    assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 0xa5);
    assert(r.outputs[ClockedRegister::BUS_OUT].getVoltage(8) == 10);
    assert(r.outputs[ClockedRegister::CLOCK_OUT].getVoltage() == 10);
    putBus(r.inputs[ClockedRegister::BUS_IN], 0x5a);
    for (int k = 0; k < 100; ++k) { r.process(a); assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 0xa5); }
    assert(r.outputs[ClockedRegister::CLOCK_OUT].getVoltage() == 0);
    for (int k = 0; k < 8; ++k) assert(r.lights[ClockedRegister::BIT0_LIGHT+k].getBrightness() == (0xa5 & (1<<k) ? 1 : 0));
    // Params and held word survive Rack JSON persistence.
    r.params[ClockedRegister::RESET_WORD].setValue(195);
    json_t* saved = r.toJson(); ClockedRegister restored; restored.model = modelClockedRegister;
    connectOutputs(restored); restored.fromJson(saved); json_decref(saved);
    assert(restored.params[ClockedRegister::RESET_WORD].getValue() == 195);
    assert(restored.params[ClockedRegister::CLOCK_MODE].getValue() == 1);
    restored.process(a); assert(word(restored.outputs[ClockedRegister::BUS_OUT]) == 0xa5);
    restored.params[ClockedRegister::RESET_BUTTON].setValue(1); restored.process(a); restored.process(a);
    assert(word(restored.outputs[ClockedRegister::BUS_OUT]) == 195);
    // Explicit ENGINE mode emits an eight-channel bus, clearing the old CLK.
    r.params[ClockedRegister::CLOCK_MODE].setValue(4); r.process(a);
    assert(r.outputs[ClockedRegister::BUS_OUT].getChannels() == 8);
    assert(r.outputs[ClockedRegister::BUS_OUT].getVoltage(8) == 0);
    assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 0x5a);
    // A short mono gate is word 1, never 255, even if high channels are stale.
    r.inputs[ClockedRegister::BUS_IN].channels = 1;
    r.inputs[ClockedRegister::BUS_IN].setVoltage(10); r.process(a);
    assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 1);
    // Bus CLK mode ignores changes until that bus edge, and doesn't invent a clock.
    r.params[ClockedRegister::CLOCK_MODE].setValue(2);
    putBus(r.inputs[ClockedRegister::BUS_IN], 0x81, true, false); r.process(a);
    assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 1);
    putBus(r.inputs[ClockedRegister::BUS_IN], 0x81, true, true); r.process(a);
    assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 0x81);
    // Invalid saved data is bounded before narrowing to a byte.
    json_t* bad = json_pack("{s:I}", "word", json_int_t(10000000000LL));
    r.dataFromJson(bad); json_decref(bad); r.process(a); assert(word(r.outputs[ClockedRegister::BUS_OUT]) == 255);
}

static std::vector<uint8_t> feedback(float sr, uint8_t seed, unsigned orderSeed) {
    ClockedRegister r; Vastish fun; connectOutputs(r); connectOutputs(fun); auto a = args(sr);
    r.params[ClockedRegister::CLOCK_MODE].setValue(1); // external clock owns the loop
    r.params[ClockedRegister::RESET_WORD].setValue(seed);
    r.inputs[ClockedRegister::CLOCK_IN].channels = r.inputs[ClockedRegister::RESET_IN].channels = 1;
    // Existing Vastish can run all eight FUN lanes with an eight-channel pitch
    // cable. No gate, new BUS wave, or unreleased FUN mode is required.
    fun.inputs[Vastish::PITCH_IN].channels = 8;
    fun.params[vastish::FUN1_A].setValue(vastish::SRC_AUX1);
    fun.params[vastish::FUN1_B].setValue(vastish::SRC_AUX1 + 1);
    fun.params[vastish::FUN1_EQ].setValue(6); // ABS(A-B) = XOR for canonical bits
    putBus(fun.inputs[Vastish::AUX2_IN], 0xff);
    putBus(r.inputs[ClockedRegister::BUS_IN], 0);
    putBus(fun.inputs[Vastish::AUX1_IN], 0, true);
    std::mt19937 order(orderSeed);
    std::array<std::vector<uint8_t>, 2> sequences;
    constexpr int period = 32, duration = period * 12;
    for (int run = 0; run < 2; ++run) {
        for (int t = 0; t < duration; ++t) {
            r.inputs[ClockedRegister::RESET_IN].setVoltage(t == 0 ? 10 : 0);
            r.inputs[ClockedRegister::CLOCK_IN].setVoltage(t % period == 0 ? 10 : 0);
            // Rack cables transport the previous engine sample. Copy both
            // cables before processing either module, including in reverse order.
            copy(r.outputs[ClockedRegister::BUS_OUT], fun.inputs[Vastish::AUX1_IN]);
            copy(fun.outputs[Vastish::FUN1_OUT], r.inputs[ClockedRegister::BUS_IN]);
            if (order() & 1) { r.process(a); fun.process(a); }
            else { fun.process(a); r.process(a); }
            if (r.outputs[ClockedRegister::CLOCK_OUT].getVoltage() == 10)
                sequences[run].push_back(word(r.outputs[ClockedRegister::BUS_OUT]));
            assert(fun.outputs[Vastish::LEFT_OUT].getVoltage() == 0);
            assert(fun.outputs[Vastish::RIGHT_OUT].getVoltage() == 0);
            ++a.frame;
        }
    }
    assert(sequences[0] == sequences[1] && sequences[0].size() == 12);
    for (size_t k = 0; k < sequences[0].size(); ++k)
        assert(sequences[0][k] == (k & 1 ? uint8_t(~seed) : seed));
    return sequences[0];
}

static void counterPorts() {
    using C = ByteCounter;
    C c; c.model = modelCounter; auto a = args(); connectOutputs(c);
    assert(c.params.size() == 7 && c.inputs.size() == 3 && c.outputs.size() == 3);
    assert(c.params[C::RUN_MODE].getValue() == 0); // Loop
    assert(c.params[C::INC].getValue() == 1 && c.params[C::MODULUS].getValue() == 256 && c.params[C::SEED].getValue() == 0);
    // CLOCK jack: INC 5, MOD 12. CARRY pulses on the wrapping edges, with the word clock.
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(5); c.params[C::MODULUS].setValue(12);
    c.inputs[C::CLOCK_IN].channels = 1;
    c.process(a); assert(c.outputs[C::CLOCK_OUT].getVoltage() == 10); // the initial word is published
    c.process(a);
    std::vector<int> words, carries;
    for (int k = 0; k < 6; ++k) {
        c.inputs[C::CLOCK_IN].setVoltage(10); c.process(a);
        assert(c.outputs[C::CLOCK_OUT].getVoltage() == 10 && c.outputs[C::BUS_OUT].getChannels() == 9);
        assert(c.outputs[C::BUS_OUT].getVoltage(8) == 10);
        words.push_back(word(c.outputs[C::BUS_OUT])); carries.push_back(c.outputs[C::CARRY_OUT].getVoltage() == 10);
        c.inputs[C::CLOCK_IN].setVoltage(0); c.process(a);
        assert(c.outputs[C::CARRY_OUT].getVoltage() == 0 && c.outputs[C::CLOCK_OUT].getVoltage() == 0);
    }
    assert((words == std::vector<int>{5, 10, 3, 8, 1, 6}) && (carries == std::vector<int>{0, 0, 1, 0, 1, 0}));
    for (int k = 0; k < 8; ++k) assert(c.lights[C::BIT0_LIGHT+k].getBrightness() == (6 & (1<<k) ? 1 : 0));
    // A patched BUS IN replaces INC.
    putBus(c.inputs[C::BUS_IN], 2); c.inputs[C::CLOCK_IN].setVoltage(10); c.process(a);
    assert(word(c.outputs[C::BUS_OUT]) == 8);
    // Controls and the held word survive Rack JSON; reload publishes the saved word.
    c.params[C::SEED].setValue(200);
    json_t* saved = c.toJson(); C r; r.model = modelCounter; connectOutputs(r); r.fromJson(saved); json_decref(saved);
    assert(r.params[C::INC].getValue() == 5 && r.params[C::MODULUS].getValue() == 12 && r.params[C::SEED].getValue() == 200);
    r.process(a); assert(word(r.outputs[C::BUS_OUT]) == 8 && r.outputs[C::CLOCK_OUT].getVoltage() == 10);
    // LOAD: a seed above MOD is published as it is, after the mandatory low sample.
    r.params[C::LOAD_BUTTON].setValue(1); r.process(a);
    assert(word(r.outputs[C::BUS_OUT]) == 200 && r.outputs[C::CLOCK_OUT].getVoltage() == 0);
    r.process(a); assert(r.outputs[C::CLOCK_OUT].getVoltage() == 10);
    // Engine rate: eight channels, no clock, a new word every sample.
    r.params[C::CLOCK_MODE].setValue(4); r.params[C::INC].setValue(1); r.process(a);
    assert(r.outputs[C::BUS_OUT].getChannels() == 8 && r.outputs[C::CLOCK_OUT].getVoltage() == 0);
    uint8_t before = word(r.outputs[C::BUS_OUT]); r.process(a); assert(word(r.outputs[C::BUS_OUT]) == (before + 1) % 12);
    // Initialize restores default controls and word 0; invalid saved words are bounded.
    r.onReset(Module::ResetEvent{}); r.process(a);
    assert(r.params[C::MODULUS].getValue() == 256 && word(r.outputs[C::BUS_OUT]) == 0);
    json_t* bad = json_pack("{s:I}", "word", json_int_t(-5)); r.dataFromJson(bad); json_decref(bad);
    r.process(a); assert(word(r.outputs[C::BUS_OUT]) == 0);
}

// Run modes through the module: Pendulum's direction is saved with the patch, a 2.3 patch
// loads as Loop and up, Initialize restores both, and One-shot holds after one CARRY.
static void counterRunModes() {
    using C = ByteCounter;
    C c; c.model = modelCounter; auto a = args(); connectOutputs(c);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(3); c.params[C::MODULUS].setValue(8);
    c.params[C::RUN_MODE].setValue(2); c.inputs[C::CLOCK_IN].channels = 1;
    c.process(a); c.process(a);
    auto edges = [&](C& m, int n, std::vector<int>& words, std::vector<int>& carries) {
        for (int k = 0; k < n; ++k) {
            m.inputs[C::CLOCK_IN].setVoltage(10); m.process(a);
            words.push_back(word(m.outputs[C::BUS_OUT])); carries.push_back(m.outputs[C::CARRY_OUT].getVoltage() == 10);
            m.inputs[C::CLOCK_IN].setVoltage(0); m.process(a);
        }
    };
    std::vector<int> words, carries;
    edges(c, 4, words, carries);
    assert((words == std::vector<int>{3, 6, 7, 4}) && (carries == std::vector<int>{0, 0, 1, 0}));
    // Saved while descending: the reload carries on down.
    json_t* saved = c.toJson();
    assert(json_is_true(json_object_get(json_object_get(saved, "data"), "down")));
    C r; r.model = modelCounter; connectOutputs(r); r.fromJson(saved);
    r.inputs[C::CLOCK_IN].channels = 1; r.process(a); r.process(a);
    assert(word(r.outputs[C::BUS_OUT]) == 4 && r.params[C::RUN_MODE].getValue() == 2);
    words.clear(); carries.clear(); edges(r, 4, words, carries);
    assert((words == std::vector<int>{1, 0, 3, 6}) && (carries == std::vector<int>{0, 1, 0, 0}));
    // A ByteBusLab 2.3 patch has neither the MODE param nor the direction: Loop, counting up.
    json_t* params = json_object_get(saved, "params");
    for (size_t k = 0; k < json_array_size(params); ++k)
        if (json_integer_value(json_object_get(json_array_get(params, k), "id")) == C::RUN_MODE) json_array_remove(params, k--);
    json_object_del(json_object_get(saved, "data"), "down");
    C old; old.model = modelCounter; connectOutputs(old); old.fromJson(saved); json_decref(saved);
    old.inputs[C::CLOCK_IN].channels = 1; old.process(a); old.process(a);
    assert(old.params[C::RUN_MODE].getValue() == 0 && word(old.outputs[C::BUS_OUT]) == 4);
    words.clear(); carries.clear(); edges(old, 2, words, carries);
    assert((words == std::vector<int>{7, 2}) && (carries == std::vector<int>{0, 1}));
    old.params[C::RUN_MODE].setValue(2); words.clear(); carries.clear(); edges(old, 3, words, carries);
    assert((words == std::vector<int>{5, 7, 4}) && (carries == std::vector<int>{0, 1, 0})); // up from 2.3
    // Initialize: Loop, word 0, direction up.
    r.onReset(Module::ResetEvent{}); assert(r.params[C::RUN_MODE].getValue() == 0);
    r.process(a); assert(word(r.outputs[C::BUS_OUT]) == 0 && !(r.published.load() & 0x100));
    // One-shot: one CARRY on arrival at MOD - 1, then held; RESET restarts it.
    c.params[C::RUN_MODE].setValue(1); c.params[C::INC].setValue(5); c.params[C::MODULUS].setValue(16);
    c.inputs[C::RESET_IN].channels = 1; c.inputs[C::RESET_IN].setVoltage(10); c.process(a);
    c.inputs[C::RESET_IN].setVoltage(0); c.process(a); c.process(a);
    for (int run = 0; run < 2; ++run) {
        words.clear(); carries.clear(); edges(c, 6, words, carries);
        assert((words == std::vector<int>{5, 10, 15, 15, 15, 15}) && (carries == std::vector<int>{0, 0, 1, 0, 0, 0}));
        c.inputs[C::RESET_IN].setVoltage(10); c.process(a); assert(word(c.outputs[C::BUS_OUT]) == 0);
        c.inputs[C::RESET_IN].setVoltage(0); c.process(a); c.process(a);
    }
}

// Counter (One-shot) -> Lookup (Curve, SHAPE 32): Curve 32 is 255 - n, so the rising one-shot
// becomes a falling envelope that ends held at zero. Pendulum through it is an inverted triangle.
static void counterEnvelope(float sr) {
    namespace lu = dspemu::lookup; using C = ByteCounter;
    auto library = std::make_shared<lu::Library>(lu::Library::load("../res/lookup-v1.bin"));
    for (int run : {1, 2}) {
        C c; ByteLookup l(library); connectOutputs(c); connectOutputs(l); auto a = args(sr);
        c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(40); c.params[C::RUN_MODE].setValue(run);
        c.inputs[C::CLOCK_IN].channels = 1;
        l.params[ByteLookup::FAMILY].setValue(lu::Curve); l.params[ByteLookup::SHAPE].setValue(32);
        std::vector<int> counts, out;
        for (int t = 0; t < 8 * 16; ++t) {
            c.inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
            copy(c.outputs[C::BUS_OUT], l.inputs[ByteLookup::BUS_IN]);
            c.process(a); l.process(a);
            if (c.outputs[C::CLOCK_OUT].getVoltage() == 10) counts.push_back(word(c.outputs[C::BUS_OUT]));
            if (l.outputs[ByteLookup::BUS_OUT].getVoltage(8) == 10) out.push_back(word(l.outputs[ByteLookup::BUS_OUT]));
        }
        assert(out.size() == counts.size());
        for (size_t k = 0; k < out.size(); ++k) assert(out[k] == 255 - counts[k]);
        std::vector<int> want = run == 1 ? std::vector<int>{0, 40, 80, 120, 160, 200, 240, 255, 255}
                                         : std::vector<int>{0, 40, 80, 120, 160, 200, 240, 255, 215};
        assert(std::vector<int>(counts.begin(), counts.begin() + 9) == want);
        if (run == 1) assert(out.back() == 0);
    }
}

// Counter CARRY drives a second Counter's CLOCK (a 16-bit count) and a Register that captures
// the low counter's word, all through one-sample cables in random process order.
static std::vector<std::array<int, 3>> carryChain(float sr, unsigned orderSeed) {
    using C = ByteCounter; using R = ClockedRegister;
    C low, high; R reg; connectOutputs(low); connectOutputs(high); connectOutputs(reg); auto a = args(sr);
    low.params[C::CLOCK_MODE].setValue(1); low.params[C::INC].setValue(3); low.params[C::MODULUS].setValue(16);
    high.params[C::CLOCK_MODE].setValue(1); reg.params[R::CLOCK_MODE].setValue(1);
    low.inputs[C::CLOCK_IN].channels = high.inputs[C::CLOCK_IN].channels = reg.inputs[R::CLOCK_IN].channels = 1;
    std::mt19937 order(orderSeed); std::vector<std::array<int, 3>> captured;
    for (int t = 0; t < 32 * 48; ++t) {
        low.inputs[C::CLOCK_IN].setVoltage(t % 32 == 8 ? 10 : 0);
        copy(low.outputs[C::CARRY_OUT], high.inputs[C::CLOCK_IN]);
        copy(low.outputs[C::CARRY_OUT], reg.inputs[R::CLOCK_IN]);
        copy(low.outputs[C::BUS_OUT], reg.inputs[R::BUS_IN]);
        Module* modules[] = {&low, &high, &reg}; std::shuffle(std::begin(modules), std::end(modules), order);
        for (auto* m : modules) m->process(a);
        if (t > 2 && reg.outputs[R::CLOCK_OUT].getVoltage() == 10)
            captured.push_back({word(reg.outputs[R::BUS_OUT]), word(high.outputs[C::BUS_OUT]), t});
    }
    // Low counts 3, 6, ... mod 16 on each clock and carries on every wrap. Both followers hear the
    // carry one cable later; the Register takes the low word published on the wrapping edge.
    std::vector<std::array<int, 3>> expected; int w = 0, carries = 0;
    for (int k = 0; k < 48; ++k) {
        int t = 32 * k + 8; w += 3;
        if (w >= 16) { w -= 16; ++carries; expected.push_back({w, carries, t + 1}); }
    }
    assert(captured == expected && captured.size() == 9);
    return captured;
}

// Counter -> Lookup (Rhythm) -> Split: the gates equal the Rhythm table read at every count,
// as the portable Lookup engine evaluates it directly, with a low half-step between steps.
static std::vector<uint8_t> rhythmChain(std::shared_ptr<const dspemu::lookup::Library> library, float sr, unsigned orderSeed) {
    namespace lu = dspemu::lookup; using C = ByteCounter;
    C c; ByteLookup l(library); connectOutputs(c); connectOutputs(l); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(1); c.inputs[C::CLOCK_IN].channels = 1;
    l.params[ByteLookup::FAMILY].setValue(lu::Rhythm); l.params[ByteLookup::SEED].setValue(0x5a);
    l.params[ByteLookup::SHAPE].setValue(3);
    std::mt19937 order(orderSeed); std::vector<uint8_t> gates, counts;
    for (int t = 0; t < 8 * 520; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], l.inputs[ByteLookup::BUS_IN]);
        if (order() & 1) { c.process(a); l.process(a); } else { l.process(a); c.process(a); }
        if (l.outputs[ByteLookup::BUS_OUT].getVoltage(8) == 10) gates.push_back(word(l.outputs[ByteLookup::BUS_OUT]));
        if (c.outputs[C::CLOCK_OUT].getVoltage() == 10) counts.push_back(word(c.outputs[C::BUS_OUT]));
    }
    // The Lookup publishes each count one cable after the Counter does.
    lu::Engine direct; lu::Parameters p; p.family = lu::Rhythm; p.seed = 0x5a; p.shape = 3;
    std::vector<uint8_t> expected;
    for (size_t k = 0; k < gates.size(); ++k) {
        dspemu::bus::Sample s; s.connected = s.clocked = s.rising = s.clk = s.updated = true; s.rawWord = s.word = counts[k];
        expected.push_back(direct.tick(*library, p, s, sr).word);
    }
    assert(gates == expected && gates.size() >= 515);
    int sounding = 0;
    for (size_t k = 0; k < gates.size(); ++k) {
        if (counts[k] % 8 >= 4) assert(gates[k] == 0);
        sounding += gates[k] != 0;
    }
    assert(sounding > 32);
    return gates;
}

// Counter -> Lookup BUS IN and JUMP (BIT 3), through one-sample cables in random order: the
// address and the JUMP bit arrive on the same publication, so each count with bit 3 high steps
// SHAPE before that count is evaluated. The Counter's seed publication only arms JUMP.
static std::vector<uint8_t> jumpChain(std::shared_ptr<const dspemu::lookup::Library> library, float sr, unsigned orderSeed) {
    namespace lu = dspemu::lookup; using C = ByteCounter; using L = ByteLookup;
    C c; L l(library); connectOutputs(c); connectOutputs(l); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::MODULUS].setValue(16); c.inputs[C::CLOCK_IN].channels = 1;
    l.params[L::FAMILY].setValue(lu::Walsh); l.params[L::SHAPE].setValue(9); l.params[L::JUMP_BIT].setValue(3);
    std::mt19937 order(orderSeed); std::vector<uint8_t> out, counts;
    for (int t = 0; t < 8 * 100; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], l.inputs[L::BUS_IN]); copy(c.outputs[C::BUS_OUT], l.inputs[L::JUMP_IN]);
        if (order() & 1) { c.process(a); l.process(a); } else { l.process(a); c.process(a); }
        if (l.outputs[L::BUS_OUT].getVoltage(8) == 10) out.push_back(word(l.outputs[L::BUS_OUT]));
        if (c.outputs[C::CLOCK_OUT].getVoltage() == 10) counts.push_back(word(c.outputs[C::BUS_OUT]));
    }
    assert(out.size() >= 98 && counts.size() >= out.size());
    int steps = 0;
    for (size_t k = 0; k < out.size(); ++k) {
        if (k > 0 && (counts[k] & 8)) ++steps;
        assert(out[k] == library->table(lu::Walsh, (9 + steps) & 63)[counts[k]]);
    }
    assert(steps > 40 && l.jumpSteps.load() == (steps & 63));
    return out;
}

// Switch: real ports and defaults (A passes through), parameters through Rack JSON.
static void switchPorts() {
    using S = ByteSwitch;
    S s; s.model = modelSwitch; auto a = args(); connectOutputs(s);
    assert(s.params.size() == 6 && s.inputs.size() == 4 && s.outputs.size() == 2 && s.lights.size() == 9);
    for (int k = 0; k < 256; k += 3) {
        putBus(s.inputs[S::A_IN], uint8_t(k)); s.process(a);
        assert(word(s.outputs[S::BUS_OUT]) == k && s.outputs[S::BUS_OUT].getChannels() == 8);
    }
    putBus(s.inputs[S::A_IN], 0x5a, true, true); s.process(a);
    assert(word(s.outputs[S::BUS_OUT]) == 0x5a && s.outputs[S::BUS_OUT].getChannels() == 9 && s.outputs[S::CLOCK_OUT].getVoltage() == 10);
    // A gate into C is bit 0: Select with BIT 0 passes B while it is high.
    s.params[S::B_WORD].setValue(0xc3); s.inputs[S::C_IN].channels = 1; s.inputs[S::C_IN].setVoltage(10);
    putBus(s.inputs[S::A_IN], 0x5a); s.process(a); assert(word(s.outputs[S::BUS_OUT]) == 0xc3);
    s.inputs[S::C_IN].setVoltage(0); s.process(a); assert(word(s.outputs[S::BUS_OUT]) == 0x5a);
    for (int k = 0; k < 8; ++k) assert(s.lights[S::BIT0_LIGHT + k].getBrightness() == (0x5a >> k & 1));
    s.params[S::MODE].setValue(2); s.params[S::SOURCE].setValue(3); s.params[S::BIT].setValue(5);
    json_t* saved = s.toJson(); S r; r.model = modelSwitch; r.fromJson(saved); json_decref(saved);
    assert(r.params[S::MODE].getValue() == 2 && r.params[S::SOURCE].getValue() == 3 && r.params[S::BIT].getValue() == 5);
    assert(r.params[S::B_WORD].getValue() == 0xc3);
    r.onReset(Module::ResetEvent{}); assert(r.params[S::MODE].getValue() == 0 && r.params[S::B_WORD].getValue() == 0);
}

// Two Counters on one clock into A and B, A's count also into C with SEL BIT 3: the output is A's
// count for counts 0-7 and B's for 8-15, on A's clock, through one-sample cables in random order.
// Then Mix toward a B WORD of 00 is an 8-bit VCA on A, with C WORD as the gain.
static std::vector<int> commutator(float sr, unsigned orderSeed) {
    using C = ByteCounter; using S = ByteSwitch;
    C ca, cb; S s; connectOutputs(ca); connectOutputs(cb); connectOutputs(s); auto a = args(sr);
    for (C* c : {&ca, &cb}) { c->params[C::CLOCK_MODE].setValue(1); c->params[C::MODULUS].setValue(16); c->inputs[C::CLOCK_IN].channels = 1; }
    cb.params[C::INC].setValue(5); s.params[S::BIT].setValue(3);
    std::mt19937 order(orderSeed); std::vector<int> out; std::vector<std::array<int, 2>> counts;
    for (int t = 0; t < 8 * 64; ++t) {
        for (C* c : {&ca, &cb}) c->inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
        copy(ca.outputs[C::BUS_OUT], s.inputs[S::A_IN]); copy(cb.outputs[C::BUS_OUT], s.inputs[S::B_IN]);
        copy(ca.outputs[C::BUS_OUT], s.inputs[S::C_IN]);
        Module* modules[] = {&ca, &cb, &s}; std::shuffle(std::begin(modules), std::end(modules), order);
        for (auto* m : modules) m->process(a);
        if (s.outputs[S::CLOCK_OUT].getVoltage() == 10 && s.outputs[S::BUS_OUT].getChannels() == 9) out.push_back(word(s.outputs[S::BUS_OUT]));
        if (ca.outputs[C::CLOCK_OUT].getVoltage() == 10) counts.push_back({word(ca.outputs[C::BUS_OUT]), word(cb.outputs[C::BUS_OUT])});
    }
    assert(out.size() >= 62);
    for (size_t k = 0; k < out.size(); ++k) assert(out[k] == (counts[k][0] & 8 ? counts[k][1] : counts[k][0]));
    // Mix: B unpatched at 00, C unpatched at 64: out = round(a * 191 / 255).
    s.inputs[S::B_IN].channels = s.inputs[S::C_IN].channels = 0; s.params[S::MODE].setValue(2); s.params[S::C_WORD].setValue(64);
    ca.params[C::MODULUS].setValue(256); ca.params[C::INC].setValue(37);
    for (int t = 0; t < 8 * 64; ++t) {
        ca.inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
        copy(ca.outputs[C::BUS_OUT], s.inputs[S::A_IN]);
        if (order() & 1) { ca.process(a); s.process(a); } else { s.process(a); ca.process(a); }
        if (s.outputs[S::CLOCK_OUT].getVoltage() == 10) {
            int w = inputWord(s.inputs[S::A_IN]); out.push_back(word(s.outputs[S::BUS_OUT]));
            assert(out.back() == (w * 191 + 127) / 255);
        }
    }
    return out;
}

// Reclocking: a Counter on its internal clock into A; Switch on its own CLOCK jack at another rate
// publishes the raw word that arrived on each of its edges, with one-sample strobes.
static void reclock(float sr) {
    using C = ByteCounter; using S = ByteSwitch;
    C c; S s; connectOutputs(c); connectOutputs(s); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(3); c.params[C::RATE].setValue(std::log2(1000.f)); c.params[C::INC].setValue(1);
    s.params[S::SOURCE].setValue(1); s.inputs[S::CLOCK_IN].channels = 1;
    int strobes = 0; bool last = false;
    for (int t = 0; t < int(sr / 4); ++t) {
        s.inputs[S::CLOCK_IN].setVoltage(t % 997 < 3 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], s.inputs[S::A_IN]);
        c.process(a); s.process(a);
        bool clk = s.outputs[S::CLOCK_OUT].getVoltage() == 10;
        assert(!(clk && last) && s.outputs[S::BUS_OUT].getChannels() == 9); last = clk;
        if (clk && t > 0) { ++strobes; assert(word(s.outputs[S::BUS_OUT]) == inputWord(s.inputs[S::A_IN])); }
    }
    assert(strobes >= int(sr / 4 / 997) - 1);
}

// Library v2 in patches. Counter (One-shot, RESET from a gate) -> Lookup Envelope is an envelope
// generator: each gate restarts the table from address 00 and it ends held at its last byte.
// Counter -> Lookup Density with the Counter's CARRY into JUMP is a build-up: one hit more per bar.
static void libraryV2Chains(float sr) {
    namespace lu = dspemu::lookup; using C = ByteCounter; using L = ByteLookup;
    auto library = std::make_shared<lu::Library>(lu::Library::load("../res/lookup-v1.bin"));
    library->loadV2("../res/lookup-v2.bin");
    {
        C c; L l(library); connectOutputs(c); connectOutputs(l); auto a = args(sr);
        c.params[C::CLOCK_MODE].setValue(1); c.params[C::RUN_MODE].setValue(1); c.params[C::INC].setValue(24);
        c.inputs[C::CLOCK_IN].channels = c.inputs[C::RESET_IN].channels = 1;
        l.params[L::FAMILY].setValue(lu::Envelope); l.params[L::SHAPE].setValue(3);
        const uint8_t* env = library->table(lu::Envelope, 3);
        std::vector<int> out; int resets = 0;
        for (int t = 0; t < 8 * 64; ++t) {
            c.inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
            c.inputs[C::RESET_IN].setVoltage(t % 160 == 0 ? 10 : 0);
            copy(c.outputs[C::BUS_OUT], l.inputs[L::BUS_IN]);
            c.process(a); l.process(a);
            if (l.outputs[L::BUS_OUT].getVoltage(8) == 10) {
                int count = inputWord(l.inputs[L::BUS_IN]);
                assert(word(l.outputs[L::BUS_OUT]) == env[count]);
                out.push_back(word(l.outputs[L::BUS_OUT])); resets += count == 0;
            }
        }
        assert(resets >= 3 && env[0] == 255 && env[255] == 0);
        assert(std::count(out.begin(), out.end(), 0) > 10);                 // each run ends held at 00
    }
    {
        C c; L l(library); connectOutputs(c); connectOutputs(l); auto a = args(sr);
        c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(8); c.inputs[C::CLOCK_IN].channels = 1;
        l.params[L::FAMILY].setValue(lu::Density);
        std::vector<int> hits(12, 0); int bar = 0;
        for (int t = 0; t < 4 * 32 * 12 + 2; ++t) {
            c.inputs[C::CLOCK_IN].setVoltage(t % 4 == 2 ? 10 : 0);
            copy(c.outputs[C::BUS_OUT], l.inputs[L::BUS_IN]); copy(c.outputs[C::CARRY_OUT], l.inputs[L::JUMP_IN]);
            c.process(a); l.process(a);
            if (l.outputs[L::BUS_OUT].getVoltage(8) == 10) {
                int count = inputWord(l.inputs[L::BUS_IN]);
                if (count == 0 && t > 4) ++bar;
                if (bar < 12) for (int k = 0; k < 8; ++k) hits[bar] += word(l.outputs[L::BUS_OUT]) >> k & 1;
            }
        }
        for (int k = 0; k < 12; ++k) assert(hits[k] == 4 + k);
        assert(l.jumpSteps.load() == 12);
    }
}

// Pipeline: a Counter -> Lookup Rhythm pattern word and its copy DELAY publications later (the
// canon of the kit), through one-sample cables in random order; SPREAD per lane; LOOP from the
// gate and the button; a looped phrase that survives save and reload; CLEAR and Initialize.
static std::vector<int> pipelineCanon(std::shared_ptr<const dspemu::lookup::Library> library, float sr, unsigned orderSeed) {
    namespace lu = dspemu::lookup; using C = ByteCounter; using L = ByteLookup; using P = BytePipeline;
    using S = ByteSwitch;
    C c; L l(library); P pl; S sw; connectOutputs(c); connectOutputs(l); connectOutputs(pl); connectOutputs(sw); auto a = args(sr);
    sw.params[S::MODE].setValue(1); sw.params[S::C_WORD].setValue(0x0f);
    assert(pl.params.size() == 6 && pl.inputs.size() == 4 && pl.outputs.size() == 2 && pl.lights.size() == 11);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(8); c.inputs[C::CLOCK_IN].channels = 1;
    l.params[L::FAMILY].setValue(lu::Rhythm); l.params[L::SHAPE].setValue(13);
    pl.params[P::DELAY].setValue(6); pl.params[P::SPREAD].setValue(1);
    std::mt19937 order(orderSeed); std::vector<int> pattern, delayed, canon;
    for (int t = 0; t < 6 * 200; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 6 == 3 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], l.inputs[L::BUS_IN]); copy(l.outputs[L::BUS_OUT], pl.inputs[P::BUS_IN]);
        copy(pl.outputs[P::BUS_OUT], sw.inputs[S::A_IN]); copy(l.outputs[L::BUS_OUT], sw.inputs[S::B_IN]);
        Module* modules[] = {&c, &l, &pl, &sw}; std::shuffle(std::begin(modules), std::end(modules), order);
        for (auto* m : modules) m->process(a);
        if (l.outputs[L::BUS_OUT].getVoltage(8) == 10) pattern.push_back(word(l.outputs[L::BUS_OUT]));
        if (pl.outputs[P::CLOCK_OUT].getVoltage() == 10) delayed.push_back(word(pl.outputs[P::BUS_OUT]));
        if (sw.outputs[S::CLOCK_OUT].getVoltage() == 10) canon.push_back(word(sw.outputs[S::BUS_OUT]));
    }
    // The First patch's Switch: lanes 0-3 from the pattern (B), 4-7 from its echo (A), on A's clock.
    assert(canon.size() + 1 == delayed.size());                     // the last publication is still in the cable
    for (size_t k = 1; k < canon.size(); ++k) assert(canon[k] == ((pattern[k - 1] & 0x0f) | (delayed[k] & 0xf0)));
    // Pipeline follows the Lookup's clock (Auto -> bus CLK); its first publication is the 00 history.
    assert(delayed.size() >= pattern.size() - 1 && pattern.size() > 190);
    for (size_t k = 1; k < delayed.size(); ++k) {
        int want = 0;
        for (int lane = 0; lane < 8; ++lane) { int i = int(k) - 1 - 6 - lane; if (i >= 0 && pattern[i] >> lane & 1) want |= 1 << lane; }
        assert(delayed[k] == want);
    }
    return delayed;
}
static void pipelineLoop() {
    using P = BytePipeline;
    P pl; connectOutputs(pl); auto a = args(); pl.model = modelPipeline;
    pl.params[P::CLOCK_MODE].setValue(4); pl.params[P::DELAY].setValue(5);   // Engine rate: every sample
    std::vector<int> out;
    for (int t = 0; t < 40; ++t) { putBus(pl.inputs[P::BUS_IN], uint8_t(t * 3)); pl.process(a); }
    // The LOOP gate closes it; the phrase saved mid-loop restores into a fresh module and goes on alike.
    pl.inputs[P::LOOP_IN].channels = 1; pl.inputs[P::LOOP_IN].setVoltage(10);
    for (int t = 0; t < 12; ++t) { putBus(pl.inputs[P::BUS_IN], 0xee); pl.process(a); out.push_back(word(pl.outputs[P::BUS_OUT])); }
    assert(pl.lights[P::LOOP_LIGHT].getBrightness() == 1);
    for (int t = 5; t < 12; ++t) assert(out[t] == out[t - 5] && out[t] != 0xee);
    json_t* saved = pl.toJson(); assert(json_array_size(json_object_get(json_object_get(saved, "data"), "history")) == 128);
    P r; connectOutputs(r); r.model = modelPipeline; r.fromJson(saved); json_decref(saved);
    r.params[P::LOOP_BUTTON].setValue(1); r.inputs[P::LOOP_IN].channels = 0;               // the button loops too
    r.process(a);                                                                          // publishes the restored word
    assert(r.lights[P::LOOP_LIGHT].getBrightness() == 1);
    for (int t = 0; t < 15; ++t) {
        putBus(pl.inputs[P::BUS_IN], 0xee); putBus(r.inputs[P::BUS_IN], 0xee);
        pl.process(a); r.process(a); assert(word(r.outputs[P::BUS_OUT]) == word(pl.outputs[P::BUS_OUT]));
    }
    // A history list shorter than 128 fills the latest stages; values are bounded.
    json_t* short_ = json_pack("{s:[i,i,i]}", "history", 7, 300, -2); r.dataFromJson(short_); json_decref(short_);
    r.params[P::LOOP_BUTTON].setValue(0); r.params[P::DELAY].setValue(1); r.process(a);
    assert(word(r.outputs[P::BUS_OUT]) == 255);       // latest stage -2 -> 00, the one before 300 -> FF: DELAY 1 reads FF
    // CLEAR empties the history; Initialize restores the controls and an empty history.
    pl.params[P::CLEAR_BUTTON].setValue(1); pl.process(a); assert(word(pl.outputs[P::BUS_OUT]) == 0);
    pl.params[P::CLEAR_BUTTON].setValue(0); pl.inputs[P::LOOP_IN].setVoltage(0);
    for (int t = 0; t < 4; ++t) { putBus(pl.inputs[P::BUS_IN], 9); pl.process(a); assert(word(pl.outputs[P::BUS_OUT]) == 0); }
    pl.onReset(Module::ResetEvent{}); assert(pl.params[P::DELAY].getValue() == 16);
    pl.process(a); assert(word(pl.outputs[P::BUS_OUT]) == 0);
}

// Two Counters into Lookup2 (Logic, XOR) and a third shape stepped by JUMP from the first Counter's
// CARRY, through one-sample cables in random order: each result is f(A, B) of the words published on
// A's edge, with B read raw on that sample.
static std::vector<int> lookup2Chain(float sr, unsigned orderSeed) {
    namespace l2 = dspemu::lookup2; using C = ByteCounter; using M = ByteLookup2;
    C ca, cb; M m; connectOutputs(ca); connectOutputs(cb); connectOutputs(m); auto a = args(sr);
    for (C* c : {&ca, &cb}) { c->params[C::CLOCK_MODE].setValue(1); c->inputs[C::CLOCK_IN].channels = 1; }
    ca.params[C::INC].setValue(3); ca.params[C::MODULUS].setValue(32); cb.params[C::INC].setValue(11);
    m.params[M::SHAPE].setValue(3);
    std::mt19937 order(orderSeed); std::vector<int> out; std::vector<std::array<int, 3>> seen;
    for (int t = 0; t < 8 * 120; ++t) {
        for (C* c : {&ca, &cb}) c->inputs[C::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
        copy(ca.outputs[C::BUS_OUT], m.inputs[M::A_IN]); copy(cb.outputs[C::BUS_OUT], m.inputs[M::B_IN]);
        copy(ca.outputs[C::CARRY_OUT], m.inputs[M::JUMP_IN]);
        Module* modules[] = {&ca, &cb, &m}; std::shuffle(std::begin(modules), std::end(modules), order);
        for (auto* mod : modules) mod->process(a);
        if (m.outputs[M::BUS_OUT].getVoltage(8) == 10) {
            out.push_back(word(m.outputs[M::BUS_OUT]));
            seen.push_back({inputWord(m.inputs[M::A_IN]), inputWord(m.inputs[M::B_IN]), m.jumpSteps.load()});
        }
    }
    assert(out.size() >= 118);
    for (size_t k = 0; k < out.size(); ++k) assert(out[k] == l2::logic(3 + seen[k][2], uint8_t(seen[k][0]), uint8_t(seen[k][1])));
    assert(seen.back()[2] > 5);
    return out;
}

// Counter 2: real ports; a 12-edge and a 16-edge cycle on one clock carry 4 and 3 times per 48
// edges and meet at 00; a save mid-cycle resumes exactly; Initialize.
static void counter2Module(float sr) {
    using K = ByteCounter2;
    K a, b; connectOutputs(a); connectOutputs(b); a.model = b.model = modelCounter2; auto args_ = args(sr);
    assert(a.params.size() == 6 && a.inputs.size() == 3 && a.outputs.size() == 3 && a.lights.size() == 11);
    for (K* k : {&a, &b}) { k->params[K::CLOCK_MODE].setValue(1); k->inputs[K::CLOCK_IN].channels = 1; }
    a.params[K::D_PARAM].setValue(12); b.params[K::D_PARAM].setValue(16);
    int carriesA = 0, carriesB = 0; std::vector<int> wordsA;
    auto run = [&](K& m, int t) { m.inputs[K::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0); m.process(args_); };
    for (int t = 0; t < 8 * 48; ++t) {                                  // 48 edges
        run(a, t); run(b, t);
        carriesA += a.outputs[K::CARRY_OUT].getVoltage() == 10; carriesB += b.outputs[K::CARRY_OUT].getVoltage() == 10;
        if (a.outputs[K::CLOCK_OUT].getVoltage() == 10) wordsA.push_back(word(a.outputs[K::BUS_OUT]));
    }
    assert(carriesA == 4 && carriesB == 3 && word(a.outputs[K::BUS_OUT]) == 0 && word(b.outputs[K::BUS_OUT]) == 0);
    for (size_t k = 1; k < wordsA.size(); ++k) assert(wordsA[k] == int((k % 12) * 256 / 12));
    // Saved after 5 more edges of N 5 / D 7, restored, and run on: the same words as the original.
    a.params[K::N_PARAM].setValue(5); a.params[K::D_PARAM].setValue(7);
    for (int t = 0; t < 8 * 5; ++t) run(a, t);
    json_t* saved = a.toJson(); K r; connectOutputs(r); r.model = modelCounter2; r.fromJson(saved); json_decref(saved);
    r.inputs[K::CLOCK_IN].channels = 1; r.process(args_); assert(word(r.outputs[K::BUS_OUT]) == word(a.outputs[K::BUS_OUT]));
    for (int t = 1; t < 8 * 40; ++t) { run(a, t); run(r, t); assert(word(r.outputs[K::BUS_OUT]) == word(a.outputs[K::BUS_OUT])); }
    assert(r.ratio.load() == (5 << 8 | 7));
    json_t* bad = json_pack("{s:i,s:i}", "accumulator", 999999, "denominator", -3); r.dataFromJson(bad); json_decref(bad);
    assert(r.state.d == 1 && r.state.acc == 255);
    r.onReset(Module::ResetEvent{}); assert(r.params[K::D_PARAM].getValue() == 16 && r.params[K::N_PARAM].getValue() == 1);
    r.process(args_); assert(word(r.outputs[K::BUS_OUT]) == 0);
}

// Counter 2's RATIO input and Lookup's Ratio family (library v3): a ratio word replaces N and D, and
// a control byte through Lookup Ratio picks a ratio from a grid, through one-sample cables in random
// order: the Counter then carries n times every d edges.
static void ratioChain(float sr, unsigned orderSeed) {
    namespace lu = dspemu::lookup; using K = ByteCounter2; using L = ByteLookup;
    auto library = std::make_shared<lu::Library>(lu::Library::load("../res/lookup-v1.bin"));
    library->loadV2("../res/lookup-v2.bin"); library->loadV3("../res/lookup-v3.bin");
    {   // A ratio word straight in: 3/8.
        K k; connectOutputs(k); k.model = modelCounter2; auto a = args(sr);
        k.params[K::CLOCK_MODE].setValue(3); putBus(k.inputs[K::RATIO_IN], uint8_t(2 << 4 | 7)); k.process(a);
        assert(k.ratio.load() == (3 << 8 | 8) && k.state.d == 8);
        k.inputs[K::RATIO_IN].channels = 0; k.process(a); assert(k.ratio.load() == (1 << 8 | 16));   // unpatched: the knobs
    }
    K k; L l(library); connectOutputs(k); connectOutputs(l); auto a = args(sr);
    k.params[K::CLOCK_MODE].setValue(1); k.inputs[K::CLOCK_IN].channels = 1;
    l.params[L::FAMILY].setValue(lu::Ratio); l.params[L::SHAPE].setValue(3 * 8 + 7);   // Harmonics, whole grid
    std::mt19937 order(orderSeed);
    for (int address : {0, 60, 128, 200, 255}) {
        uint8_t w = library->table(lu::Ratio, 3 * 8 + 7)[address];
        int n = lu::ratioN(w), d = lu::ratioD(w);
        putBus(l.inputs[L::BUS_IN], uint8_t(address));
        // Settle the ratio, start from 00, then count 4d edges.
        for (int t = 0; t < 4; ++t) { copy(l.outputs[L::BUS_OUT], k.inputs[K::RATIO_IN]); l.process(a); k.process(a); }
        k.inputs[K::RESET_IN].channels = 1; k.inputs[K::RESET_IN].setVoltage(10); k.process(a); k.inputs[K::RESET_IN].setVoltage(0); k.process(a);
        int carries = 0;
        for (int t = 0; t < 8 * 4 * d; ++t) {
            k.inputs[K::CLOCK_IN].setVoltage(t % 8 == 4 ? 10 : 0);
            copy(l.outputs[L::BUS_OUT], k.inputs[K::RATIO_IN]);
            if (order() & 1) { l.process(a); k.process(a); } else { k.process(a); l.process(a); }
            carries += k.outputs[K::CARRY_OUT].getVoltage() == 10;
        }
        // CARRY pulses once on each edge where the cycle count rises: n times in d edges, or every edge
        // when n > d (COUNTER2.md, Counting).
        assert(k.ratio.load() == (n << 8 | d) && carries == 4 * std::min(n, d));
    }
}

// Fan with real ports: polyphonic cables both ways, the one-hot counter mode, Hold, GLITCH,
// persistence; then Counter -> Fan BUS IN through one-sample cables in random module order.
static void fanPorts() {
    using F = ByteFan;
    F f; f.model = modelFan; auto a = args(); connectOutputs(f);
    assert(f.params.size() == 8 && f.inputs.size() == 7 && f.outputs.size() == 6 && f.lights.size() == 9);
    f.params[F::SOURCE].setValue(3);                                        // Engine: one step a sample
    f.inputs[F::IN_A].channels = 16; f.inputs[F::IN_B].channels = 5;        // IN 22-32 missing: 0 V
    for (int ch = 0; ch < 16; ++ch) f.inputs[F::IN_A].setVoltage(ch * .5f, ch);
    for (int ch = 0; ch < 5; ++ch) f.inputs[F::IN_B].setVoltage(-1.f - ch, ch);
    f.process(a);
    for (int t = 1; t <= 64; ++t) {
        f.process(a); int addr = t % 32;
        assert(f.outputs[F::OUT_A].getChannels() == 16 && f.outputs[F::OUT_B].getChannels() == 16);
        assert(f.outputs[F::BUS_OUT].getChannels() == 8 && word(f.outputs[F::BUS_OUT]) == addr);
        for (int ch = 0; ch < 32; ++ch) {
            Output& o = f.outputs[ch < 16 ? F::OUT_A : F::OUT_B];
            assert(o.getVoltage(ch & 15) == (ch == addr ? 10.f : 0.f));         // COM IN unpatched: 10 V
        }
        float expect = addr < 16 ? addr * .5f : addr < 21 ? -1.f - (addr - 16) : 0.f;
        assert(f.outputs[F::COM_OUT].getVoltage() == expect);
        assert(f.outputs[F::EOC_OUT].getVoltage() == (addr == 0 ? 10.f : 0.f));
        for (int k = 0; k < 5; ++k) assert(f.lights[F::BIT0_LIGHT + k].getBrightness() == (addr >> k & 1));
    }
    // Hold keeps each output's last value; a DIR gate counts down.
    f.params[F::UNSELECTED].setValue(1); f.inputs[F::COM_IN].channels = 1;
    for (int t = 0; t < 32; ++t) { f.inputs[F::COM_IN].setVoltage(float(t)); f.process(a); }
    f.inputs[F::DIR_IN].channels = 1; f.inputs[F::DIR_IN].setVoltage(10);
    int before = word(f.outputs[F::BUS_OUT]); f.process(a);
    assert(word(f.outputs[F::BUS_OUT]) == (before + 31) % 32);
    int held = 0;
    for (int ch = 0; ch < 32; ++ch) if (f.outputs[ch < 16 ? F::OUT_A : F::OUT_B].getVoltage(ch & 15) != 0) ++held;
    assert(held >= 30);
    // Persistence: params and the internal count; Initialize restores the defaults.
    f.params[F::SOURCE].setValue(1); f.params[F::LENGTH].setValue(12); f.params[F::GLITCH].setValue(1);
    f.inputs[F::CLOCK_IN].channels = 1; f.process(a);
    int saved = word(f.outputs[F::BUS_OUT]);
    json_t* j = f.toJson(); F r; r.model = modelFan; connectOutputs(r); r.fromJson(j); json_decref(j);
    assert(r.params[F::LENGTH].getValue() == 12 && r.params[F::GLITCH].getValue() == 1 && r.params[F::SOURCE].getValue() == 1);
    r.inputs[F::CLOCK_IN].channels = 1; r.process(a); assert(word(r.outputs[F::BUS_OUT]) == saved);
    assert(r.outputs[F::BUS_OUT].getChannels() == 9 && r.outputs[F::CLOCK_OUT].getVoltage() == 10);
    json_t* bad = json_pack("{s:I}", "count", json_int_t(1000)); r.dataFromJson(bad); json_decref(bad);
    r.process(a); r.process(a); assert(word(r.outputs[F::BUS_OUT]) == 31);
    r.onReset(Module::ResetEvent{});
    assert(r.params[F::LENGTH].getValue() == 32 && r.params[F::GLITCH].getValue() == 0 && r.params[F::UNSELECTED].getValue() == 0);
    // GLITCH through the panel: counting up from 15 at Engine rate, out 1 joins out 17 for one sample.
    F g; g.model = modelFan; connectOutputs(g);
    g.params[F::SOURCE].setValue(3); g.params[F::GLITCH].setValue(1); g.params[F::SEED].setValue(15);
    g.params[F::LOAD_BUTTON].setValue(1); g.process(a); g.params[F::LOAD_BUTTON].setValue(0);
    assert(word(g.outputs[F::BUS_OUT]) == 15);
    g.process(a);
    assert(word(g.outputs[F::BUS_OUT]) == 16 && g.outputs[F::OUT_A].getVoltage(0) == 10 && g.outputs[F::OUT_B].getVoltage(0) == 10);
    g.process(a); assert(g.outputs[F::OUT_A].getVoltage(0) == 0 && g.outputs[F::OUT_B].getVoltage(1) == 10);
}

// A Counter (MOD 32, INC 3) on a clock into Fan's BUS IN: Fan follows the count on the Counter's
// clock, and its one-hot gates and BUS OUT match the word that arrived, whatever the module order.
static std::vector<int> fanChain(float sr, unsigned orderSeed) {
    using C = ByteCounter; using F = ByteFan;
    C c; F f; connectOutputs(c); connectOutputs(f); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::MODULUS].setValue(32); c.params[C::INC].setValue(3);
    c.inputs[C::CLOCK_IN].channels = 1;
    std::mt19937 order(orderSeed); std::vector<int> steps;
    for (int t = 0; t < 32 * 40; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 32 == 7 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], f.inputs[F::BUS_IN]);
        if (order() & 1) { c.process(a); f.process(a); } else { f.process(a); c.process(a); }
        if (f.outputs[F::CLOCK_OUT].getVoltage() == 10) {
            int addr = word(f.outputs[F::BUS_OUT]);
            assert(addr == (inputWord(f.inputs[F::BUS_IN]) & 31) && f.outputs[F::BUS_OUT].getChannels() == 9);
            for (int ch = 0; ch < 32; ++ch) assert(f.outputs[ch < 16 ? F::OUT_A : F::OUT_B].getVoltage(ch & 15) == (ch == addr ? 10.f : 0.f));
            steps.push_back(addr);
        }
    }
    assert(steps.size() >= 39);
    for (size_t k = 1; k < steps.size(); ++k) assert(steps[k] == (steps[k-1] + 3) % 32);
    return steps;
}

// Lattice with real ports: the normalled 16-step raster at Engine rate, CV ranges, inverted CV,
// one-hot gates and lights; jumps through the panel in Follow and Load; BUS IN jumps; slew and
// slew CV; persistence with bounded restore; Initialize.
static void latticePorts() {
    using L = ByteLattice; namespace lt = dspemu::lattice;
    auto a = args();
    // LOAD with a seed, then the button's low sample.
    auto press = [&](L& m, int seed) {
        m.params[L::SEED].setValue(seed); m.params[L::LOAD_BUTTON].setValue(1); m.process(a);
        m.params[L::LOAD_BUTTON].setValue(0); m.process(a);
    };
    L l; l.model = modelLattice; connectOutputs(l);
    assert(l.params.size() == 24 && l.inputs.size() == 10 && l.outputs.size() == 8 && l.lights.size() == 18);
    for (int k = 0; k < 16; ++k) l.params[L::STEP + k].setValue(k / 15.f);
    l.params[L::SOURCE].setValue(3);                                        // Engine: H counts every sample
    l.process(a);
    for (int t = 1; t <= 64; ++t) {
        l.process(a);
        int step = t & 15, state = (t >> 2 & 15) << 4 | (t & 15);
        assert(l.outputs[L::BUS_OUT].getChannels() == 8 && word(l.outputs[L::BUS_OUT]) == state);
        assert(l.published.load() == unsigned(state));
        float cv = 10 * (step / 15.f);
        assert(l.outputs[L::CV_OUT].getVoltage() == cv && l.outputs[L::INV_OUT].getVoltage() == -cv);
        assert(l.outputs[L::STEPS_A_OUT].getChannels() == 8 && l.outputs[L::STEPS_B_OUT].getChannels() == 8);
        for (int k = 0; k < 16; ++k) {
            assert(l.outputs[k < 8 ? L::STEPS_A_OUT : L::STEPS_B_OUT].getVoltage(k & 7) == (k == step ? 10.f : 0.f));
            assert(l.lights[L::STEP_LIGHT + k].getBrightness() == (k == step ? 1.f : 0.f));
        }
    }
    // CV ranges at step 16 (knob 1) and step 1 (knob 0); H CLOCK patched and low: no edges.
    l.params[L::SOURCE].setValue(1); l.inputs[L::H_CLOCK_IN].channels = 1;
    for (int range : {1, 2}) {
        l.params[L::RANGE].setValue(range);
        press(l, 0xff); assert(l.outputs[L::CV_OUT].getVoltage() == 5.f);
        press(l, 0x00); assert(l.outputs[L::CV_OUT].getVoltage() == (range == 1 ? 0.f : -5.f));
    }
    l.params[L::RANGE].setValue(0);
    // Jumps through the panel from state 0x36 (V 3, H 6).
    for (auto& in : {L::JUMP_H_IN, L::JUMP_V_IN}) l.inputs[in].channels = 1;
    press(l, 0x36);
    l.inputs[L::JUMP_H_IN].setVoltage(10); l.process(a);                     // Follow: H = 5 x V's position
    assert(l.published.load() == unsigned(0x30 | lt::followH(3)));
    l.inputs[L::JUMP_H_IN].setVoltage(0); l.process(a);
    press(l, 0x36);
    l.inputs[L::JUMP_V_IN].setVoltage(10); l.process(a);
    assert(l.published.load() == unsigned(lt::followV(6) << 4 | 6));
    l.inputs[L::JUMP_V_IN].setVoltage(0); l.process(a);
    l.params[L::JUMP].setValue(1);                                         // Load: the jam values, once
    press(l, 0x36);
    l.inputs[L::JUMP_H_IN].setVoltage(10); l.inputs[L::JUMP_V_IN].setVoltage(10); l.process(a);
    assert(l.published.load() == unsigned(lt::loadV(6, 3) << 4 | lt::loadH(6, 3)));
    unsigned held = l.published.load(); l.process(a); assert(l.published.load() == held);
    l.inputs[L::JUMP_H_IN].setVoltage(0); l.inputs[L::JUMP_V_IN].setVoltage(0); l.process(a);
    // BUS IN: JUMP V takes bits 4-7, JUMP H bits 0-3.
    putBus(l.inputs[L::BUS_IN], 0xa7); press(l, 0x36);
    l.inputs[L::JUMP_V_IN].setVoltage(10); l.process(a); assert(l.published.load() == 0xa6u);
    l.inputs[L::JUMP_H_IN].setVoltage(10); l.process(a); assert(l.published.load() == 0xa7u);
    l.inputs[L::JUMP_H_IN].setVoltage(0); l.inputs[L::JUMP_V_IN].setVoltage(0); l.process(a);
    l.inputs[L::BUS_IN].channels = 0;
    // Persistence: params and the state; bounded restore; Initialize.
    l.params[L::RANGE].setValue(2); l.params[L::SLEW_A].setValue(.3f);
    press(l, 0x5c);
    json_t* j = l.toJson(); L r; r.model = modelLattice; connectOutputs(r); r.fromJson(j); json_decref(j);
    assert(r.params[L::JUMP].getValue() == 1 && r.params[L::RANGE].getValue() == 2 && r.params[L::SLEW_A].getValue() == .3f);
    r.inputs[L::H_CLOCK_IN].channels = 1; r.process(a);
    assert(word(r.outputs[L::BUS_OUT]) == 0x5c && r.outputs[L::BUS_OUT].getChannels() == 9 && r.outputs[L::CLOCK_OUT].getVoltage() == 10);
    json_t* bad = json_pack("{s:I}", "state", json_int_t(1000)); r.dataFromJson(bad); json_decref(bad);
    r.process(a); assert(word(r.outputs[L::BUS_OUT]) == 255);
    r.onReset(Module::ResetEvent{});
    assert(r.params[L::JUMP].getValue() == 0 && r.params[L::RANGE].getValue() == 0 && r.params[L::SEED].getValue() == 0);
    r.process(a); assert(word(r.outputs[L::BUS_OUT]) == 0);
    // Slew from 0 to 10 V: knob 0 (0.5 ms) arrives within 10 ms; knob 1 (500 ms) is a fiftieth of a
    // time constant along. Then at a middle knob, 2 V of slew CV quarters the time.
    {
        L s; s.model = modelLattice; connectOutputs(s);
        s.params[L::SOURCE].setValue(1); s.inputs[L::H_CLOCK_IN].channels = 1;
        s.params[L::STEP + 15].setValue(1); s.params[L::SLEW_A].setValue(0); s.params[L::SLEW_B].setValue(1);
        for (int t = 0; t < 10; ++t) s.process(a);
        press(s, 0xff);
        for (int t = 2; t < 480; ++t) s.process(a);
        assert(s.outputs[L::CV_OUT].getVoltage() == 10 && s.outputs[L::SLEW_A_OUT].getVoltage() > 9.99f);
        assert(std::abs(s.outputs[L::SLEW_B_OUT].getVoltage() - 10 * (1 - std::exp(-480 / 24000.))) < .002);
    }
    {
        L s; s.model = modelLattice; connectOutputs(s);
        s.params[L::SOURCE].setValue(1); s.inputs[L::H_CLOCK_IN].channels = 1;
        s.params[L::STEP + 15].setValue(1); s.params[L::SLEW_A].setValue(.5f); s.params[L::SLEW_B].setValue(.5f);
        s.inputs[L::SLEW_B_CV_IN].channels = 1; s.inputs[L::SLEW_B_CV_IN].setVoltage(2);
        for (int t = 0; t < 48000; ++t) s.process(a);                     // settle the control at step 1
        press(s, 0xff);
        int n = int(std::round(lt::Slew::tau(.5) / 4 * 48000));
        for (int t = 2; t < n; ++t) s.process(a);
        float slowA = s.outputs[L::SLEW_A_OUT].getVoltage(), fastB = s.outputs[L::SLEW_B_OUT].getVoltage();
        assert(std::abs(fastB - 10 * (1 - std::exp(-1.))) < .05 && std::abs(slowA - 10 * (1 - std::exp(-.25))) < .05);
    }
}

// A Counter (INC 37) into Lattice's BUS IN, its CLK OUT into both jumps, while Lattice's H clock
// runs on its own: every Counter word lands as Lattice's state, and the clock counts on from it,
// through one-sample cables in random module order.
static std::vector<int> latticeChain(float sr, unsigned orderSeed) {
    using C = ByteCounter; using L = ByteLattice;
    C c; L l; connectOutputs(c); connectOutputs(l); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(37); c.inputs[C::CLOCK_IN].channels = 1;
    l.params[L::SOURCE].setValue(1);
    for (auto in : {L::H_CLOCK_IN, L::JUMP_H_IN, L::JUMP_V_IN}) l.inputs[in].channels = 1;
    std::mt19937 order(orderSeed); std::vector<int> states;
    float jumpBefore = 0;
    for (int t = 0; t < 96 * 30; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 96 == 40 ? 10 : 0);
        l.inputs[L::H_CLOCK_IN].setVoltage(t % 16 == 3 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], l.inputs[L::BUS_IN]);
        float jump = c.outputs[C::CLOCK_OUT].getVoltage();
        l.inputs[L::JUMP_H_IN].setVoltage(jump); l.inputs[L::JUMP_V_IN].setVoltage(jump);
        if (order() & 1) { c.process(a); l.process(a); } else { l.process(a); c.process(a); }
        if (jump >= 1 && jumpBefore < 1) assert(l.published.load() == inputWord(l.inputs[L::BUS_IN]));
        jumpBefore = jump;
        if (l.outputs[L::CLOCK_OUT].getVoltage() == 10) states.push_back(word(l.outputs[L::BUS_OUT]));
    }
    assert(states.size() >= 29 * 6 + 29);
    return states;
}

// Parallel Sum with real ports: the default B8/XNOR cycle at Engine rate with CV, AUDIO, GATE and
// lights; W CV, mono and polyphonic; the MASK bus; INJECT; GATE -> INJECT feedback through a
// cable equal to tapping stage 1; HOT; persistence with bounded restore; Initialize.
static void parallelSumPorts() {
    using P = ByteParallelSum; namespace ps = dspemu::parallel_sum;
    auto a = args();
    auto press = [&](P& m, int seed) {
        m.params[P::SEED].setValue(seed); m.params[P::LOAD_BUTTON].setValue(1); m.process(a);
        m.params[P::LOAD_BUTTON].setValue(0); m.process(a);
    };
    P s; s.model = modelParallelSum; connectOutputs(s);
    assert(s.params.size() == 16 && s.inputs.size() == 5 && s.outputs.size() == 5 && s.lights.size() == 12);
    s.params[P::SOURCE].setValue(4);                                        // Engine: a step every sample
    s.process(a); assert(word(s.outputs[P::BUS_OUT]) == 0);
    uint8_t model = 0;
    for (int t = 1; t <= 255; ++t) {
        s.process(a); model = ps::step(model, 0xb8, ps::Sense::Xnor, false);
        int ones = __builtin_popcount(model);
        assert(s.outputs[P::BUS_OUT].getChannels() == 8 && word(s.outputs[P::BUS_OUT]) == model);
        assert(s.outputs[P::CV_OUT].getVoltage() == 1.25f * ones && s.outputs[P::AUDIO_OUT].getVoltage() == 1.25f * (2 * ones - 8));
        assert(s.outputs[P::GATE_OUT].getVoltage() == (model & 1 ? 10.f : 0.f));
        for (int k = 0; k < 8; ++k) assert(s.lights[P::STAGE_LIGHT + k].getBrightness() == (model >> k & 1));
        assert(s.published.load() == (0xb800u | model) && (t == 255) == (model == 0));
    }
    // W CV: channel k to stage k + 1, 1 per 5 V, clamped; a mono cable moves stage 1 only.
    s.params[P::SOURCE].setValue(1); s.inputs[P::CLOCK_IN].channels = 1;   // CLOCK patched, low: no edges
    press(s, 0x07);
    s.inputs[P::W_CV_IN].channels = 3;
    s.inputs[P::W_CV_IN].setVoltage(5, 0); s.inputs[P::W_CV_IN].setVoltage(-5, 1); s.inputs[P::W_CV_IN].setVoltage(-10, 2);
    s.process(a); assert(s.outputs[P::CV_OUT].getVoltage() == 0 && s.outputs[P::AUDIO_OUT].getVoltage() == -6.25f);
    s.inputs[P::W_CV_IN].channels = 1; s.inputs[P::W_CV_IN].setVoltage(-5, 0);
    s.process(a); assert(s.outputs[P::CV_OUT].getVoltage() == 2.5f);
    s.inputs[P::W_CV_IN].channels = 0;
    // HOT: 6 V a stage, clipped at 10.5 V.
    s.params[P::HOT].setValue(1);
    press(s, 0xff); assert(s.outputs[P::CV_OUT].getVoltage() == 10.5f && s.outputs[P::AUDIO_OUT].getVoltage() == 10.5f);
    press(s, 0x00); assert(s.outputs[P::CV_OUT].getVoltage() == 0 && s.outputs[P::AUDIO_OUT].getVoltage() == -10.5f);
    assert(s.lights[P::HOT_LIGHT].getBrightness() == 1);
    s.params[P::HOT].setValue(0);
    // The MASK bus replaces the knob: mask 00 under XNOR shifts in ones.
    s.params[P::SOURCE].setValue(4); putBus(s.inputs[P::MASK_IN], 0x00);
    for (int t = 0; t < 10; ++t) s.process(a);
    assert(word(s.outputs[P::BUS_OUT]) == 0xff && (s.published.load() >> 8) == 0);
    s.inputs[P::MASK_IN].channels = 0;
    // INJECT through the comparator: MASK 00 under XOR is a delay line for it.
    s.params[P::MASK].setValue(0); s.params[P::SENSE].setValue(0); s.inputs[P::INJECT_IN].channels = 1;
    s.inputs[P::INJECT_IN].setVoltage(-5); for (int t = 0; t < 8; ++t) s.process(a);
    assert(word(s.outputs[P::BUS_OUT]) == 0 && s.lights[P::INJECT_LIGHT].getBrightness() == 0);
    s.inputs[P::INJECT_IN].setVoltage(5); for (int t = 0; t < 3; ++t) s.process(a);
    assert(word(s.outputs[P::BUS_OUT]) == 0x07 && s.lights[P::INJECT_LIGHT].getBrightness() == 1);
    s.inputs[P::INJECT_IN].channels = 0;
    // GATE -> INJECT through a one-sample cable taps stage 1: B8 behaves as B9.
    {
        P g; g.model = modelParallelSum; connectOutputs(g); g.params[P::SOURCE].setValue(4); g.inputs[P::INJECT_IN].channels = 1;
        uint8_t m = 0;
        for (int t = 0; t < 600; ++t) {
            g.inputs[P::INJECT_IN].setVoltage(g.outputs[P::GATE_OUT].getVoltage());
            g.process(a);
            if (t > 0) m = ps::step(m, 0xb9, ps::Sense::Xnor, false);
            assert(word(g.outputs[P::BUS_OUT]) == m);
        }
    }
    // Persistence: params and the word; bounded restore; Initialize.
    s.params[P::SOURCE].setValue(1); s.params[P::MASK].setValue(0x8e); s.params[P::HOT].setValue(1); s.params[P::WEIGHT + 3].setValue(-.5f);
    press(s, 0x3c);
    json_t* j = s.toJson(); P r; r.model = modelParallelSum; connectOutputs(r); r.fromJson(j); json_decref(j);
    assert(r.params[P::MASK].getValue() == 0x8e && r.params[P::SENSE].getValue() == 0 && r.params[P::HOT].getValue() == 1);
    assert(r.params[P::WEIGHT + 3].getValue() == -.5f && r.params[P::SOURCE].getValue() == 1);
    r.inputs[P::CLOCK_IN].channels = 1; r.process(a);
    assert(word(r.outputs[P::BUS_OUT]) == 0x3c && r.outputs[P::BUS_OUT].getChannels() == 9 && r.outputs[P::CLOCK_OUT].getVoltage() == 10);
    json_t* bad = json_pack("{s:I}", "word", json_int_t(1000)); r.dataFromJson(bad); json_decref(bad);
    r.process(a); assert(word(r.outputs[P::BUS_OUT]) == 255);
    r.onReset(Module::ResetEvent{});
    assert(r.params[P::MASK].getValue() == 0xb8 && r.params[P::SENSE].getValue() == 1 && r.params[P::HOT].getValue() == 0);
    for (int k = 0; k < 8; ++k) assert(r.params[P::WEIGHT + k].getValue() == 1);
    r.process(a); assert(word(r.outputs[P::BUS_OUT]) == 0);
}

// A Counter (INC 7) into Parallel Sum's MASK bus, which clocks it in Auto: on each strobe the word
// takes one step under the mask that arrived with it, through one-sample cables in random order.
static std::vector<int> parallelSumChain(float sr, unsigned orderSeed) {
    using C = ByteCounter; using P = ByteParallelSum; namespace ps = dspemu::parallel_sum;
    C c; P s; connectOutputs(c); connectOutputs(s); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::INC].setValue(7); c.inputs[C::CLOCK_IN].channels = 1;
    std::mt19937 order(orderSeed); std::vector<int> words; int previous = -1;
    for (int t = 0; t < 40 * 200; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 40 == 5 ? 10 : 0);
        copy(c.outputs[C::BUS_OUT], s.inputs[P::MASK_IN]);
        if (order() & 1) { c.process(a); s.process(a); } else { s.process(a); c.process(a); }
        if (s.outputs[P::CLOCK_OUT].getVoltage() == 10) {
            int w = word(s.outputs[P::BUS_OUT]), mask = inputWord(s.inputs[P::MASK_IN]);
            if (previous >= 0) {
                assert(w == ps::step(uint8_t(previous), uint8_t(mask), ps::Sense::Xnor, false));
                assert((s.published.load() >> 8) == unsigned(mask));
            }
            previous = w; words.push_back(w);
        }
    }
    assert(words.size() >= 199);
    return words;
}

// Wheel with real ports against an engine fed the same controls: both buses, U, G and the gates
// at Engine rate, a capture from the button, LENGTH Off, another window; capture from the jack;
// persistence of both registers with bounded restore; Initialize.
static void wheelPorts() {
    using W = ByteWheel; namespace wh = dspemu::wheel;
    auto a = args();
    W w; w.model = modelWheel; connectOutputs(w);
    assert(w.params.size() == 8 && w.inputs.size() == 3 && w.outputs.size() == 7 && w.lights.size() == 11);
    w.params[W::SOURCE].setValue(3); w.params[W::SEED].setValue(0x5e);
    wh::Wheel ref; ref.clear(); wh::Parameters rp; rp.mode = wh::ClockMode::Engine; rp.seed = 0x5e; wh::Inputs ri;
    w.params[W::LOAD_BUTTON].setValue(1); ri.clock.resetButton = true;
    for (int t = 0; t < 400; ++t) {
        if (t == 1) { w.params[W::LOAD_BUTTON].setValue(0); ri.clock.resetButton = false; }
        if (t == 40) { w.params[W::CAPTURE_BUTTON].setValue(1); ri.captureButton = true; }
        if (t == 41) { w.params[W::CAPTURE_BUTTON].setValue(0); ri.captureButton = false; }
        if (t == 150) { w.params[W::LENGTH].setValue(0); rp.length = 0; }
        if (t == 250) { w.params[W::STRIDE].setValue(2); w.params[W::OFFSET].setValue(5); rp.stride = 3; rp.offset = 5; }
        w.process(a); auto o = ref.tick(rp, ri, 48000);
        int lw = word(w.outputs[W::LOOP_OUT]), fw = word(w.outputs[W::FREE_OUT]);
        assert(w.outputs[W::LOOP_OUT].getChannels() == 8 && w.outputs[W::FREE_OUT].getChannels() == 8);
        assert(lw == o.loopWord && fw == o.freeWord && w.savedFree.load() == o.free && w.savedLoop.load() == o.loop);
        assert(w.outputs[W::U_OUT].getVoltage() == (lw - 128) * 5.f / 128.f);
        assert(w.outputs[W::G_OUT].getVoltage() == (__builtin_popcount(unsigned(lw)) - 4) * 1.25f);
        assert(w.outputs[W::LOOP_GATE_OUT].getVoltage() == (o.loop & 1 ? 10.f : 0.f) && w.outputs[W::NOISE_GATE_OUT].getVoltage() == (o.free & 1 ? 10.f : 0.f));
        for (int k = 0; k < 8; ++k) assert(w.lights[W::BIT_LIGHT + k].getBrightness() == (lw >> k & 1));
        if (t == 0) assert(o.free == 0x5e5e5e && o.loop == 0x5e5e5e);
        if (t == 40) assert(o.captured && o.loop == o.free);
    }
    // Capture from the jack, on the CLOCK jack: armed until the next edge.
    w.params[W::SOURCE].setValue(1); w.inputs[W::CLOCK_IN].channels = 1; w.inputs[W::CAPTURE_IN].channels = 1;
    w.params[W::LENGTH].setValue(8);
    for (int t = 0; t < 4; ++t) w.process(a);
    w.inputs[W::CAPTURE_IN].setVoltage(10); w.process(a); w.inputs[W::CAPTURE_IN].setVoltage(0);
    assert(w.lights[W::ARMED_LIGHT].getBrightness() == 1 && w.savedLoop.load() != w.savedFree.load());
    for (int t = 0; t < 3; ++t) w.process(a);
    w.inputs[W::CLOCK_IN].setVoltage(10); w.process(a);
    assert(w.lights[W::ARMED_LIGHT].getBrightness() == 0 && w.savedLoop.load() == w.savedFree.load());
    assert(w.outputs[W::CLOCK_OUT].getVoltage() == 10 && w.outputs[W::LOOP_OUT].getChannels() == 9);
    assert(word(w.outputs[W::LOOP_OUT]) == word(w.outputs[W::FREE_OUT]));
    w.inputs[W::CLOCK_IN].setVoltage(0); w.process(a);
    // Persistence: both registers and the controls; bounded restore; Initialize.
    uint32_t freeState = w.savedFree.load(), loopState = w.savedLoop.load();
    json_t* j = w.toJson(); W r; r.model = modelWheel; connectOutputs(r); r.fromJson(j); json_decref(j);
    assert(r.params[W::LENGTH].getValue() == 8 && r.params[W::STRIDE].getValue() == 2 && r.params[W::OFFSET].getValue() == 5);
    r.inputs[W::CLOCK_IN].channels = 1; r.process(a);
    assert(r.savedFree.load() == freeState && r.savedLoop.load() == loopState);
    assert(word(r.outputs[W::LOOP_OUT]) == wh::window(loopState, 5, 3) && word(r.outputs[W::FREE_OUT]) == wh::window(freeState, 5, 3));
    assert(r.outputs[W::CLOCK_OUT].getVoltage() == 10);
    json_t* bad = json_pack("{s:I,s:I}", "free", json_int_t(1) << 40, "loop", json_int_t(-5)); r.dataFromJson(bad); json_decref(bad);
    r.process(a); assert(r.savedFree.load() == wh::ALL && r.savedLoop.load() == 0);
    r.onReset(Module::ResetEvent{});
    assert(r.params[W::LENGTH].getValue() == 16 && r.params[W::STRIDE].getValue() == 0 && r.params[W::OFFSET].getValue() == 0 && r.params[W::SEED].getValue() == 0);
    r.process(a); assert(r.savedFree.load() == 0 && word(r.outputs[W::LOOP_OUT]) == 0);
}

// A Counter's clock drives Wheel and its CARRY (MOD 8) captures: every eighth edge LOOP takes FREE,
// and between captures LOOP recirculates its eight stages, through one-sample cables in random order.
static std::vector<int> wheelChain(float sr, unsigned orderSeed) {
    using C = ByteCounter; using W = ByteWheel; namespace wh = dspemu::wheel;
    C c; W w; connectOutputs(c); connectOutputs(w); auto a = args(sr);
    c.params[C::CLOCK_MODE].setValue(1); c.params[C::MODULUS].setValue(8); c.inputs[C::CLOCK_IN].channels = 1;
    w.params[W::SOURCE].setValue(1); w.params[W::LENGTH].setValue(8); w.params[W::SEED].setValue(0x91);
    w.inputs[W::CLOCK_IN].channels = 1; w.inputs[W::CAPTURE_IN].channels = 1;
    std::mt19937 order(orderSeed); std::vector<int> words; int captures = 0; uint32_t loopBefore = 0;
    for (int t = 0; t < 24 * 400; ++t) {
        c.inputs[C::CLOCK_IN].setVoltage(t % 24 == 3 ? 10 : 0);
        w.inputs[W::CLOCK_IN].setVoltage(c.outputs[C::CLOCK_OUT].getVoltage());
        float capture = c.outputs[C::CARRY_OUT].getVoltage();
        w.inputs[W::CAPTURE_IN].setVoltage(capture);
        if (order() & 1) { c.process(a); w.process(a); } else { w.process(a); c.process(a); }
        if (w.outputs[W::CLOCK_OUT].getVoltage() == 10) {
            uint32_t loop = w.savedLoop.load();
            if (capture >= 1) { assert(loop == w.savedFree.load()); ++captures; }
            else if (words.size() > 1) assert(loop == wh::stepLoop(loopBefore, 0, 8));
            loopBefore = loop; words.push_back(word(w.outputs[W::LOOP_OUT]));
        }
    }
    assert(captures >= 45 && words.size() >= 390);
    return words;
}

int main() {
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug = "ByteBusLab"; plugin.version = "2.0.0";
    plugin.addModel(modelClockedRegister); plugin.addModel(modelCounter); plugin.addModel(modelSwitch); plugin.addModel(modelPipeline); plugin.addModel(modelLookup2); plugin.addModel(modelCounter2); plugin.addModel(modelFan); plugin.addModel(modelLattice); plugin.addModel(modelParallelSum); plugin.addModel(modelWheel); pluginInstance = &plugin;
    rack::plugin::plugins.push_back(&plugin);
    portsAndState();
    counterPorts();
    counterRunModes();
    for (float sr : {44100.f, 48000.f, 96000.f}) counterEnvelope(sr);
    switchPorts();
    auto switched = commutator(48000, 11);
    assert(commutator(44100, 12) == switched && commutator(96000, 13) == switched);
    for (float sr : {44100.f, 48000.f, 96000.f}) reclock(sr);
    auto chain = carryChain(48000, 1); assert(carryChain(44100, 2) == chain && carryChain(96000, 3) == chain);
    auto library = std::make_shared<dspemu::lookup::Library>(dspemu::lookup::Library::load("../res/lookup-v1.bin"));
    auto rhythm = rhythmChain(library, 48000, 4);
    assert(rhythmChain(library, 44100, 5) == rhythm && rhythmChain(library, 96000, 6) == rhythm);
    auto jumped = jumpChain(library, 48000, 7);
    for (float sr : {44100.f, 48000.f, 96000.f}) libraryV2Chains(sr);
    auto canon = pipelineCanon(library, 48000, 21);
    assert(pipelineCanon(library, 44100, 22) == canon && pipelineCanon(library, 96000, 23) == canon);
    pipelineLoop();
    for (float sr : {44100.f, 48000.f, 96000.f}) counter2Module(sr);
    for (float sr : {44100.f, 48000.f, 96000.f}) ratioChain(sr, unsigned(sr));
    auto twoWords = lookup2Chain(48000, 31);
    assert(lookup2Chain(44100, 32) == twoWords && lookup2Chain(96000, 33) == twoWords);
    assert(jumpChain(library, 44100, 8) == jumped && jumpChain(library, 96000, 9) == jumped);
    for (uint8_t seed : {uint8_t(0), uint8_t(0xa5), uint8_t(255)}) {
        auto reference = feedback(48000, seed, 1);
        assert(feedback(44100, seed, 2) == reference);
        assert(feedback(96000, seed, 3) == reference);
    }
    fanPorts();
    auto fanned = fanChain(48000, 41);
    assert(fanChain(44100, 42) == fanned && fanChain(96000, 43) == fanned);
    latticePorts();
    auto latticed = latticeChain(48000, 51);
    assert(latticeChain(44100, 52) == latticed && latticeChain(96000, 53) == latticed);
    parallelSumPorts();
    auto summed = parallelSumChain(48000, 61);
    assert(parallelSumChain(44100, 62) == summed && parallelSumChain(96000, 63) == summed);
    wheelPorts();
    auto wheeled = wheelChain(48000, 71);
    assert(wheelChain(44100, 72) == wheeled && wheelChain(96000, 73) == wheeled);
    rack::plugin::plugins.clear();
    std::cout << "PASS: Rack ports, persistence, clocked/unclocked modes; real Vastish FUN feedback with one-sample cables, random module order, RESET replay, no gates, three seeds at 44.1/48/96 kHz; Counter ports, persistence, LOAD, Engine rate, Initialize, run modes with saved Pendulum direction and 2.3 patches, One-shot -> Lookup Curve 32 envelopes, CARRY -> Counter/Register chains and Counter -> Lookup Rhythm gates and Counter -> Lookup JUMP steps in random order at 44.1/48/96 kHz; One-shot -> Lookup Envelope and a Density build-up from CARRY -> JUMP; Switch ports, gate select, persistence, a Counter A/B commutator and Mix VCA in random order, and reclocking at 44.1/48/96 kHz; Pipeline canon of a Rhythm word with SPREAD in random order at 44.1/48/96 kHz, LOOP gate and button, history save/reload, CLEAR, Initialize; Counters -> Lookup2 with CARRY -> JUMP in random order at 44.1/48/96 kHz; Counter 2 polymeter, exact resume and bounded restore, RATIO words and a Lookup Ratio -> Counter 2 chain at 44.1/48/96 kHz; Fan ports, 16-channel cables both ways, one-hot counter mode, Hold, DIR gate, GLITCH, persistence and Initialize, and a Counter -> Fan chain in random order at 44.1/48/96 kHz; Lattice ports, the normalled raster, CV ranges, gates and lights, Follow/Load/BUS IN jumps through the panel, slew and slew CV, persistence and Initialize, and Counter -> Lattice BUS IN jumps under its own H clock in random order at 44.1/48/96 kHz; Parallel Sum ports, the B8/XNOR cycle with CV, AUDIO, GATE and lights, W CV, HOT, the MASK bus, INJECT, GATE -> INJECT feedback, persistence and Initialize, and a Counter -> MASK bus chain clocking it in random order at 44.1/48/96 kHz; Wheel ports against its engine at Engine rate with capture, LENGTH Off and another window, capture from the jack, persistence of both registers and Initialize, and Counter clock and CARRY -> Wheel capture in random order at 44.1/48/96 kHz\n";
}
