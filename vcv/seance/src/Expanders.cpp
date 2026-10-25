// SPDX-License-Identifier: BSD-3-Clause
// The four expanders. Each finds the frame to its left (Chain.hpp) and applies one of the
// engine's operations to it in place when the frame reaches its position in the chain.
#include "plugin.hpp"
#include "Layout.hpp"
#include "Chain.hpp"
using namespace seance;

// Shared widget plumbing: narrow panel, two screws, the LINK light, the link menu entry.
template <class M>
struct ExpanderWidget : ModuleWidget {
    void setup(M* m, const char* svg, float lightX, float lightY, int lightId) {
        setModule(m);
        setPanel(createPanel(asset::plugin(pluginInstance, svg)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(lightX, lightY), m, lightId));
    }
    void appendContextMenu(Menu* menu) override {
        if (auto* m = dynamic_cast<Expander*>(module)) m->linkMenu(menu);
    }
};

// ---------------------------------------------------------------- Freeze
struct SeanceFreeze : Expander {
    enum Params { FREEZE, DRIFT, NUM_PARAMS };
    enum Inputs { GATE_IN, NUM_INPUTS };
    enum Outputs { NUM_OUTPUTS };
    enum Lights { LINK_LIGHT, FREEZE_LIGHT, NUM_LIGHTS };
    Freeze freeze;
    SeanceFreeze() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(FREEZE, 0, 1, 0, "Freeze", {"Off", "On"});
        configParam(DRIFT, 0, 1, 0, "Drift (seeded phase noise on the held frame)", "%", 0, 100);
        configInput(GATE_IN, "Freeze gate");
    }
    void process(const ProcessArgs& args) override {
        bool hold = params[FREEZE].getValue() > .5f || inputs[GATE_IN].getVoltage() >= 1.f;
        float drift = params[DRIFT].getValue();
        run(false, [&](FrameV1& f) { freeze.process(f, hold, drift); });
        lights[LINK_LIGHT].setBrightness(shownPosition.load(std::memory_order_relaxed) ? 1.f : 0.f);
        lights[FREEZE_LIGHT].setBrightness(hold ? 1.f : 0.f);
    }
};
struct SeanceFreezeWidget : ExpanderWidget<SeanceFreeze> {
    SeanceFreezeWidget(SeanceFreeze* m) {
        using namespace seance_layout::freeze;
        setup(m, "res/SeanceFreeze.svg", LIGHT_LINK_LIGHT_X, LIGHT_LINK_LIGHT_Y, SeanceFreeze::LINK_LIGHT);
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(LATCH_FREEZE_X, LATCH_FREEZE_Y), m, SeanceFreeze::FREEZE, SeanceFreeze::FREEZE_LIGHT));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_DRIFT_X, KNOB_DRIFT_Y), m, SeanceFreeze::DRIFT));
        addInput(createInputCentered<PJ301MPort>(Vec(JACK_GATE_IN_X, JACK_GATE_IN_Y), m, SeanceFreeze::GATE_IN));
    }
};
Model* modelSeanceFreeze = createModel<SeanceFreeze, SeanceFreezeWidget>("SeanceFreeze");

// ---------------------------------------------------------------- Scramble
struct SeanceScramble : Expander {
    enum Params { RANGE, PHASE, HOLD, NUM_PARAMS };
    enum Inputs { RANGE_CV, NUM_INPUTS };
    enum Outputs { NUM_OUTPUTS };
    enum Lights { LINK_LIGHT, NUM_LIGHTS };
    Scramble scramble;
    SeanceScramble() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(RANGE, 0, 1, .25f, "Range (largest offset of a peak, either way)", " semitones", 0, 12);
        configParam(PHASE, 0, 1, 0, "Phase (diffuser: constant random phase per bin)", "%", 0, 100);
        configParam(HOLD, 1, 64, 8, "Hold (frames per epoch of offsets)", " frames");
        getParamQuantity(HOLD)->snapEnabled = true;
        configInput(RANGE_CV, "Range CV (10 V spans the knob)");
    }
    void process(const ProcessArgs& args) override {
        float range = clamp(params[RANGE].getValue() + (inputs[RANGE_CV].isConnected() ? inputs[RANGE_CV].getVoltage() * .1f : 0.f), 0.f, 1.f);
        float phase = params[PHASE].getValue();
        int hold = int(std::round(params[HOLD].getValue()));
        run(false, [&](FrameV1& f) { scramble.process(f, range, phase, hold); });
        lights[LINK_LIGHT].setBrightness(shownPosition.load(std::memory_order_relaxed) ? 1.f : 0.f);
    }
};
struct SeanceScrambleWidget : ExpanderWidget<SeanceScramble> {
    SeanceScrambleWidget(SeanceScramble* m) {
        using namespace seance_layout::scramble;
        setup(m, "res/SeanceScramble.svg", LIGHT_LINK_LIGHT_X, LIGHT_LINK_LIGHT_Y, SeanceScramble::LINK_LIGHT);
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_RANGE_X, KNOB_RANGE_Y), m, SeanceScramble::RANGE));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_PHASE_X, KNOB_PHASE_Y), m, SeanceScramble::PHASE));
        addParam(createParamCentered<RoundBlackSnapKnob>(Vec(KNOB_HOLD_X, KNOB_HOLD_Y), m, SeanceScramble::HOLD));
        addInput(createInputCentered<PJ301MPort>(Vec(JACK_RANGE_CV_X, JACK_RANGE_CV_Y), m, SeanceScramble::RANGE_CV));
    }
};
Model* modelSeanceScramble = createModel<SeanceScramble, SeanceScrambleWidget>("SeanceScramble");

// ---------------------------------------------------------------- Cross
struct SeanceCross : Expander {
    enum Params { DEPTH, SMOOTH, MODE, NUM_PARAMS };
    enum Inputs { DEPTH_CV, NUM_INPUTS };
    enum Outputs { NUM_OUTPUTS };
    enum Lights { LINK_LIGHT, NUM_LIGHTS };
    Cross cross;
    SeanceCross() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(DEPTH, 0, 1, .5f, "Depth", "%", 0, 100);
        configParam(SMOOTH, 0, 1, .3f, "Smooth (width of B's envelope, up to ±127 bins)", "%", 0, 100);
        configSwitch(MODE, 0, 1, 0, "Mode", {"Multiply (A shaped by B's envelope, level restored)", "Morph (A's magnitudes towards B's)"});
        configInput(DEPTH_CV, "Depth CV (10 V spans the knob)");
    }
    void process(const ProcessArgs& args) override {
        float depth = clamp(params[DEPTH].getValue() + (inputs[DEPTH_CV].isConnected() ? inputs[DEPTH_CV].getVoltage() * .1f : 0.f), 0.f, 1.f);
        float smooth = params[SMOOTH].getValue();
        bool morph = params[MODE].getValue() > .5f;
        run(false, [&](FrameV1& f) { cross.process(f, depth, smooth, morph); });
        lights[LINK_LIGHT].setBrightness(shownPosition.load(std::memory_order_relaxed) ? 1.f : 0.f);
    }
};
struct SeanceCrossWidget : ExpanderWidget<SeanceCross> {
    SeanceCrossWidget(SeanceCross* m) {
        using namespace seance_layout::cross;
        setup(m, "res/SeanceCross.svg", LIGHT_LINK_LIGHT_X, LIGHT_LINK_LIGHT_Y, SeanceCross::LINK_LIGHT);
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_DEPTH_X, KNOB_DEPTH_Y), m, SeanceCross::DEPTH));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_SMOOTH_X, KNOB_SMOOTH_Y), m, SeanceCross::SMOOTH));
        addParam(createParamCentered<CKSS>(Vec(SWITCH_MODE_X, SWITCH_MODE_Y), m, SeanceCross::MODE));
        addInput(createInputCentered<PJ301MPort>(Vec(JACK_DEPTH_CV_X, JACK_DEPTH_CV_Y), m, SeanceCross::DEPTH_CV));
    }
};
Model* modelSeanceCross = createModel<SeanceCross, SeanceCrossWidget>("SeanceCross");

// ---------------------------------------------------------------- Wipe
struct SeanceWipe : Expander {
    enum Params { WIPE, SOFT, NUM_PARAMS };
    enum Inputs { WIPE_CV, NUM_INPUTS };
    enum Outputs { NUM_OUTPUTS };
    enum Lights { LINK_LIGHT, NUM_LIGHTS };
    SeanceWipe() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(WIPE, -1, 1, 0, "Wipe (+ clears below a rising edge, - clears above a falling one)", "%", 0, 100);
        configParam(SOFT, 0, 1, 0, "Soft (edge width, up to two octaves)", "%", 0, 100);
        configInput(WIPE_CV, "Wipe CV (±10 V spans the knob)");
    }
    void process(const ProcessArgs& args) override {
        float w = clamp(params[WIPE].getValue() + (inputs[WIPE_CV].isConnected() ? inputs[WIPE_CV].getVoltage() * .1f : 0.f), -1.f, 1.f);
        float soft = params[SOFT].getValue();
        run(false, [&](FrameV1& f) { wipe(f, w, soft); });
        lights[LINK_LIGHT].setBrightness(shownPosition.load(std::memory_order_relaxed) ? 1.f : 0.f);
    }
};
struct SeanceWipeWidget : ExpanderWidget<SeanceWipe> {
    SeanceWipeWidget(SeanceWipe* m) {
        using namespace seance_layout::wipe;
        setup(m, "res/SeanceWipe.svg", LIGHT_LINK_LIGHT_X, LIGHT_LINK_LIGHT_Y, SeanceWipe::LINK_LIGHT);
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_WIPE_X, KNOB_WIPE_Y), m, SeanceWipe::WIPE));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_SOFT_X, KNOB_SOFT_Y), m, SeanceWipe::SOFT));
        addInput(createInputCentered<PJ301MPort>(Vec(JACK_WIPE_CV_X, JACK_WIPE_CV_Y), m, SeanceWipe::WIPE_CV));
    }
};
Model* modelSeanceWipe = createModel<SeanceWipe, SeanceWipeWidget>("SeanceWipe");
