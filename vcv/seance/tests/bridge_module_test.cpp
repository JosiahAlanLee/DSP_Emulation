// Real Rack adapters, randomized processing order and one-sample cable transport.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Seance.cpp"
#include "../src/Expanders.cpp"
#include "../src/Bridge.cpp"
#include "../../bytebus/src/Register.cpp"
#include "../../bytebus/src/Lookup.cpp"
#include "../../otislab/src/OtisSampler.cpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <random>
#include <set>

Plugin* pluginInstance = nullptr;
static Module::ProcessArgs args(float sr = 48000) {
    Module::ProcessArgs a{}; a.sampleRate = sr; a.sampleTime = 1 / sr; return a;
}
static void outputs(Module& m) { for (auto& p : m.outputs) p.channels = 1; }
static void attach(Module& left, Module& right) { left.rightExpander.module = &right; right.leftExpander.module = &left; }
static void set(Input& p, uint8_t n, bool clocked = false, bool clk = false) {
    p.channels = clocked ? 9 : 8; dspemu::bus::Writer{}.write(p, n, clocked, clk);
}
static void copy(Output& out, Input& in) {
    in.channels = out.channels;
    for (int k = 0; k < 16; ++k) in.setVoltage(k < out.channels ? out.getVoltage(k) : 0, k);
}
static uint8_t word(Output& out) {
    uint8_t n = 0; for (int b = 0; b < 8; ++b) if (out.getVoltage(b) >= 1) n |= uint8_t(1 << b); return n;
}
static void carrierAndLifecycle() {
    Seance core; SeanceBridge b; core.model = modelSeance; b.model = modelSeanceBridge; outputs(b);
    auto a = args(); b.params[SeanceBridge::SLEW].setValue(0); b.params[SeanceBridge::THRESHOLD].setValue(-72);
    assert(b.params.size() == 3 && b.inputs.size() == 1 && b.outputs.size() == 1);
    assert(core.params.size() == 4 && core.inputs.size() == 5 && core.outputs.size() == 1);
    b.process(a); assert(b.outputs[0].getChannels() == 9 && word(b.outputs[0]) == 0 && b.outputs[0].getVoltage(8) == 0);
    attach(core, b);
    auto frame = [&] {
        auto& f = core.core.frame;
        for (int k = 0; k < f.bins; ++k) { f.mag[k] = 1; f.phase[k] = 0; }
        f.stage.store(1); ++f.serial; b.process(a);
        assert(word(b.outputs[0]) == 255 && b.outputs[0].getVoltage(8) == 10);
        assert(f.stage.load() == STAGE_DONE && b.shownPosition.load() == 1);
        b.process(a); assert(word(b.outputs[0]) == 255 && b.outputs[0].getVoltage(8) == 0);
    };
    auto mask = [&](uint8_t n) {
        for (int k = 0; k < core.core.frame.bins; ++k) {
            int band = SpectralBridge::bandAt(k * 48000.f / 2048);
            assert(core.core.frame.mag[k] == ((n & (1 << band)) ? 1.f : 0.f));
        }
    };
    for (int n = 0; n < 256; ++n) { set(b.inputs[0], uint8_t(n)); frame(); mask(uint8_t(n)); }
    set(b.inputs[0], 0xa5, true, false); frame(); mask(0);  // no first edge, no word
    set(b.inputs[0], 0xa5, true, true); frame(); mask(0xa5);
    set(b.inputs[0], 0x5a, true, false);
    for (int n = 0; n < 100; ++n) { frame(); mask(0xa5); }  // stopped clock holds across FFT frames
    set(b.inputs[0], 0x5a, true, true); frame(); mask(0x5a);
    // Reserved channels do not affect the word or clock mode.
    b.inputs[0].channels = 16; for (int k = 9; k < 16; ++k) b.inputs[0].setVoltage(10, k);
    frame(); mask(0x5a);
    set(b.inputs[0], 255); b.inputs[0].channels = 1; frame(); mask(1);  // mono is one lane
    b.inputs[0].setVoltage(NAN); frame(); mask(0);
    b.inputs[0].channels = 0; frame(); mask(255);
    // Two rising edges before the next frame coalesce into the latest held word.
    set(b.inputs[0], 17, true, true); b.process(a);
    set(b.inputs[0], 0, true, false); b.process(a);
    set(b.inputs[0], 42, true, true); b.process(a); frame(); mask(42);
    set(b.inputs[0], 0); b.processBypass(a);
    assert(word(b.outputs[0]) == 0 && b.outputs[0].getVoltage(8) == 0);
    for (int k = 0; k < core.core.frame.bins; ++k) core.core.frame.mag[k] = 1;
    core.core.frame.stage.store(1); b.processBypass(a);
    assert(core.core.frame.stage.load() == STAGE_DONE);
    for (int k = 0; k < core.core.frame.bins; ++k) assert(core.core.frame.mag[k] == 1);
    frame(); mask(0);
    core.rightExpander.module = nullptr; b.leftExpander.module = nullptr; b.process(a);
    assert(word(b.outputs[0]) == 0 && b.shownPosition.load() == 0 && b.outputs[0].getVoltage(8) == 0);
    // Restoring a patch saves knobs, never an input latch or activity frame.
    b.params[SeanceBridge::HYSTERESIS].setValue(13); auto* saved = b.toJson();
    SeanceBridge fresh; fresh.model = modelSeanceBridge; fresh.fromJson(saved); json_decref(saved);
    assert(fresh.params[SeanceBridge::HYSTERESIS].getValue() == 13 && fresh.bridge.activity == 0 && fresh.reader.word == 0);
    for (float g : fresh.bridge.gain) assert(g == 1);
}

static void row(float sr, int size) {
    Seance c; SeanceFreeze fz; SeanceBridge b; SeanceScramble sc; SeanceCross cr; SeanceWipe wp;
    c.model = modelSeance; fz.model = modelSeanceFreeze; b.model = modelSeanceBridge;
    sc.model = modelSeanceScramble; cr.model = modelSeanceCross; wp.model = modelSeanceWipe;
    attach(c, fz); attach(fz, b); attach(b, sc); attach(sc, cr); attach(cr, wp);
    outputs(c); outputs(b); c.inputs[Seance::A_IN].channels = 1;
    c.params[Seance::SIZE].setValue(float(size)); sc.params[SeanceScramble::RANGE].setValue(0);
    Module::SampleRateChangeEvent e{}; e.sampleRate = sr; e.sampleTime = 1 / sr; c.onSampleRateChange(e);
    Core plain; plain.prepare(sr); plain.setSize(size); CoreParams cp; cp.hasB = false;
    Freeze plainFreeze; Scramble plainScramble; Cross plainCross;
    auto a = args(sr); std::mt19937 random(302); int pulses = 0, referencePulses = 0; bool prevClock = false;
    for (int t = 0; t < 10 * sizeOf(size); ++t) {
        float x = .2f * float(std::sin(2 * PI * 1000 * t / sr));
        c.inputs[Seance::A_IN].setVoltage(x * 10);
        Module* modules[] = {&c, &fz, &b, &sc, &cr, &wp}; std::shuffle(std::begin(modules), std::end(modules), random);
        for (auto* m : modules) m->process(a);
        float expected = plain.tick(c.inputs[Seance::A_IN].getVoltage() * .1f, 0, cp, true) * 10;
        referencePulses += serve(plain.frame, 1, true, false, [&](FrameV1& f) {
            plainFreeze.process(f, false, 0); plainScramble.process(f, 0, 0, 8);
            plainCross.process(f, .5f, .3f, false); wipe(f, 0, 0);
        });
        assert(c.outputs[0].getVoltage() == expected);  // an open bridge adds zero audio change
        bool clk = b.outputs[0].getVoltage(8) == 10; assert(!(clk && prevClock)); pulses += clk; prevClock = clk;
        ++a.frame;
    }
    assert(c.core.stalls == 0 && c.core.frame.length.load() == 5 && b.shownPosition.load() == 2);
    assert(pulses >= referencePulses - 1 && pulses <= referencePulses);
    // Fully closed gain settles, but its pre-mask activity still reports the tone.
    set(b.inputs[0], 0); double closed = 0;
    for (int t = 0; t < int(sr * .6); ++t) {
        c.inputs[Seance::A_IN].setVoltage(2 * float(std::sin(2 * PI * 1000 * t / sr)));
        for (Module* m : {(Module*)&c, (Module*)&fz, (Module*)&b, (Module*)&sc, (Module*)&cr, (Module*)&wp}) m->process(a);
        if (t > sr * .5) closed += std::abs(c.outputs[0].getVoltage());
        ++a.frame;
    }
    assert(closed < .001 && (word(b.outputs[0]) & 0x18) != 0 && c.core.stalls == 0);
    // An upstream wipe is visible to the detector; moving it after Bridge changes that.
    wp.params[SeanceWipe::WIPE].setValue(1);
    c.rightExpander.module = &wp; wp.leftExpander.module = &c; attach(wp, b); b.rightExpander.module = nullptr;
    for (int t = 0; t < 3 * sizeOf(size); ++t) { c.process(a); wp.process(a); b.process(a); ++a.frame; }
    assert(word(b.outputs[0]) == 0);
}

static void example(float sr, std::shared_ptr<const dspemu::lookup::Library> library) {
    auto* patch = json_load_file("../examples/OTIS-spectral-feedback.vcv", JSON_REJECT_DUPLICATES, nullptr); assert(patch);
    auto o = std::make_unique<OtisSampler>(); Seance c; SeanceBridge b; ClockedRegister r; ByteLookup l(library);
    o->model = modelOtisSampler; c.model = modelSeance; b.model = modelSeanceBridge; r.model = modelClockedRegister; l.model = modelLookup;
    Module* modules[] = {nullptr, o.get(), &c, &b, &r, &l};
    auto* saved = json_object_get(patch, "modules"); assert(json_array_size(saved) == 6);
    Module::SampleRateChangeEvent e{}; e.sampleRate = sr; e.sampleTime = 1 / sr;
    for (int i = 1; i <= 5; ++i) { modules[i]->fromJson(json_array_get(saved, i - 1)); outputs(*modules[i]); modules[i]->onSampleRateChange(e); }
    assert(json_number_value(json_array_get(json_object_get(json_array_get(saved, 1), "pos"), 0)) + 10 ==
        json_number_value(json_array_get(json_object_get(json_array_get(saved, 2), "pos"), 0)));  // adjacency saved correctly
    attach(c, b);
    auto* cables = json_object_get(patch, "cables");
    auto a = args(sr); std::set<size_t> frames; std::set<uint8_t> masks, detected;
    std::mt19937 random(7703); double wet = 0, dry = 0; int pulses = 0;
    for (int t = 0; t < int(sr * 5); ++t) {
        // Snapshot every cable before stepping any module: exactly one engine sample per cable.
        for (size_t k = 0; k < json_array_size(cables); ++k) {
            auto* cable = json_array_get(cables, k);
            int from = int(json_integer_value(json_object_get(cable, "outputModuleId")));
            int to = int(json_integer_value(json_object_get(cable, "inputModuleId")));
            int out = int(json_integer_value(json_object_get(cable, "outputId")));
            int in = int(json_integer_value(json_object_get(cable, "inputId")));
            if (to < 6) copy(modules[from]->outputs[out], modules[to]->inputs[in]);
        }
        o->params[OtisSampler::PLAY].setValue(t == 16 ? 1 : 0);
        Module* order[] = {o.get(), &c, &b, &r, &l}; std::shuffle(std::begin(order), std::end(order), random);
        for (auto* m : order) m->process(a);
        if (t > sr) {
            frames.insert(o->engine.windowFor(0).begin / 256); masks.insert(word(l.outputs[0])); detected.insert(word(b.outputs[0]));
            float w = c.outputs[0].getVoltage(), d = o->outputs[1].getVoltage();
            assert(std::isfinite(w) && std::isfinite(d)); wet += w * w; dry += d * d;
            pulses += b.outputs[0].getVoltage(8) == 10;
        }
        ++a.frame;
    }
    std::cout << "  example " << sr << " Hz: frames=" << frames.size() << " masks=" << masks.size()
        << " detections=" << detected.size() << " wet RMS=" << std::sqrt(wet / (sr * 4)) << " V\n";
    assert(o->engine.sample.stereo && frames.size() >= 2 && masks.size() >= 2 && detected.size() >= 2);
    assert(wet > 1 && dry > 1 && pulses > 100 && c.core.stalls == 0);
    json_decref(patch);
}
int main() {
    std::cout << std::unitbuf;
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Plugin seancePlugin; seancePlugin.slug = "SeanceLab"; seancePlugin.version = "2.1.0";
    for (auto* m : {modelSeance, modelSeanceFreeze, modelSeanceScramble, modelSeanceCross, modelSeanceWipe, modelSeanceBridge}) seancePlugin.addModel(m);
    Plugin bytePlugin; bytePlugin.slug = "ByteBusLab"; bytePlugin.version = "2.2.0"; bytePlugin.addModel(modelClockedRegister); bytePlugin.addModel(modelLookup);
    Plugin otisPlugin; otisPlugin.slug = "OtisLab"; otisPlugin.version = "2.8.0"; otisPlugin.addModel(modelOtisSampler);
    rack::plugin::plugins = {&seancePlugin, &bytePlugin, &otisPlugin};
    carrierAndLifecycle();
    auto library = std::make_shared<dspemu::lookup::Library>(dspemu::lookup::Library::load("../../bytebus/res/lookup-v1.bin"));
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        for (int size = 0; size < SIZES; ++size) row(sr, size);
        example(sr, library);
    }
    rack::plugin::plugins.clear();
    std::cout << "PASS: Bridge native ports, all masks, clock holds/coalescing, mono/invalid/reserved lines, bypass, unlinked/persistence, random expander order, unchanged open-row audio and audible saved OTIS feedback at three rates\n";
}
