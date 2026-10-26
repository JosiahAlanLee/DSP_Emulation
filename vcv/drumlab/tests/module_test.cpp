// SPDX-License-Identifier: BSD-3-Clause
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Percussion.cpp"
#include <cassert>
#include <iostream>
#include <limits>
Plugin* pluginInstance = nullptr;

static Module::ProcessArgs args(float sr) {
    Module::ProcessArgs a{}; a.sampleRate = sr; a.sampleTime = 1 / sr; return a;
}
static void connect(Percussion& m, int channels = 4) {
    for (auto& out : m.outputs) out.channels = 1;
    m.inputs[Percussion::TRIG_IN].channels = channels;
    m.model = modelPercussion;
}
static void gates(Percussion& m, float v) {
    for (int c = 0; c < m.inputs[Percussion::TRIG_IN].getChannels(); ++c)
        m.inputs[Percussion::TRIG_IN].setVoltage(v, c);
}

static void captures(float sr) {
    Percussion a, b; connect(a); connect(b);
    auto t = args(sr);
    gates(a, 10); gates(b, 10); a.process(t); b.process(t);
    assert(a.outputs[Percussion::POLY_OUT].getChannels() == 4);
    for (int c = 0; c < 4; ++c) {
        assert(a.engine.lane[c].voice.hit.machine == drumlab::Machine(c));
        assert(a.engine.lane[c].hits == 1);
    }
    a.params[Percussion::TUNE].setValue(24);
    a.params[Percussion::DECAY].setValue(-7);
    a.params[Percussion::BITS].setValue(4);
    a.params[Percussion::MACHINE].setValue(5);
    a.inputs[Percussion::COLOR_IN].channels = 1;
    a.inputs[Percussion::COLOR_IN].setVoltage(-5);
    for (int i = 0; i < 1000; ++i) {
        a.process(t); b.process(t);
        for (int c = 0; c < 4; ++c)
            assert(a.outputs[Percussion::POLY_OUT].getVoltage(c) == b.outputs[Percussion::POLY_OUT].getVoltage(c));
    }
    assert(a.engine.lane[0].hits == 1); // a held gate never retriggers
    gates(a, 0); a.process(t); gates(a, 10); a.process(t);
    for (int c = 0; c < 4; ++c) {
        const auto& lane = a.engine.lane[c];
        assert(lane.hits == 2 && lane.voice.hit.pitch == 2 && lane.voice.hit.bits == 4);
        assert(lane.voice.hit.color == 0); // mono parameter CV broadcasts
        assert(lane.tail.hit.pitch == 0 && lane.tail.hit.bits == 16);
    }
    // Removed channels are cleared, including their trigger state.
    a.inputs[Percussion::TRIG_IN].channels = 1; a.process(t);
    assert(a.outputs[Percussion::POLY_OUT].getChannels() == 1 && !a.engine.lane[3].voice.active);
    a.inputs[Percussion::TRIG_IN].channels = 4; gates(a, 10); a.process(t);
    assert(a.engine.lane[3].hits == 1);
    // Unpatched and short poly cables cannot expose stale high channels.
    a.inputs[Percussion::TRIG_IN].channels = 0; a.process(t);
    auto count = a.engine.lane[0].hits;
    a.process(t); assert(a.engine.lane[0].hits == count);
    a.inputs[Percussion::COLOR_IN].channels = 2;
    a.inputs[Percussion::COLOR_IN].setVoltage(10, 3);
    assert(a.cv(Percussion::COLOR_IN, 3) == 0);
}

static void velocityAndState() {
    Percussion m; connect(m, 2); auto t = args(48000);
    m.inputs[Percussion::VEL_IN].channels = 2;
    m.inputs[Percussion::VEL_IN].setVoltage(0, 0);
    m.inputs[Percussion::VEL_IN].setVoltage(5, 1);
    m.inputs[Percussion::MACHINE_IN].channels = 2;
    m.inputs[Percussion::MACHINE_IN].setVoltage(4, 0);
    m.inputs[Percussion::MACHINE_IN].setVoltage(5, 1);
    gates(m, 10); m.process(t);
    assert(!m.engine.lane[0].voice.active && m.engine.lane[1].voice.hit.velocity == .5);
    assert(m.engine.lane[1].voice.hit.machine == drumlab::Machine::Clap);
    gates(m, 0); m.process(t); m.velocityHigh.store(5); gates(m, 10); m.process(t);
    assert(m.engine.lane[1].voice.hit.velocity == 1);
    json_t* saved = m.toJson(); Percussion restored; connect(restored); restored.fromJson(saved); json_decref(saved);
    assert(restored.velocityHigh.load() == 5);
    assert(restored.params[Percussion::MACHINE].getValue() == 6);
    assert(!restored.engine.lane[1].voice.active); // no serialized transient voice
    json_t* bad = json_pack("{s:f}", "velocityHigh", 5.5); restored.dataFromJson(bad); json_decref(bad);
    assert(restored.velocityHigh.load() == 10);
}

static void resetChokeAndBadInputs() {
    Percussion m; connect(m); auto t = args(96000);
    m.inputs[Percussion::RESET_IN].channels = 1;
    m.inputs[Percussion::RESET_IN].setVoltage(10);
    gates(m, 10); m.process(t);
    assert(m.engine.lane[0].hits == 1); // simultaneous reset + hit is audible
    m.inputs[Percussion::RESET_IN].setVoltage(0); m.process(t);
    m.inputs[Percussion::RESET_IN].setVoltage(10); m.process(t);
    assert(m.engine.lane[0].hits == 0); // held gate does not fire again on reset
    m.inputs[Percussion::RESET_IN].setVoltage(0); gates(m, 0); m.process(t);
    gates(m, 10); m.process(t);
    for (int i = 0; i < 1000; ++i) m.process(t);
    m.inputs[Percussion::CHOKE_IN].channels = 1; m.inputs[Percussion::CHOKE_IN].setVoltage(10);
    m.process(t);
    for (int i = 0; i < 200; ++i) m.process(t);
    for (int c = 0; c < 4; ++c) assert(m.outputs[Percussion::POLY_OUT].getVoltage(c) == 0);
    for (auto& input : m.inputs) { input.channels = 16; for (int c = 0; c < 16; ++c) input.setVoltage(std::numeric_limits<float>::quiet_NaN(), c); }
    for (auto& param : m.params) param.setValue(std::numeric_limits<float>::quiet_NaN());
    for (int i = 0; i < 500; ++i) m.process(t);
    for (auto& output : m.outputs) for (int c = 0; c < output.getChannels(); ++c) assert(std::isfinite(output.getVoltage(c)));
}

int main() {
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug = "DrumLab"; plugin.version = "2.0.0";
    plugin.addModel(modelPercussion); pluginInstance = &plugin;
    rack::plugin::plugins.push_back(&plugin);
    for (float sr : {44100.f, 48000.f, 96000.f}) captures(sr);
    velocityAndState(); resetChokeAndBadInputs();
    rack::plugin::plugins.clear();
    contextSet(nullptr); delete context.engine; context.engine = nullptr;
    std::cout << "PASS: real Rack ports, four-lane kit, poly/mono CV capture, independent tails, velocity profiles, JSON, reset/choke, channel removal, non-finite inputs\n";
}
