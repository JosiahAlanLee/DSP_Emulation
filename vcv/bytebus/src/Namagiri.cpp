// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Namagiri.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteNamagiri : Module {
    enum Params { SOURCE, RATE, RADIUS, ANGLE, DEPTH, ROOT, SPAN, THRESH, SETTLE, NUM_PARAMS };
    enum Inputs { RADIUS_CV_IN, ANGLE_CV_IN, DEPTH_CV_IN, CLOCK_IN, RESET_IN, BUS_IN, NUM_INPUTS };
    enum Outputs { REAL_OUT, IMAG_OUT, MAG_OUT, PHASE_OUT, V_OCT_OUT, GATE_OUT,
        DELTA_OUT, VEL_OUT, CONVERGED_OUT, EOC_OUT, BUS_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, CLOCK_LIGHT, RESET_LIGHT, CONVERGED_LIGHT, EOC_LIGHT, NUM_LIGHTS };
    dspemu::bus::Writer writer;
    dspemu::bus::Reader addressReader;
    dspemu::namagiri::Namagiri state;
    // Published for the UI thread and the saver; the audio thread writes these each sample.
    std::atomic<int> stepOut{1};
    std::atomic<double> previousReal{0}, previousImag{0};
    std::atomic<int> convergedOut{0};
    std::atomic<int> noteOut{0};                  // the held note (V/OCT across rests)

    static const char* noteName(int note) {
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        static std::string text;
        text = string::f("%s%d", names[note % 12], note / 12 - 1);
        return text.c_str();
    }

    ByteNamagiri() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SOURCE, 0, 3, 0, "Clock source",
            {"Auto (CLOCK jack, then internal)", "CLOCK jack", "Internal", "Engine rate (unclocked bus)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(RADIUS, 0, dspemu::namagiri::MAX_RADIUS, .8, "Radius (|q|)");
        configParam(ANGLE, 0, 1, 0, "Angle (arg q, in turns)");
        configParam(DEPTH, 1, dspemu::namagiri::MAX_DEPTH, 12, "Depth (steps per cycle)");
        getParamQuantity(DEPTH)->snapEnabled = true;
        configParam(ROOT, 0, 127, 48, "Root note (MIDI)");
        getParamQuantity(ROOT)->snapEnabled = true;
        configParam(SPAN, 1, 96, 24, "Span (semitones above ROOT)");
        getParamQuantity(SPAN)->snapEnabled = true;
        configParam(THRESH, 0, 1, 0, "Velocity threshold (a step below it is a rest)");
        // The CONVERGED latch's threshold, in the DELTA output's volts (1 mV - 10 V, logarithmic).
        configParam(SETTLE, -10, std::log2(10.f), std::log2(.1f), "Settle threshold (delta)", " V", 2.f);
        configInput(RADIUS_CV_IN, "Radius CV (10 V spans |q| 0 - .97)");
        configInput(ANGLE_CV_IN, "Angle CV (10 V spans one turn)");
        configInput(DEPTH_CV_IN, "Depth CV (10 V spans 32 steps)");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (back to step 1; takes priority over clock)");
        configInput(BUS_IN, "Step word: low bits set the step, modulo DEPTH (an unclocked bus tracks every sample)");
        configOutput(REAL_OUT, "Real part of R(q), 5 V per unit, +/-5 V");
        configOutput(IMAG_OUT, "Imaginary part of R(q), 5 V per unit, +/-5 V");
        configOutput(MAG_OUT, "Magnitude |R(q)|, 5 V per unit, 0-10 V");
        configOutput(PHASE_OUT, "Phase of R(q), +/-5 V for +/-pi");
        configOutput(V_OCT_OUT, "Note pitch, (note - 60)/12 V, so C4 = 0 V; holds across rests");
        configOutput(GATE_OUT, "Gate, high for the step when its velocity reaches THRESH and is above zero");
        configOutput(DELTA_OUT, "This step's change, 5 V per unit, 0-10 V");
        configOutput(VEL_OUT, "The step's velocity, 0-10 V (below THRESH on a rest)");
        configOutput(CONVERGED_OUT, "Converged gate (high once delta falls below SETTLE, until the wrap)");
        configOutput(EOC_OUT, "End of cycle trigger at the wrap");
        configOutput(BUS_OUT, "Phase word (the angle as a signed fraction of pi, offset binary; 9 channels)");
        for (int k = 0; k < 8; ++k) configLight(BIT0_LIGHT + k, string::f("Phase word bit %d", k));
        configLight(CLOCK_LIGHT, "Step clock");
        configLight(RESET_LIGHT, "Reset publication");
        configLight(CONVERGED_LIGHT, "Converged");
        configLight(EOC_LIGHT, "End of cycle");
    }
    static dspemu::namagiri::ClockMode modeOf(int choice) {
        using M = dspemu::namagiri::ClockMode;
        static const M modes[] = {M::Auto, M::Jack, M::Internal, M::Engine};
        return modes[std::max(0, std::min(3, choice))];
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); state.clear(); addressReader.reset();
        stepOut.store(1); previousReal.store(0); previousImag.store(0);
        convergedOut.store(0); noteOut.store(0);
    }
    void process(const ProcessArgs& args) override {
        namespace nm = dspemu::namagiri;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        nm::Parameters p;
        p.mode = modeOf(integer(SOURCE, 0, 3, 0));
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.radius = clamp(finite(params[RADIUS].getValue(), .8f), 0.f, float(nm::MAX_RADIUS));
        p.turns = params[ANGLE].getValue();
        p.turns = std::isfinite(p.turns) ? p.turns - std::floor(p.turns) : 0.f;
        p.depth = integer(DEPTH, 1, nm::MAX_DEPTH, 12);
        p.root = integer(ROOT, 0, 127, 48);
        p.span = integer(SPAN, 1, 96, 24);
        p.velocityThreshold = clamp(finite(params[THRESH].getValue(), 0.f), 0.f, 1.f);
        p.settle = std::exp2(clamp(finite(params[SETTLE].getValue(), std::log2(.1f)), -10.f, std::log2(10.f))) / 5;
        nm::Inputs in;
        in.clock.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock.clock = inputs[CLOCK_IN].getVoltage();
        in.clock.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.address = addressReader.process(inputs[BUS_IN]);
        // CV: 10 V spans the full knob range. The engine clamps and wraps again.
        if (inputs[RADIUS_CV_IN].isConnected())
            p.radius += clamp(finite(inputs[RADIUS_CV_IN].getVoltage(), 0.f), -10.f, 10.f) * float(nm::MAX_RADIUS / 10);
        if (inputs[ANGLE_CV_IN].isConnected())
            p.turns += clamp(finite(inputs[ANGLE_CV_IN].getVoltage(), 0.f), -10.f, 10.f) * .1f;
        if (inputs[DEPTH_CV_IN].isConnected())
            p.depth += int(std::round(clamp(finite(inputs[DEPTH_CV_IN].getVoltage(), 0.f), -10.f, 10.f) * 3.2f));
        p.turns -= std::floor(p.turns);
        auto out = state.tick(p, in, args.sampleRate);
        writer.write(outputs[BUS_OUT], out.word, out.clocked, out.clk);
        auto clip = [](double v, double lo, double hi) { return float(std::max(lo, std::min(hi, v))); };
        outputs[REAL_OUT].setVoltage(clip(5 * out.c.real, -5, 5));
        outputs[IMAG_OUT].setVoltage(clip(5 * out.c.imag, -5, 5));
        outputs[MAG_OUT].setVoltage(clip(5 * out.c.mag, 0, 10));
        outputs[PHASE_OUT].setVoltage(clip(5 * out.c.phase / nm::PI, -5, 5));
        outputs[V_OCT_OUT].setVoltage((out.note - 60) / 12.f);
        outputs[GATE_OUT].setVoltage(out.gate ? dspemu::bus::HIGH : 0.f);
        outputs[DELTA_OUT].setVoltage(clip(5 * out.c.delta, 0, 10));
        outputs[VEL_OUT].setVoltage(clip(10 * out.velocity, 0, 10));
        outputs[CONVERGED_OUT].setVoltage(out.converged ? dspemu::bus::HIGH : 0.f);
        outputs[EOC_OUT].setVoltage(out.eoc ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT + k].setBrightness(out.word & (1u << k) ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        lights[CONVERGED_LIGHT].setBrightnessSmooth(out.converged ? 1.f : 0.f, args.sampleTime);
        lights[EOC_LIGHT].setBrightnessSmooth(out.eoc ? 1.f : 0.f, args.sampleTime);
        stepOut.store(out.step, std::memory_order_relaxed);
        previousReal.store(state.previous.real(), std::memory_order_relaxed);
        previousImag.store(state.previous.imag(), std::memory_order_relaxed);
        convergedOut.store(state.converged, std::memory_order_relaxed);
        noteOut.store(out.note, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "step", json_integer(stepOut.load(std::memory_order_relaxed)));
        json_object_set_new(root, "previousReal", json_real(previousReal.load(std::memory_order_relaxed)));
        json_object_set_new(root, "previousImag", json_real(previousImag.load(std::memory_order_relaxed)));
        json_object_set_new(root, "converged", json_integer(convergedOut.load(std::memory_order_relaxed)));
        json_object_set_new(root, "note", json_integer(noteOut.load(std::memory_order_relaxed)));
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto integerOf = [&](const char* key, json_int_t lo, json_int_t hi, json_int_t fallback) {
            json_t* j = json_object_get(root, key);
            return json_is_integer(j) ? std::max(lo, std::min(hi, json_integer_value(j))) : fallback;
        };
        auto realOf = [&](const char* key, double fallback) {
            json_t* j = json_object_get(root, key);
            return json_is_number(j) ? json_number_value(j) : fallback;
        };
        int step = int(integerOf("step", 1, dspemu::namagiri::MAX_DEPTH, 1));
        std::complex<double> previous(realOf("previousReal", 0), realOf("previousImag", 0));
        bool converged = integerOf("converged", 0, 1, 0) != 0;
        // Patches saved before the held note was persisted have no "note":
        // fall back to MIDI 0, the old restore behavior until the next event.
        int note = int(integerOf("note", 0, 127, 0));
        state.clear(step, previous, converged, note); addressReader.reset();
        stepOut.store(step, std::memory_order_relaxed);
        previousReal.store(state.previous.real(), std::memory_order_relaxed);
        previousImag.store(state.previous.imag(), std::memory_order_relaxed);
        convergedOut.store(state.converged, std::memory_order_relaxed);
        noteOut.store(state.note, std::memory_order_relaxed);
    }
};

struct NamagiriDisplay : widget::Widget {
    ByteNamagiri* module = nullptr;
    bool note = false;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, note ? 14 : 15);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        std::string text;
        if (note) {
            int root = module ? int(std::round(module->params[ByteNamagiri::ROOT].getValue())) : 48;
            text = ByteNamagiri::noteName(std::max(0, std::min(127, root)));
        } else {
            int step = module ? module->stepOut.load(std::memory_order_relaxed) : 1;
            text = string::f("%d", step);
        }
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteNamagiriWidget : ModuleWidget {
    ByteNamagiriWidget(ByteNamagiri* module) {
        using namespace bytebus_layout::namagiri;
        using R = ByteNamagiri;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Namagiri.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::namagiri::WIDTH - 15, 365)));
        for (bool note : {false, true}) {
            auto* display = createWidget<NamagiriDisplay>(note ? POS(NOTE) : POS(STEP));
            display->box.size = note ? Vec(NOTE_W, NOTE_H) : Vec(STEP_W, STEP_H);
            display->module = module; display->note = note; addChild(display);
        }
        const Vec bits[] = {POS(BIT0_LIGHT), POS(BIT1_LIGHT), POS(BIT2_LIGHT), POS(BIT3_LIGHT),
            POS(BIT4_LIGHT), POS(BIT5_LIGHT), POS(BIT6_LIGHT), POS(BIT7_LIGHT)};
        for (int k = 0; k < 8; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, R::BIT0_LIGHT + k));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, R::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "INTERNAL", "ENGINE"}; addParam(source);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, R::RATE));
        addParam(createParamCentered<RoundBlackKnob>(POS(RADIUS), module, R::RADIUS));
        addParam(createParamCentered<RoundBlackKnob>(POS(ANGLE), module, R::ANGLE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(DEPTH), module, R::DEPTH));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(ROOT), module, R::ROOT));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SPAN), module, R::SPAN));
        addParam(createParamCentered<RoundBlackKnob>(POS(THRESH), module, R::THRESH));
        addParam(createParamCentered<RoundBlackKnob>(POS(SETTLE), module, R::SETTLE));
        addInput(createInputCentered<PJ301MPort>(POS(RADIUS_CV_IN), module, R::RADIUS_CV_IN));
        addInput(createInputCentered<PJ301MPort>(POS(ANGLE_CV_IN), module, R::ANGLE_CV_IN));
        addInput(createInputCentered<PJ301MPort>(POS(DEPTH_CV_IN), module, R::DEPTH_CV_IN));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, R::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, R::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN), module, R::BUS_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(REAL_OUT), module, R::REAL_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(IMAG_OUT), module, R::IMAG_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(MAG_OUT), module, R::MAG_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(PHASE_OUT), module, R::PHASE_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(V_OCT_OUT), module, R::V_OCT_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(GATE_OUT), module, R::GATE_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(DELTA_OUT), module, R::DELTA_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(VEL_OUT), module, R::VEL_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CONVERGED_OUT), module, R::CONVERGED_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(EOC_OUT), module, R::EOC_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, R::BUS_OUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, R::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, R::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(CONVERGED_LIGHT), module, R::CONVERGED_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(EOC_LIGHT), module, R::EOC_LIGHT));
#undef POS
    }
};
Model* modelNamagiri = createModel<ByteNamagiri, ByteNamagiriWidget>("Namagiri");
