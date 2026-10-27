// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Lattice.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteLattice : Module {
    static constexpr int STEPS = 16;
    enum Params { STEP, RANGE = STEP + STEPS, SOURCE, JUMP, RATE, SEED, LOAD_BUTTON, SLEW_A, SLEW_B, NUM_PARAMS };
    enum Inputs { H_CLOCK_IN, V_CLOCK_IN, DIR_H_IN, DIR_V_IN, JUMP_H_IN, JUMP_V_IN, RESET_IN, BUS_IN,
        SLEW_A_CV_IN, SLEW_B_CV_IN, NUM_INPUTS };
    enum Outputs { CV_OUT, INV_OUT, SLEW_A_OUT, SLEW_B_OUT, BUS_OUT, CLOCK_OUT, STEPS_A_OUT, STEPS_B_OUT, NUM_OUTPUTS };
    enum Lights { STEP_LIGHT, CLOCK_LIGHT = STEP_LIGHT + STEPS, RESET_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    dspemu::lattice::Lattice state;
    dspemu::lattice::Slew slewA, slewB;
    std::atomic<unsigned> published{0};   // the state word, saved as "state"

    ByteLattice() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (int k = 0; k < STEPS; ++k) {
            configParam(STEP + k, 0, 1, 0, string::f("Step %d (row %d, column %d)", k + 1, k / 4 + 1, k % 4 + 1), "%", 0, 100);
            configLight(STEP_LIGHT + k, string::f("Step %d", k + 1));
        }
        configSwitch(RANGE, 0, 2, 0, "CV range", {"0-10 V", "0-5 V", "±5 V"});
        configSwitch(SOURCE, 0, 3, 0, "H clock source",
            {"Auto (H CLOCK jack, else internal)", "H CLOCK jack", "Internal", "Engine rate (unclocked bus)"});
        configSwitch(JUMP, 0, 1, 0, "Jump", {"Follow (the settled jam wiring; held jumps follow)", "Load (the jam values, once per rising edge)"});
        configParam(RATE, -3, 11, 3, "Internal H clock", " Hz", 2.f);
        configParam(SEED, 0, 255, 0, "Seed state (V x 16 + H)");
        getParamQuantity(SEED)->snapEnabled = true;
        configButton(LOAD_BUTTON, "Load seed and restart internal clock");
        configParam(SLEW_A, 0, 1, 0, "Slew A time", " ms", dspemu::lattice::Slew::MAX_TAU / dspemu::lattice::Slew::MIN_TAU,
            1000 * dspemu::lattice::Slew::MIN_TAU);
        configParam(SLEW_B, 0, 1, 0, "Slew B time", " ms", dspemu::lattice::Slew::MAX_TAU / dspemu::lattice::Slew::MIN_TAU,
            1000 * dspemu::lattice::Slew::MIN_TAU);
        configInput(H_CLOCK_IN, "H clock (rising edge; the column counter)");
        configInput(V_CLOCK_IN, "V clock (rising edge; the row counter; unpatched, H's wrap)");
        configInput(DIR_H_IN, "H direction gate (down while high)");
        configInput(DIR_V_IN, "V direction gate (down while high)");
        configInput(JUMP_H_IN, "Jump H");
        configInput(JUMP_V_IN, "Jump V");
        configInput(RESET_IN, "Reset (loads the seed; takes priority over the H clock)");
        configInput(BUS_IN, "Jump targets byte bus (JUMP H takes bits 0-3, JUMP V bits 4-7)");
        configInput(SLEW_A_CV_IN, "Slew A CV (each volt halves the time)");
        configInput(SLEW_B_CV_IN, "Slew B CV (each volt halves the time)");
        configOutput(CV_OUT, "CV (the selected step knob)");
        configOutput(INV_OUT, "Inverted CV");
        configOutput(SLEW_A_OUT, "Slew A");
        configOutput(SLEW_B_OUT, "Slew B");
        configOutput(BUS_OUT, "State byte bus (V x 16 + H; 9 channels when clocked, else 8)");
        configOutput(CLOCK_OUT, "State clock");
        configOutput(STEPS_A_OUT, "Step gates 1-8 (8-channel polyphonic)");
        configOutput(STEPS_B_OUT, "Step gates 9-16 (8-channel polyphonic)");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); reader.reset(); state.clear(); slewA.reset(); slewB.reset(); published.store(0);
    }
    void process(const ProcessArgs& args) override {
        namespace lt = dspemu::lattice;
        using lt::ClockMode;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        auto gate = [&](int id) { return inputs[id].isConnected() ? inputs[id].getVoltage() : 0.f; };
        static constexpr ClockMode modes[] = {ClockMode::Auto, ClockMode::Jack, ClockMode::Internal, ClockMode::Engine};
        lt::Parameters p;
        p.mode = modes[integer(SOURCE, 0, 3, 0)];
        p.jump = integer(JUMP, 0, 1, 0) == 1 ? lt::JumpMode::Load : lt::JumpMode::Follow;
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.seed = uint8_t(integer(SEED, 0, 255, 0));
        lt::Inputs in;
        in.bus = reader.process(inputs[BUS_IN]);
        in.clock.clockConnected = inputs[H_CLOCK_IN].isConnected();
        in.clock.clock = inputs[H_CLOCK_IN].getVoltage();
        in.clock.reset = gate(RESET_IN);
        in.clock.resetButton = params[LOAD_BUTTON].getValue() >= .5f;
        in.vClockConnected = inputs[V_CLOCK_IN].isConnected();
        in.vClock = gate(V_CLOCK_IN);
        in.dirH = gate(DIR_H_IN); in.dirV = gate(DIR_V_IN);
        in.jumpH = gate(JUMP_H_IN); in.jumpV = gate(JUMP_V_IN);
        auto out = state.tick(p, in, args.sampleRate);

        float knob = clamp(finite(params[STEP + out.step].getValue(), 0), 0.f, 1.f);
        int range = integer(RANGE, 0, 2, 0);
        float cv = range == 0 ? 10 * knob : range == 1 ? 5 * knob : 10 * knob - 5;
        outputs[CV_OUT].setVoltage(cv);
        outputs[INV_OUT].setVoltage(-cv);
        outputs[SLEW_A_OUT].setVoltage(slewA.process(cv, params[SLEW_A].getValue(), gate(SLEW_A_CV_IN), args.sampleTime));
        outputs[SLEW_B_OUT].setVoltage(slewB.process(cv, params[SLEW_B].getValue(), gate(SLEW_B_CV_IN), args.sampleTime));
        writer.write(outputs[BUS_OUT], out.state, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        for (int half = 0; half < 2; ++half) {
            Output& poly = outputs[half ? STEPS_B_OUT : STEPS_A_OUT];
            poly.setChannels(STEPS / 2);
            for (int ch = 0; ch < STEPS / 2; ++ch) poly.setVoltage(half * 8 + ch == out.step ? dspemu::bus::HIGH : 0.f, ch);
        }
        for (int k = 0; k < STEPS; ++k) lights[STEP_LIGHT + k].setBrightness(k == out.step ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        published.store(out.state, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "state", json_integer(published.load(std::memory_order_relaxed) & 255));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* saved = json_object_get(root, "state");
        int value = json_is_integer(saved) ? int(std::max<json_int_t>(0, std::min<json_int_t>(255, json_integer_value(saved)))) : 0;
        reader.reset(); state.clear(uint8_t(value)); slewA.reset(); slewB.reset(); published.store(unsigned(value));
    }
};

struct LatticeDisplay : widget::Widget {
    ByteLattice* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        unsigned value = module ? module->published.load(std::memory_order_relaxed) : 0;
        std::string text = string::f("%02X", value & 255);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteLatticeWidget : ModuleWidget {
    ByteLatticeWidget(ByteLattice* module) {
        using namespace bytebus_layout::lattice;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Lattice.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::lattice::WIDTH-15, 365)));
        const Vec steps[] = {POS(STEP0), POS(STEP1), POS(STEP2), POS(STEP3), POS(STEP4), POS(STEP5), POS(STEP6), POS(STEP7),
            POS(STEP8), POS(STEP9), POS(STEP10), POS(STEP11), POS(STEP12), POS(STEP13), POS(STEP14), POS(STEP15)};
        const Vec lights[] = {POS(STEP0_LIGHT), POS(STEP1_LIGHT), POS(STEP2_LIGHT), POS(STEP3_LIGHT), POS(STEP4_LIGHT),
            POS(STEP5_LIGHT), POS(STEP6_LIGHT), POS(STEP7_LIGHT), POS(STEP8_LIGHT), POS(STEP9_LIGHT), POS(STEP10_LIGHT),
            POS(STEP11_LIGHT), POS(STEP12_LIGHT), POS(STEP13_LIGHT), POS(STEP14_LIGHT), POS(STEP15_LIGHT)};
        for (int k = 0; k < ByteLattice::STEPS; ++k) {
            addParam(createParamCentered<RoundSmallBlackKnob>(steps[k], module, ByteLattice::STEP + k));
            addChild(createLightCentered<SmallLight<GreenLight>>(lights[k], module, ByteLattice::STEP_LIGHT + k));
        }
        auto* display = createWidget<LatticeDisplay>(POS(STATE));
        display->box.size = Vec(STATE_W, STATE_H); display->module = module; addChild(display);
        auto* jump = createParam<dspemu::panel::Choice>(POS(JUMP), module, ByteLattice::JUMP);
        jump->box.size = Vec(JUMP_W, JUMP_H);
        jump->labels = {"FOLLOW", "LOAD"}; addParam(jump);
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, ByteLattice::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "INTERNAL", "ENGINE"}; addParam(source);
        auto* range = createParam<dspemu::panel::Choice>(POS(RANGE), module, ByteLattice::RANGE);
        range->box.size = Vec(RANGE_W, RANGE_H);
        range->labels = {"0-10 V", "0-5 V", "±5 V"}; addParam(range);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, ByteLattice::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED), module, ByteLattice::SEED));
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, ByteLattice::LOAD_BUTTON));
        addParam(createParamCentered<RoundSmallBlackKnob>(POS(SLEW_A), module, ByteLattice::SLEW_A));
        addParam(createParamCentered<RoundSmallBlackKnob>(POS(SLEW_B), module, ByteLattice::SLEW_B));
        addInput(createInputCentered<PJ301MPort>(POS(H_CLOCK_IN), module, ByteLattice::H_CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(V_CLOCK_IN), module, ByteLattice::V_CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(DIR_H_IN), module, ByteLattice::DIR_H_IN));
        addInput(createInputCentered<PJ301MPort>(POS(DIR_V_IN), module, ByteLattice::DIR_V_IN));
        addInput(createInputCentered<PJ301MPort>(POS(JUMP_H_IN), module, ByteLattice::JUMP_H_IN));
        addInput(createInputCentered<PJ301MPort>(POS(JUMP_V_IN), module, ByteLattice::JUMP_V_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, ByteLattice::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN), module, ByteLattice::BUS_IN));
        addInput(createInputCentered<PJ301MPort>(POS(SLEW_A_CV_IN), module, ByteLattice::SLEW_A_CV_IN));
        addInput(createInputCentered<PJ301MPort>(POS(SLEW_B_CV_IN), module, ByteLattice::SLEW_B_CV_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CV_OUT), module, ByteLattice::CV_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(INV_OUT), module, ByteLattice::INV_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(SLEW_A_OUT), module, ByteLattice::SLEW_A_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(SLEW_B_OUT), module, ByteLattice::SLEW_B_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, ByteLattice::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, ByteLattice::CLOCK_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(STEPS_A_OUT), module, ByteLattice::STEPS_A_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(STEPS_B_OUT), module, ByteLattice::STEPS_B_OUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, ByteLattice::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, ByteLattice::CLOCK_LIGHT));
#undef POS
    }
};
Model* modelLattice = createModel<ByteLattice, ByteLatticeWidget>("Lattice");
