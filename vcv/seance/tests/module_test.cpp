// Drives the real Séance modules through Rack's Module API (no GUI): a core with a row of
// expanders wired as the engine would wire them, stepped in varying order; link discovery
// and positions, in-place processing down the chain, bypass, detaching, RESET, I/O scaling,
// parameter displays and typed entry.
// Host internals first (before rack.hpp marks them private) to provide a Rack context.
#include <engine/Engine.hpp>
#include <context.hpp>
#include "../src/Seance.cpp"
#include "../src/Expanders.cpp"
#include <cassert>
#include <iostream>
#include <random>
Plugin* pluginInstance = nullptr;

// Places `right` directly to the right of `left`, as Rack does when modules touch.
static void attach(Module& left, Module& right) {
    left.rightExpander.module = &right; left.rightExpander.moduleId = right.id;
    right.leftExpander.module = &left; right.leftExpander.moduleId = left.id;
}
static void detach(Module& left, Module& right) {
    left.rightExpander.module = nullptr; left.rightExpander.moduleId = -1;
    right.leftExpander.module = nullptr; right.leftExpander.moduleId = -1;
}
static float rms(const std::vector<float>& v, size_t from) {
    double e = 0; for (size_t i = from; i < v.size(); i++) e += double(v[i]) * v[i]; return float(std::sqrt(e / (v.size() - from)));
}

int main() {
    std::cout << std::unitbuf;
    // ParamQuantity::getValue() reads through APP->engine, so a minimal host context is needed.
    Context context; context.engine = new engine::Engine; contextSet(&context);
    Module::ProcessArgs args; args.sampleRate = 48000; args.sampleTime = 1.f / 48000; args.frame = 0;

    Seance core; SeanceFreeze fz; SeanceScramble sc; SeanceCross cr; SeanceWipe wp;
    core.model = modelSeance; fz.model = modelSeanceFreeze; sc.model = modelSeanceScramble; cr.model = modelSeanceCross; wp.model = modelSeanceWipe;
    core.id = 1; fz.id = 2; sc.id = 3; cr.id = 4; wp.id = 5;
    for (Module* m : {(Module*)&core, (Module*)&fz, (Module*)&sc, (Module*)&cr, (Module*)&wp}) for (auto& o : m->outputs) o.channels = 1;
    Module::SampleRateChangeEvent sr; sr.sampleRate = 48000; sr.sampleTime = 1.f / 48000; core.onSampleRateChange(sr);

    // The published frame is valid and sits on the core's own right-hand slots.
    assert(seance::validFrame(core.rightExpander.producerMessage) && core.rightExpander.producerMessage == core.rightExpander.consumerMessage);

    // Row: core | Freeze | Scramble | Cross | Wipe, stepped in a different order every sample.
    attach(core, fz); attach(fz, sc); attach(sc, cr); attach(cr, wp);
    Module* row[5] = {&core, &fz, &sc, &cr, &wp};
    std::mt19937 rng(3);
    auto step = [&](int n, std::vector<float>* out = nullptr) {
        for (int i = 0; i < n; i++) {
            args.frame++;
            int order[5] = {0, 1, 2, 3, 4}; std::shuffle(order, order + 5, rng);
            for (int k : order) row[k]->process(args);
            if (out) out->push_back(core.outputs[Seance::OUT].getVoltage());
        }
    };
    // A 1 kHz tone on A (polyphonic: two channels summed), silence on B.
    core.inputs[Seance::A_IN].channels = 2;
    long t = 0;
    auto feed = [&](int n, std::vector<float>* out = nullptr, float amp = 2.5f) {
        for (int i = 0; i < n; i++, t++) {
            float v = amp * float(std::sin(2 * PI * 1000 * t / 48000.0));
            core.inputs[Seance::A_IN].setVoltage(v, 0); core.inputs[Seance::A_IN].setVoltage(v, 1);
            step(1, out);
        }
    };
    std::vector<float> out;
    feed(48000, &out);
    int D = core.core.latency(); assert(D == 2048 - 1 + 64);
    // Everything at defaults but Scramble (range 3 semitones): still a steady signal of about the input level.
    assert(fz.shownPosition == 1 && sc.shownPosition == 2 && cr.shownPosition == 3 && wp.shownPosition == 4);
    assert(core.core.frame.length.load() == 4 && core.lights[Seance::CHAIN_LIGHT].getBrightness() == 1);
    assert(core.core.stalls == 0 && core.lights[Seance::STALL_LIGHT].getBrightness() < .01f);
    float level = rms(out, out.size() - 24000);
    assert(level > 5.f / std::sqrt(2.f) * .5f && level < 5.f / std::sqrt(2.f) * 1.5f);  // 2.5 V x 2 channels = 5 V peak

    // Wipe at +100 % clears everything below Nyquist: the tone disappears, and comes back at 0.
    wp.params[SeanceWipe::WIPE].setValue(1);
    out.clear(); feed(24000, &out);
    assert(rms(out, 12000) < 1e-3f);
    wp.params[SeanceWipe::WIPE].setValue(0);
    out.clear(); feed(24000, &out);
    assert(rms(out, 12000) > 1.f);
    // Wipe CV: +10 V does the same as the knob at +100 %.
    wp.inputs[SeanceWipe::WIPE_CV].channels = 1; wp.inputs[SeanceWipe::WIPE_CV].setVoltage(10);
    out.clear(); feed(24000, &out);
    assert(rms(out, 12000) < 1e-3f);
    // A bypassed Wipe passes the frame on untouched: processBypass is what Rack calls then.
    out.clear();
    for (int i = 0; i < 24000; i++, t++) {
        float v = 2.5f * float(std::sin(2 * PI * 1000 * t / 48000.0));
        core.inputs[Seance::A_IN].setVoltage(v, 0); core.inputs[Seance::A_IN].setVoltage(v, 1);
        args.frame++;
        for (Module* m : {(Module*)&core, (Module*)&fz, (Module*)&sc, (Module*)&cr}) m->process(args);
        wp.processBypass(args);
        out.push_back(core.outputs[Seance::OUT].getVoltage());
    }
    assert(rms(out, 12000) > 1.f && core.core.stalls == 0);
    wp.inputs[SeanceWipe::WIPE_CV].setVoltage(0); wp.inputs[SeanceWipe::WIPE_CV].channels = 0;

    // Freeze through the gate input: hold the frame, silence the input, the tone continues.
    fz.inputs[SeanceFreeze::GATE_IN].channels = 1; fz.inputs[SeanceFreeze::GATE_IN].setVoltage(10);
    feed(4096);
    out.clear(); feed(48000, &out, 0.f);
    assert(rms(out, 24000) > 1.f && fz.lights[SeanceFreeze::FREEZE_LIGHT].getBrightness() == 1);
    fz.inputs[SeanceFreeze::GATE_IN].setVoltage(0);
    out.clear(); feed(24000, &out, 0.f);
    assert(rms(out, 12000) < 1e-3f);

    // Detach Wipe: Cross becomes last, the chain reports three, Wipe is unlinked.
    detach(cr, wp);
    feed(8192);
    assert(wp.shownPosition == 0 && core.core.frame.length.load() == 3 && core.core.stalls == 0);
    // Detach everything: the core runs alone, no stalls.
    detach(core, fz);
    feed(8192);
    assert(core.lights[Seance::CHAIN_LIGHT].getBrightness() == 0 && fz.shownPosition == 0 && core.core.stalls == 0);
    // A module that is not Séance to the right is not a chain.
    attach(core, fz); attach(fz, sc); attach(sc, cr); attach(cr, wp);

    // Cross with a tone on B shapes A (noise) to a band around 1 kHz: output present and finite.
    core.inputs[Seance::B_IN].channels = 1;
    cr.params[SeanceCross::DEPTH].setValue(1);
    std::mt19937 nrng(9); std::uniform_real_distribution<float> u(-2.5f, 2.5f);
    out.clear();
    for (int i = 0; i < 48000; i++, t++) {
        core.inputs[Seance::A_IN].setVoltage(u(nrng), 0); core.inputs[Seance::A_IN].setVoltage(u(nrng), 1);
        core.inputs[Seance::B_IN].setVoltage(2.5f * float(std::sin(2 * PI * 1000 * t / 48000.0)));
        step(1, &out);
    }
    for (float v : out) assert(std::isfinite(v));
    assert(rms(out, 24000) > .1f);
    cr.params[SeanceCross::DEPTH].setValue(.5f); core.inputs[Seance::B_IN].channels = 0;

    // RESET: trigger input restarts the frame count; the button does the same.
    core.inputs[Seance::RESET_IN].channels = 1; core.inputs[Seance::RESET_IN].setVoltage(10); feed(1);
    assert(core.core.serial == 0);
    core.inputs[Seance::RESET_IN].setVoltage(0); feed(4096); assert(core.core.serial >= 4);
    core.params[Seance::RESET].setValue(1); feed(1); core.params[Seance::RESET].setValue(0);
    assert(core.core.serial == 0);

    // Seed: knob plus CV at 1 V per 100, wrapping at 1000.
    core.params[Seance::SEED].setValue(950); core.inputs[Seance::SEED_CV].channels = 1; core.inputs[Seance::SEED_CV].setVoltage(1);
    feed(1); assert(core.core.frame.seed == 50 || core.core.frame.seed == 0);  // the frame carries the seed from its analysis
    feed(2048); assert(core.core.frame.seed == 50);
    core.inputs[Seance::SEED_CV].channels = 0; core.params[Seance::SEED].setValue(0);

    // Size: display, typed entry to the nearest size, and the latency follows at the next frame.
    auto* sq = core.paramQuantities[Seance::SIZE];
    assert(sq->getDisplayValueString() == "2048");
    sq->setDisplayValueString("1000"); assert(sq->getDisplayValueString() == "1024");
    sq->setDisplayValueString("5000"); assert(sq->getDisplayValueString() == "4096");
    feed(8192); assert(core.core.latency() == 4096 - 1 + 64 && core.core.stalls == 0);
    sq->setDisplayValueString("512"); feed(8192); assert(core.core.latency() == 512 - 1 + 64);
    for (int i = 0; i < 1100; i++) feed(1);
    assert(core.shownLatency.load() == 575);
    // Dry/wet fully dry: the output is the input delayed by the latency, exactly.
    core.params[Seance::MIX].setValue(0);
    std::vector<float> in2, out2;
    for (int i = 0; i < 4000; i++, t++) {
        float v = 2.5f * float(std::sin(2 * PI * 1000 * t / 48000.0));
        core.inputs[Seance::A_IN].setVoltage(v, 0); core.inputs[Seance::A_IN].setVoltage(v, 1);
        in2.push_back(2 * v); step(1, &out2);
    }
    for (size_t i = 575 + 600; i < out2.size(); i++) assert(std::abs(out2[i] - in2[i - 575]) < 1e-4f);
    core.params[Seance::MIX].setValue(1);

    // Expander parameter displays.
    assert(sc.paramQuantities[SeanceScramble::RANGE]->getDisplayValueString() == "3");
    assert(sc.paramQuantities[SeanceScramble::HOLD]->getDisplayValueString() == "8");
    sc.paramQuantities[SeanceScramble::HOLD]->setDisplayValueString("3.4"); assert(sc.paramQuantities[SeanceScramble::HOLD]->getDisplayValueString() == "3");
    assert(cr.paramQuantities[SeanceCross::MODE]->getDisplayValueString().rfind("Multiply", 0) == 0);

    std::cout << "PASS: Séance modules: link discovery and positions, chain in random step order, wipe and CV, bypass, freeze gate, detaching, cross, reset, seed, size and latency, dry path, displays\n";
}
