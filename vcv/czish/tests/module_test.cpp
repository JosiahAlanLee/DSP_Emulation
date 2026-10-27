// Real Rack port/parameter tests; no VCV window or hardware is required.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/DacStage.cpp"
#include "../src/Czish.cpp"
#include "../../bytebus/src/Register.cpp"
#include <cassert>
#include <cmath>
#include <iostream>

Plugin *pluginInstance = nullptr;
static Module::ProcessArgs args(float sr = 48000) {
    Module::ProcessArgs a{};
    a.sampleRate = sr;
    a.sampleTime = 1 / sr;
    return a;
}
static void connectedOutputs(Module &m) {
    for (auto &p : m.outputs)
        p.channels = 1;
}
static void setBus(Input &input, uint8_t byte, bool clocked, bool clock) {
    input.channels = clocked ? 9 : 8;
    dspemu::bus::Writer{}.write(input, byte, clocked, clock);
}
static uint8_t readBus(Output &output) {
    uint8_t v = 0;
    for (int i = 0; i < 8; ++i)
        if (output.getVoltage(i) >= 1)
            v |= uint8_t(1u << i);
    return v;
}
static void copyCable(Output &source, Input &dest) {
    dest.channels = source.getChannels();
    for (int i = 0; i < 16; ++i)
        dest.setVoltage(i < dest.channels ? source.getVoltage(i) : 0, i);
}

static void converter(float sr) {
    DacStageModule m;
    m.model = modelDacStage;
    connectedOutputs(m);
    auto a = args(sr);
    assert(m.inputs.size() == 5 && m.outputs.size() == 2);
    m.params[DacStageModule::RECON].setValue(0); // direct word behavior
    m.inputs[DacStageModule::SIGNAL].channels = 1;
    m.inputs[DacStageModule::SIGNAL].setVoltage(1.25f);
    for (int i = 0; i < 500; ++i)
        m.process(a);
    assert(std::abs(m.outputs[DacStageModule::AUDIO].getVoltage() - 1.25f) < .01f);
    assert(m.outputs[DacStageModule::BUS_OUT].getChannels() == 9);
    assert(m.stages[0].tap.divider ==
           unsigned(std::ceil(2. * m.params[DacStageModule::RATE].getValue() / sr)));
    auto plain = dspemu::dac::quantize(dspemu::dac::Profile::Yamaha, 8192);
    assert(readBus(m.outputs[DacStageModule::BUS_OUT]) == plain.high());
    setBus(m.inputs[DacStageModule::XOR_BUS], 0x80, true, false);
    for (int i = 0; i < 160; ++i)
        m.process(a);
    assert(std::abs(m.outputs[DacStageModule::AUDIO].getVoltage() - 1.25f) < .01f);
    setBus(m.inputs[DacStageModule::XOR_BUS], 0x80, true, true);
    for (int i = 0; i < 160; ++i)
        m.process(a);
    auto changed = dspemu::dac::quantize(dspemu::dac::Profile::Yamaha, 8192, 12, 0x80);
    assert(std::abs(m.outputs[DacStageModule::AUDIO].getVoltage() - 5.f * changed.value / 32768.f) < .01f);
    assert(readBus(m.outputs[DacStageModule::BUS_OUT]) == changed.high());
    setBus(m.inputs[DacStageModule::XOR_BUS], 0, true, false);
    for (int i = 0; i < 160; ++i)
        m.process(a);
    assert(std::abs(m.outputs[DacStageModule::AUDIO].getVoltage() - 5.f * changed.value / 32768.f) < .01f);
    setBus(m.inputs[DacStageModule::XOR_BUS], 0, true, true);
    for (int i = 0; i < 160; ++i)
        m.process(a);
    assert(std::abs(m.outputs[DacStageModule::AUDIO].getVoltage() - 1.25f) < .01f);

    m.inputs[DacStageModule::SIGNAL].channels = 2;
    m.inputs[DacStageModule::SIGNAL].setVoltage(-1.25f, 1);
    m.inputs[DacStageModule::XOR_BUS].channels = 0;
    for (int i = 0; i < 100; ++i)
        m.process(a);
    assert(m.outputs[DacStageModule::AUDIO].getChannels() == 2);
    assert(m.outputs[DacStageModule::AUDIO].getVoltage(0) > 0 &&
           m.outputs[DacStageModule::AUDIO].getVoltage(1) < 0);
    m.inputs[DacStageModule::SIGNAL].setVoltage(NAN, 1);
    m.process(a);
    assert(std::isfinite(m.outputs[DacStageModule::AUDIO].getVoltage(1)));
}

static void voice(float sr) {
    CzishModule m;
    m.model = modelCzish;
    connectedOutputs(m);
    auto a = args(sr);
    m.params[CzishModule::SHAPE].setValue(0);
    m.params[CzishModule::RELEASE].setValue(20);
    m.inputs[CzishModule::VOCT].channels = 1;
    m.inputs[CzishModule::VOCT].setVoltage(0);
    m.inputs[CzishModule::GATE].channels = 1;
    m.inputs[CzishModule::GATE].setVoltage(10);
    double sum = 0, first = -1, last = -1;
    int crossings = 0;
    float previous = 0;
    bool oldClock = false;
    for (int i = 0; i < int(sr * 3); ++i) {
        m.process(a);
        ++a.frame;
        float current = m.outputs[CzishModule::AUDIO].getVoltage();
        assert(std::isfinite(current));
        if (i > int(sr * .25)) {
            sum += std::abs(current);
            if (previous < 0 && current >= 0) {
                double at = i - 1 + (-previous) / (current - previous);
                if (first < 0)
                    first = at;
                last = at;
                ++crossings;
            }
        }
        bool clock = m.outputs[CzishModule::BUS_OUT].getVoltage(8) >= 1;
        assert(!(clock && oldClock));
        oldClock = clock;
        previous = current;
    }
    assert(sum > 100 && crossings > 500 && m.outputs[CzishModule::BUS_OUT].getChannels() == 9);
    double hz = (crossings - 1) * sr / (last - first);
    double cents = 1200 * std::log2(hz / 261.6255653005986);
    if (std::abs(cents) > 1) {
        std::cerr << "C4 tuning: " << hz << " Hz, " << cents << " cents\n";
        std::abort();
    }

    m.inputs[CzishModule::VOCT].channels = 2;
    m.inputs[CzishModule::VOCT].setVoltage(1.f, 1);
    for (int i = 0; i < 1000; ++i)
        m.process(a);
    assert(m.engine.gates[0].high && m.engine.gates[1].high);
    assert(m.engine.chip.voice[1].pitch > m.engine.chip.voice[0].pitch);
    m.inputs[CzishModule::DCW_CV].channels = 1;
    m.inputs[CzishModule::DCW_CV].setVoltage(100);
    for (int i = 0; i < 1000; ++i)
        m.process(a);
    m.inputs[CzishModule::GATE].setVoltage(0);
    for (int i = 0; i < int(sr * .25); ++i)
        m.process(a);
    assert(std::abs(m.outputs[CzishModule::AUDIO].getVoltage()) < .01f);
}

static void byteBusFeedback(float sr) {
    DacStageModule dac;
    ClockedRegister reg;
    dac.model = modelDacStage;
    reg.model = modelClockedRegister;
    connectedOutputs(dac);
    connectedOutputs(reg);
    auto a = args(sr);
    dac.params[DacStageModule::RECON].setValue(0);
    dac.inputs[DacStageModule::SIGNAL].channels = 1;
    dac.inputs[DacStageModule::SIGNAL].setVoltage(1.25f);
    reg.params[ClockedRegister::CLOCK_MODE].setValue(3); // internal, not source bus
    reg.params[ClockedRegister::RATE].setValue(7);       // 128 Hz
    bool seenMask = false, seenDifference = false, seenReset = false;
    for (int i = 0; i < int(sr * .5); ++i) {
        // Both cables carry the previous sample, independent of module order.
        copyCable(dac.outputs[DacStageModule::BUS_OUT], reg.inputs[ClockedRegister::BUS_IN]);
        copyCable(reg.outputs[ClockedRegister::BUS_OUT], dac.inputs[DacStageModule::XOR_BUS]);
        if (i & 1) {
            reg.process(a);
            dac.process(a);
        } else {
            dac.process(a);
            reg.process(a);
        }
        float v = dac.outputs[DacStageModule::AUDIO].getVoltage();
        assert(std::isfinite(v));
        if (dac.reader.word) {
            seenMask = true;
            if (std::abs(v - 1.25f) > .05f)
                seenDifference = true;
        }
        if (seenMask && !dac.reader.word)
            seenReset = true;
        ++a.frame;
    }
    assert(seenMask && seenDifference && seenReset);
}

static void fullModule(float sr) {
    CzishModule m;
    m.model = modelCzish;
    connectedOutputs(m);
    auto a = args(sr);
    m.params[CzishModule::LINE_MODE].setValue(3);
    m.params[CzishModule::MOD].setValue(1);
    m.inputs[CzishModule::VOCT].channels = 8;
    m.inputs[CzishModule::GATE].channels = 8;
    for (int c = 0; c < 8; ++c) {
        m.inputs[CzishModule::VOCT].setVoltage(c / 12.f, c);
        m.inputs[CzishModule::GATE].setVoltage(10, c);
    }
    for (int i = 0; i < 2000; ++i)
        m.process(a);
    assert(m.outputs[CzishModule::POLY_AUDIO].getChannels() == 4);
    assert(m.lights[CzishModule::OVERFLOW_LIGHT].getBrightness() > 0);
    for (int n : {0, 2, 4, 6})
        assert(m.engine.chip.voice[n].ringMod);
    m.inputs[CzishModule::VIB_CV].channels = 1;
    m.inputs[CzishModule::VIB_CV].setVoltage(10);
    m.params[CzishModule::VIB_DEPTH].setValue(10);
    for (int i = 0; i < int(sr * .1); ++i)
        m.process(a);
    for (int c = 0; c < 4; ++c)
        assert(std::isfinite(m.outputs[CzishModule::POLY_AUDIO].getVoltage(c)));
    DacStageModule d;
    d.model = modelDacStage;
    connectedOutputs(d);
    d.params[DacStageModule::PROFILE].setValue(2);
    d.params[DacStageModule::RATE].setValue(1000);
    d.params[DacStageModule::AA].setValue(1);
    d.inputs[DacStageModule::SIGNAL].channels = 16;
    d.inputs[DacStageModule::BITS_CV].channels = 1;
    d.inputs[DacStageModule::BITS_CV].setVoltage(-8);
    for (int i = 0; i < 1000; ++i) {
        for (int c = 0; c < 16; ++c)
            d.inputs[DacStageModule::SIGNAL].setVoltage(float(std::sin(i * .01 + c)), c);
        d.process(a);
        for (int c = 0; c < 16; ++c)
            assert(std::isfinite(d.outputs[DacStageModule::AUDIO].getVoltage(c)));
    }
    assert(d.activeBits.load() == 4 && d.outputs[DacStageModule::AUDIO].getChannels() == 16);
}
static void persistence() {
    CzishModule a, b;
    a.model = b.model = modelCzish;
    dspemu::cz::Tone tone;
    tone.mode = 3;
    tone.mod = 2;
    tone.line[1].env[1].end = 7;
    tone.line[1].env[1].sustain = 5;
    tone.line[1].env[1].level[4] = 66;
    auto file = dspemu::cz::sysex::encode(tone);
    std::string error;
    assert(dspemu::cz::sysex::decode(file, tone, error));
    a.setTone(tone);
    json_t *j = a.toJson();
    b.fromJson(j);
    json_decref(j);
    assert(dspemu::cz::sysex::encode(b.readTone(true)) == file);
    assert(b.params[CzishModule::ENV_END + 4].getValue() == 7);
    auto *legacy = a.toJson();
    json_object_del(legacy, "data");
    auto *values = json_array();
    for (auto p : {std::pair<int, float>{CzishModule::SHAPE, 48},
                   {CzishModule::ATTACK, 20},
                   {CzishModule::RELEASE, 50}}) {
        auto *v = json_object();
        json_object_set_new(v, "id", json_integer(p.first));
        json_object_set_new(v, "value", json_real(p.second));
        json_array_append_new(values, v);
    }
    json_object_set_new(legacy, "params", values);
    b.fromJson(legacy);
    json_decref(legacy);
    assert(b.params[CzishModule::SHAPE].getValue() == 48);
    assert(b.params[CzishModule::ENV_BASE + 16 + 1].getValue() == 99);
}

int main() {
    Context context;
    context.engine = new engine::Engine;
    contextSet(&context);
    Plugin plugin;
    plugin.slug = "CZishLab";
    plugin.addModel(modelCzish);
    plugin.addModel(modelDacStage);
    rack::plugin::plugins.push_back(&plugin);
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        converter(sr);
        voice(sr);
        byteBusFeedback(sr);
        fullModule(sr);
    }
    persistence();
    rack::plugin::plugins.clear();
    std::cout << "PASS: Rack DAC and CZ ports, clocked byte mask, encoded output, Register feedback, "
                 "polyphony, C4 tuning, CV bounds and release at 44.1/48/96 kHz\n";
}
