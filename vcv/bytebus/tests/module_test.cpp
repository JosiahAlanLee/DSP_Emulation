// Real Rack module APIs, including a Register -> Vastish FUN -> Register loop.
// The test host supplies a Rack context; the plugin itself uses rack.hpp only.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Register.cpp"
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

int main() {
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug = "ByteBusLab"; plugin.version = "2.0.0";
    plugin.addModel(modelClockedRegister); pluginInstance = &plugin;
    rack::plugin::plugins.push_back(&plugin);
    portsAndState();
    for (uint8_t seed : {uint8_t(0), uint8_t(0xa5), uint8_t(255)}) {
        auto reference = feedback(48000, seed, 1);
        assert(feedback(44100, seed, 2) == reference);
        assert(feedback(96000, seed, 3) == reference);
    }
    rack::plugin::plugins.clear();
    std::cout << "PASS: Rack ports, persistence, clocked/unclocked modes; real Vastish FUN feedback with one-sample cables, random module order, RESET replay, no gates, three seeds at 44.1/48/96 kHz\n";
}
