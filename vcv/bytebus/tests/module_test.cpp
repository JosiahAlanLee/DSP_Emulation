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

int main() {
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug = "ByteBusLab"; plugin.version = "2.0.0";
    plugin.addModel(modelClockedRegister); plugin.addModel(modelCounter); plugin.addModel(modelSwitch); pluginInstance = &plugin;
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
    assert(jumpChain(library, 44100, 8) == jumped && jumpChain(library, 96000, 9) == jumped);
    for (uint8_t seed : {uint8_t(0), uint8_t(0xa5), uint8_t(255)}) {
        auto reference = feedback(48000, seed, 1);
        assert(feedback(44100, seed, 2) == reference);
        assert(feedback(96000, seed, 3) == reference);
    }
    rack::plugin::plugins.clear();
    std::cout << "PASS: Rack ports, persistence, clocked/unclocked modes; real Vastish FUN feedback with one-sample cables, random module order, RESET replay, no gates, three seeds at 44.1/48/96 kHz; Counter ports, persistence, LOAD, Engine rate, Initialize, run modes with saved Pendulum direction and 2.3 patches, One-shot -> Lookup Curve 32 envelopes, CARRY -> Counter/Register chains and Counter -> Lookup Rhythm gates and Counter -> Lookup JUMP steps in random order at 44.1/48/96 kHz; One-shot -> Lookup Envelope and a Density build-up from CARRY -> JUMP; Switch ports, gate select, persistence, a Counter A/B commutator and Mix VCA in random order, and reclocking at 44.1/48/96 kHz\n";
}
