// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Percussion.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct Percussion : Module {
    enum Params { MACHINE, TUNE, DECAY, COLOR, CONTOUR, DRIVE, BITS, RATE, TAP, NUM_PARAMS };
    enum Inputs { TRIG_IN, PITCH_IN, VEL_IN, MACHINE_IN, DECAY_IN, COLOR_IN, CONTOUR_IN,
        DRIVE_IN, BITS_IN, RATE_IN, CHOKE_IN, RESET_IN, NUM_INPUTS };
    enum Outputs { POLY_OUT, LEFT_OUT, RIGHT_OUT, NUM_OUTPUTS };
    enum Lights { HIT_LIGHT, NUM_LIGHTS };
    drumlab::Engine engine;
    std::array<drumlab::Edge, drumlab::channels> triggers, chokes;
    drumlab::Edge resetEdge, tapEdge;
    std::atomic<int> velocityHigh{10};
    int previousChannels = 0;

    Percussion() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(MACHINE, 0, 6, 6, "Machine", {"Kick", "Snare", "Hat", "FM", "Membrane", "Clap", "Four-lane kit"});
        configParam(TUNE, -48, 48, 0, "Tuning relative to model base", " semitones");
        configParam(DECAY, std::log2(.005f), 2, std::log2(.35f), "Decay to -60 dB", " s", 2.f);
        configParam(COLOR, 0, 1, .5, "Color", "%", 0, 100);
        configParam(CONTOUR, 0, 1, .5, "Contour", "%", 0, 100);
        configParam(DRIVE, 0, 1, .15, "Drive", "%", 0, 100);
        configParam(BITS, 4, 16, 16, "Bit depth", " bits");
        getParamQuantity(BITS)->snapEnabled = true;
        configParam(RATE, 0, 6, 0, "Rate reduction (held host samples)", " samples", 2.f);
        configButton(TAP, "Audition channel 1");
        configInput(TRIG_IN, "Triggers, up to 16 channels (2 V rising edge)");
        configInput(PITCH_IN, "Pitch offset, 1 V/oct relative to model base");
        configInput(VEL_IN, "Velocity, 0-10 V or 0-5 V (menu); unpatched = full");
        configInput(MACHINE_IN, "Machine, absolute 0-5 V, 1 V per model; overrides selector");
        configInput(DECAY_IN, "Decay offset, 1 V doubles time");
        configInput(COLOR_IN, "Color offset, 10 V spans range");
        configInput(CONTOUR_IN, "Contour offset, 10 V spans range");
        configInput(DRIVE_IN, "Drive offset, 10 V spans range");
        configInput(BITS_IN, "Bit-depth offset, 1 V adds 1.2 bits");
        configInput(RATE_IN, "Rate-reduction offset, 1 V doubles hold time");
        configInput(CHOKE_IN, "Choke triggers, mono broadcasts, poly selects channels");
        configInput(RESET_IN, "Reset voices and repeatable noise sequence; a simultaneous hit plays");
        configOutput(POLY_OUT, "Individual audio, one channel per trigger lane, nominal +/-5 V");
        configOutput(LEFT_OUT, "Stereo mix left");
        configOutput(RIGHT_OUT, "Stereo mix right");
    }
    double cv(int input, int channel) {
        int n = inputs[input].getChannels();
        if (n == 0 || (n > 1 && channel >= n)) return 0;
        return drumlab::bounded(inputs[input].getVoltage(n == 1 ? 0 : channel), -100, 100, 0);
    }
    double knob(int param, double lo, double hi, double fallback) {
        return drumlab::bounded(params[param].getValue(), lo, hi, fallback);
    }
    drumlab::Hit capture(int channel) {
        drumlab::Hit p;
        int selected = int(std::round(knob(MACHINE, 0, 6, 6)));
        if (inputs[MACHINE_IN].isConnected()) selected = int(std::round(std::clamp(cv(MACHINE_IN, channel), 0., 5.)));
        else if (selected == 6) selected = channel % 4;
        p.machine = drumlab::Machine(selected);
        p.pitch = knob(TUNE, -48, 48, 0) / 12 + cv(PITCH_IN, channel);
        p.decay = std::exp2(std::clamp(knob(DECAY, -8, 2, std::log2(.35)) + cv(DECAY_IN, channel), std::log2(.005), 2.));
        p.color = knob(COLOR, 0, 1, .5) + cv(COLOR_IN, channel) / 10;
        p.contour = knob(CONTOUR, 0, 1, .5) + cv(CONTOUR_IN, channel) / 10;
        p.drive = knob(DRIVE, 0, 1, .15) + cv(DRIVE_IN, channel) / 10;
        p.bits = knob(BITS, 4, 16, 16) + 1.2 * cv(BITS_IN, channel);
        p.reduction = std::exp2(std::clamp(knob(RATE, 0, 6, 0) + cv(RATE_IN, channel), 0., 6.));
        p.velocity = inputs[VEL_IN].isConnected() ? cv(VEL_IN, channel) / velocityHigh.load(std::memory_order_relaxed) : 1;
        return p.sanitized();
    }
    void process(const ProcessArgs& args) override {
        if (resetEdge.process(cv(RESET_IN, 0))) {
            engine.reset();
            // Keep input gate state: resetting while a gate is held must not
            // invent a hit. A new edge on this sample is still processed below.
        }
        bool tap = tapEdge.process(params[TAP].getValue() * 10);
        int count = std::clamp(inputs[TRIG_IN].getChannels(), 1, drumlab::channels);
        for (int c = count; c < previousChannels; ++c) {
            engine.clear(c); triggers[c] = {}; chokes[c] = {};
        }
        previousChannels = count;
        outputs[POLY_OUT].setChannels(count);
        double left = 0, right = 0;
        bool anyHit = false;
        for (int c = 0; c < count; ++c) {
            // A monophonic trigger cable drives channel 1. Unlike parameter CV,
            // missing trigger channels are never broadcast into extra hits.
            bool hit = triggers[c].process(c < inputs[TRIG_IN].getChannels() ? inputs[TRIG_IN].getVoltage(c) : 0) || (tap && c == 0);
            bool choke = chokes[c].process(cv(CHOKE_IN, c));
            if (choke) engine.choke(c);
            else if (hit) { engine.trigger(c, capture(c), args.sampleRate); anyHit = true; }
            double audio = engine.tick(c, args.sampleRate);
            outputs[POLY_OUT].setVoltage(float(5 * audio), c);
            static constexpr double pans[4] = {0, 0, -.45, .45};
            double pan = pans[c % 4];
            double angle = (pan + 1) * drumlab::pi / 4;
            left += audio * std::cos(angle); right += audio * std::sin(angle);
        }
        // Fixed for the cable's channel count, so rests don't pump the mix.
        double gain = 5 / std::sqrt(double(count));
        outputs[LEFT_OUT].setVoltage(float(left * gain));
        outputs[RIGHT_OUT].setVoltage(float(right * gain));
        lights[HIT_LIGHT].setBrightnessSmooth(anyHit ? 1 : 0, args.sampleTime);
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        engine.reset(); triggers = {}; chokes = {}; resetEdge = {}; tapEdge = {};
        previousChannels = 0; velocityHigh.store(10);
    }
    json_t* dataToJson() override {
        return json_pack("{s:i}", "velocityHigh", velocityHigh.load());
    }
    void dataFromJson(json_t* root) override {
        json_t* v = json_object_get(root, "velocityHigh");
        velocityHigh.store(json_is_integer(v) && json_integer_value(v) == 5 ? 5 : 10);
    }
};

struct PercussionWidget : ModuleWidget {
    explicit PercussionWidget(Percussion* m) {
        using namespace drumlab_layout::percussion;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(m); setPanel(createPanel(asset::plugin(pluginInstance, "res/Percussion.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(WIDTH-15, 365)));
        auto* machine = createParam<dspemu::panel::Choice>(POS(MACHINE), m, Percussion::MACHINE);
        machine->box.size = Vec(MACHINE_W,MACHINE_H); machine->previewIndex = 6;
        machine->labels = {"KICK","SNARE","HAT","FM","MEMBRANE","CLAP","4-LANE KIT"}; addParam(machine);
        const Vec knobs[] = {POS(MACHINE),POS(TUNE),POS(DECAY),POS(COLOR),POS(CONTOUR),POS(DRIVE),POS(BITS),POS(RATE)};
        for (int k = 1; k < 8; ++k) {
            Vec p = knobs[k];
            if (k == Percussion::BITS)
                addParam(createParamCentered<RoundBlackSnapKnob>(p, m, k));
            else addParam(createParamCentered<RoundBlackKnob>(p, m, k));
        }
        addParam(createParamCentered<VCVButton>(POS(TAP), m, Percussion::TAP));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(HIT), m, Percussion::HIT_LIGHT));
        const Vec ins[] = {POS(TRIG),POS(PITCH),POS(VEL),POS(MACHINE_CV),POS(DECAY_CV),POS(COLOR_CV),
                           POS(CONTOUR_CV),POS(DRIVE_CV),POS(BITS_CV),POS(RATE_CV),POS(CHOKE),POS(RESET)};
        const Vec outs[] = {POS(POLY),POS(LEFT),POS(RIGHT)};
        for (int k = 0; k < Percussion::NUM_INPUTS; ++k)
            addInput(createInputCentered<PJ301MPort>(ins[k], m, k));
        for (int k = 0; k < 3; ++k)
            addOutput(createOutputCentered<DarkPJ301MPort>(outs[k], m, k));
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<Percussion*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem("Velocity full scale is 5 V", "", [m]() { return m->velocityHigh.load() == 5; },
            [m](bool on) { m->velocityHigh.store(on ? 5 : 10); }));
        menu->addChild(createMenuLabel("All sound controls are captured on each hit."));
        menu->addChild(createMenuLabel("Reset restarts the repeatable noise sequence."));
    }
};
Model* modelPercussion = createModel<Percussion, PercussionWidget>("Percussion");
