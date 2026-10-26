// SPDX-License-Identifier: BSD-3-Clause
// Gorgon's actual output engine, one-sample Rack-style cables, real adapter.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Percussion.cpp"
#include <GorgonEngine.hpp>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
Plugin* pluginInstance = nullptr;

struct ScoreSource : Module {
    gorgon::Score score;
    gorgon::Options options;
    gorgon::OutputState state;
    ScoreSource(float high) {
        config(0, 0, 4);
        for (auto& out : outputs) out.channels = 1;
        score.loop = true; score.bpm = 138; score.key = 2;
        score.mutes[2] = true;
        options.high = high;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 16; ++r) {
            auto& p = score.patterns[0];
            p.hit[c][r] = c == 0 ? r % 4 == 0 : c == 1 ? r % 8 == 4 : c == 2 ? r % 2 == 0 : r % 4 == 3;
            p.velocity[c][r] = c == 3 && r == 7 ? 0 : 127 - 2 * r;
            p.pitched[c][r] = true;
            p.midi[c][r] = 60 + c * 3 + r % 12;
        }
    }
    void process(const ProcessArgs& args) override {
        double beat = double(args.frame) / args.sampleRate * score.bpm / 60;
        auto o = state.render(score, true, beat, 1, options, args.sampleTime);
        for (int p = 0; p < 3; ++p) outputs[p].setChannels(4);
        for (int c = 0; c < 4; ++c) {
            outputs[0].setVoltage(o.gate[c], c);
            outputs[1].setVoltage(o.pitch[c], c);
            outputs[2].setVoltage(o.velocity[c] * options.high, c);
        }
        outputs[3].setVoltage(o.reset);
    }
};

static void cable(Output& output, Input& input) {
    input.channels = output.getChannels();
    for (int c = 0; c < 16; ++c) input.setVoltage(c < input.channels ? output.getVoltage(c) : 0, c);
}

static std::vector<float> run(float sr, int high, unsigned orderSeed) {
    ScoreSource source{float(high)};
    Percussion dest;
    for (auto& out : dest.outputs) out.channels = 1;
    dest.velocityHigh.store(high);
    Module::ProcessArgs a{}; a.sampleRate = sr; a.sampleTime = 1 / sr;
    std::mt19937 order(orderSeed);
    std::vector<float> trace;
    int frames = int(std::ceil(4 * 60 / 138. * sr)) + 8;
    trace.reserve(frames * 4);
    for (int i = 0; i < frames; ++i) {
        a.frame = i;
        // All cables move previous-sample data before either module processes.
        cable(source.outputs[0], dest.inputs[Percussion::TRIG_IN]);
        cable(source.outputs[1], dest.inputs[Percussion::PITCH_IN]);
        cable(source.outputs[2], dest.inputs[Percussion::VEL_IN]);
        cable(source.outputs[3], dest.inputs[Percussion::RESET_IN]);
        if (order() & 1) { source.process(a); dest.process(a); }
        else { dest.process(a); source.process(a); }
        if (i == 1) assert(dest.engine.lane[0].hits == 1);
        for (int c = 0; c < 4; ++c) {
            float x = dest.outputs[Percussion::POLY_OUT].getVoltage(c);
            assert(std::isfinite(x));
            if (c == 2) assert(x == 0);
            trace.push_back(x);
        }
    }
    assert(dest.engine.lane[0].hits == 4 && dest.engine.lane[1].hits == 2);
    assert(dest.engine.lane[2].hits == 0 && dest.engine.lane[3].hits == 3);
    assert(std::abs(dest.engine.lane[0].voice.hit.pitch) < 1e-6);
    assert(std::abs(dest.engine.lane[1].voice.hit.pitch - .25) < 1e-6);
    assert(std::abs(dest.engine.lane[3].voice.hit.pitch - 1) < 1e-6);
    assert(std::abs(dest.engine.lane[0].voice.hit.velocity - 103. / 127) < 1e-6);
    return trace;
}

int main() {
    Context context; context.engine = new engine::Engine; contextSet(&context);
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        auto ref = run(sr, 10, 1);
        assert(ref == run(sr, 10, 2));
        assert(ref == run(sr, 5, 3));
    }
    contextSet(nullptr); delete context.engine; context.engine = nullptr;
    std::cout << "PASS: actual Gorgon output engine -> Rack Percussion; one-sample cables, random processing order, four-lane kit, pitch/velocity, mute/zero velocity, 5/10 V, 44.1/48/96 kHz\n";
}
