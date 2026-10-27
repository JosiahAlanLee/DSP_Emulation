// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Roulette.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteRoulette : Module {
    enum Params { SOURCE, COMPARATOR, SCALE, RANGE, FILTER, NUM_PARAMS };
    enum Inputs { TRACK_IN, ROULETTE_IN, DIVIDE_IN, RANGE_CV_IN, RESET_IN, NUM_INPUTS };
    enum Outputs { LO_OUT, HI_OUT, RESULT_OUT, VCO_OUT, CLOCK_OUT, NUM_OUTPUTS };
    enum Lights { BIT_LIGHT, CLOCK_LIGHT = BIT_LIGHT + dspemu::roulette::BITS, RESULT_LIGHT, NUM_LIGHTS };
    dspemu::bus::Writer loWriter, hiWriter, resultWriter;
    dspemu::roulette::Roulette state;
    std::atomic<float> frequency{0};
    std::atomic<unsigned> count{0}, result{0};
    std::atomic<double> savedVc{0}, savedPhase{.5};

    ByteRoulette() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SOURCE, 0, 2, 0, "Counter clock", {"Auto (DIVIDE jack, else the VCO)", "DIVIDE jack", "VCO"});
        configSwitch(COMPARATOR, 0, 1, 0, "Phase comparator", {"XOR (the hardware's)", "Edge (phase-frequency)"});
        configSwitch(SCALE, 0, 2, 1, "Loop filter capacitor", {"x0.1 (0.2 uF)", "x1 (2 uF, the hardware's)", "x10 (20 uF)"});
        configParam(RANGE, 0, std::log2(20000.f), std::log2(2000.f), "Range (VCO maximum)", " Hz", 2.f);
        configParam(FILTER, 0, 100, 50, "Filter (series resistance; the wheel's friction)", " kOhm");
        configInput(TRACK_IN, "Track (the PLL's input, through a comparator at 1.2 V)");
        configInput(ROULETTE_IN, "Roulette (replaces the comparator: a gate spins the VCO, 12 V is full speed)");
        configInput(DIVIDE_IN, "Divide (clocks the counter instead of the VCO)");
        configInput(RANGE_CV_IN, "Range CV (1 V/octave on the VCO maximum, through a vactrol-like lag)");
        configInput(RESET_IN, "Reset (count 0, VCO restarted low)");
        configOutput(LO_OUT, "LO byte bus (count bits 0-7: /2 to /256)");
        configOutput(HI_OUT, "HI byte bus (count bits 4-11: /32 to /4096)");
        configOutput(RESULT_OUT, "RESULT byte bus (LO as the VCO comes to rest, one strobe per stop)");
        configOutput(VCO_OUT, "VCO (square, +/-5 V)");
        configOutput(CLOCK_OUT, "Count clock");
        for (int k = 0; k < dspemu::roulette::BITS; ++k) configLight(BIT_LIGHT + k, string::f("Count bit %d (/%d)", k, 2 << k));
        configLight(RESULT_LIGHT, "Result");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); state.clear(); frequency.store(0); count.store(0); result.store(0); savedVc.store(0); savedPhase.store(.5);
    }
    void process(const ProcessArgs& args) override {
        namespace rl = dspemu::roulette;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        static constexpr rl::Source sources[] = {rl::Source::Auto, rl::Source::Divide, rl::Source::Vco};
        static constexpr double scales[] = {.1, 1, 10};
        rl::Parameters p;
        p.source = sources[integer(SOURCE, 0, 2, 0)];
        p.comparator = integer(COMPARATOR, 0, 1, 0) == 1 ? rl::Comparator::Edge : rl::Comparator::Xor;
        p.scale = scales[integer(SCALE, 0, 2, 1)];
        p.rangeHz = std::exp2(clamp(finite(params[RANGE].getValue(), std::log2(2000.f)), 0.f, std::log2(20000.f)));
        p.filterOhms = 1e3 * clamp(finite(params[FILTER].getValue(), 50), 0.f, 100.f);
        rl::Inputs in;
        in.trackConnected = inputs[TRACK_IN].isConnected(); in.track = inputs[TRACK_IN].getVoltage();
        in.rouletteConnected = inputs[ROULETTE_IN].isConnected(); in.roulette = inputs[ROULETTE_IN].getVoltage();
        in.divideConnected = inputs[DIVIDE_IN].isConnected(); in.divide = inputs[DIVIDE_IN].getVoltage();
        in.rangeCv = inputs[RANGE_CV_IN].isConnected() ? inputs[RANGE_CV_IN].getVoltage() : 0.f;
        in.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        auto out = state.tick(p, in, args.sampleRate);
        loWriter.write(outputs[LO_OUT], out.lo, true, out.clk);
        hiWriter.write(outputs[HI_OUT], out.hi, true, out.clk);
        resultWriter.write(outputs[RESULT_OUT], out.result, true, out.resultClk);
        outputs[VCO_OUT].setVoltage(out.vcoOut);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < rl::BITS; ++k) lights[BIT_LIGHT + k].setBrightness(out.count >> k & 1 ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESULT_LIGHT].setBrightnessSmooth(out.resultClk ? 1.f : 0.f, args.sampleTime);
        frequency.store(float(out.frequency), std::memory_order_relaxed);
        count.store(out.count, std::memory_order_relaxed);
        result.store(out.result, std::memory_order_relaxed);
        savedVc.store(state.vC, std::memory_order_relaxed);
        savedPhase.store(state.phase, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "count", json_integer(count.load(std::memory_order_relaxed) & dspemu::roulette::COUNT_MASK));
        json_object_set_new(root, "result", json_integer(result.load(std::memory_order_relaxed) & 255));
        json_object_set_new(root, "filter", json_real(savedVc.load(std::memory_order_relaxed)));
        json_object_set_new(root, "phase", json_real(savedPhase.load(std::memory_order_relaxed)));
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto integerOf = [&](const char* key, json_int_t hi) {
            json_t* j = json_object_get(root, key);
            return json_is_integer(j) ? unsigned(std::max<json_int_t>(0, std::min<json_int_t>(hi, json_integer_value(j)))) : 0u;
        };
        auto realOf = [&](const char* key, double fallback) {
            json_t* j = json_object_get(root, key);
            return json_is_number(j) ? json_number_value(j) : fallback;
        };
        unsigned c = integerOf("count", dspemu::roulette::COUNT_MASK), r = integerOf("result", 255);
        state.clear(c, uint8_t(r), realOf("filter", 0), realOf("phase", .5));
        count.store(c); result.store(r); savedVc.store(state.vC); savedPhase.store(state.phase);
    }
};

struct RouletteDisplay : widget::Widget {
    ByteRoulette* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 15);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        float f = module ? module->frequency.load(std::memory_order_relaxed) : 0.f;
        std::string text = f <= 0 ? "REST" : f >= 1000 ? string::f("%.2fk", f / 1000) : f >= 100 ? string::f("%.0f", f) : string::f("%.1f", f);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteRouletteWidget : ModuleWidget {
    ByteRouletteWidget(ByteRoulette* module) {
        using namespace bytebus_layout::roulette;
        using R = ByteRoulette;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Roulette.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::roulette::WIDTH-15, 365)));
        auto* display = createWidget<RouletteDisplay>(POS(FREQUENCY));
        display->box.size = Vec(FREQUENCY_W, FREQUENCY_H); display->module = module; addChild(display);
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, R::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "DIVIDE", "VCO"}; addParam(source);
        auto* comparator = createParam<dspemu::panel::Choice>(POS(COMPARATOR), module, R::COMPARATOR);
        comparator->box.size = Vec(COMPARATOR_W, COMPARATOR_H);
        comparator->labels = {"XOR", "EDGE"}; addParam(comparator);
        auto* scale = createParam<dspemu::panel::Choice>(POS(SCALE), module, R::SCALE);
        scale->box.size = Vec(SCALE_W, SCALE_H);
        scale->labels = {"X0.1", "X1", "X10"}; addParam(scale);
        addParam(createParamCentered<RoundBlackKnob>(POS(RANGE), module, R::RANGE));
        addParam(createParamCentered<RoundBlackKnob>(POS(FILTER), module, R::FILTER));
        const Vec bits[] = {POS(BIT0_LIGHT), POS(BIT1_LIGHT), POS(BIT2_LIGHT), POS(BIT3_LIGHT), POS(BIT4_LIGHT), POS(BIT5_LIGHT),
            POS(BIT6_LIGHT), POS(BIT7_LIGHT), POS(BIT8_LIGHT), POS(BIT9_LIGHT), POS(BIT10_LIGHT), POS(BIT11_LIGHT)};
        for (int k = 0; k < dspemu::roulette::BITS; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, R::BIT_LIGHT + k));
        addInput(createInputCentered<PJ301MPort>(POS(TRACK_IN), module, R::TRACK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(ROULETTE_IN), module, R::ROULETTE_IN));
        addInput(createInputCentered<PJ301MPort>(POS(DIVIDE_IN), module, R::DIVIDE_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RANGE_CV_IN), module, R::RANGE_CV_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, R::RESET_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(LO_OUT), module, R::LO_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(HI_OUT), module, R::HI_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(RESULT_OUT), module, R::RESULT_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(VCO_OUT), module, R::VCO_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, R::CLOCK_OUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, R::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESULT_LIGHT), module, R::RESULT_LIGHT));
#undef POS
    }
};
Model* modelRoulette = createModel<ByteRoulette, ByteRouletteWidget>("Roulette");
