// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Counter2.hpp"
#include "engine/Lookup.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteCounter2 : Module {
    enum Params { CLOCK_MODE, RATE, N_PARAM, D_PARAM, SEED, LOAD_BUTTON, NUM_PARAMS };
    enum Inputs { CLOCK_IN, RESET_IN, RATIO_IN, NUM_INPUTS };   // RATIO appended in 2.11
    enum Outputs { BUS_OUT, CLOCK_OUT, CARRY_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, CLOCK_LIGHT, RESET_LIGHT, CARRY_LIGHT, NUM_LIGHTS };
    dspemu::bus::Writer writer;
    dspemu::bus::Reader ratioReader;
    dspemu::counter2::Counter2 state;
    // The accumulator (low 16 bits) and D (above), saved together; the display reads the word.
    std::atomic<uint32_t> published{16u << 16};
    std::atomic<int> ratio{(1 << 8) | 16};

    ByteCounter2() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(CLOCK_MODE, 0, 3, 0, "Clock source",
            {"Auto (CLOCK jack, then internal)", "CLOCK jack", "Internal", "Engine rate (unclocked bus)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(N_PARAM, 1, 16, 1, "N (cycles per D edges)");
        configParam(D_PARAM, 1, 64, 16, "D (edges per N cycles)");
        configParam(SEED, 0, 255, 0, "Seed phase word");
        for (int id : {N_PARAM, D_PARAM, SEED}) getParamQuantity(id)->snapEnabled = true;
        configButton(LOAD_BUTTON, "Load seed and restart internal clock");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (publishes the seed; takes priority over clock)");
        configInput(RATIO_IN, "Ratio word: n - 1 in bits 4-7, d - 1 in bits 0-3 (Lookup's Ratio family); replaces N and D");
        configOutput(BUS_OUT, "Phase byte bus (9 channels; 8 in Engine mode)");
        configOutput(CLOCK_OUT, "Published-word clock (one sample pulses)");
        configOutput(CARRY_OUT, "Carry (one sample pulse on an edge that completes a cycle)");
    }
    static dspemu::counter2::ClockMode modeOf(int choice) {
        using M = dspemu::counter2::ClockMode;   // no bus input, so no Bus CLK choice
        static const M modes[] = {M::Auto, M::Jack, M::Internal, M::Engine};
        return modes[std::max(0, std::min(3, choice))];
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); state.clear(); ratioReader.reset(); published.store(16u << 16);
    }
    void process(const ProcessArgs& args) override {
        using namespace dspemu::counter2;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        Parameters p;
        p.mode = modeOf(integer(CLOCK_MODE, 0, 3, 0));
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.n = integer(N_PARAM, 1, 16, 1);
        p.d = integer(D_PARAM, 1, 64, 16);
        // A patched RATIO replaces the knobs with the ratio word on its data lines (latched on its CLK
        // when it has one), from 1/16 to 16.
        auto ratioBus = ratioReader.process(inputs[RATIO_IN]);
        if (ratioBus.connected) { p.n = dspemu::lookup::ratioN(ratioBus.word); p.d = dspemu::lookup::ratioD(ratioBus.word); }
        p.seed = uint8_t(integer(SEED, 0, 255, 0));
        dspemu::counter2::Inputs in;
        in.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock = inputs[CLOCK_IN].getVoltage();
        in.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.resetButton = params[LOAD_BUTTON].getValue() >= .5f;
        auto out = state.tick(p, in, args.sampleRate);
        writer.write(outputs[BUS_OUT], out.word, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        outputs[CARRY_OUT].setVoltage(out.carry ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT + k].setBrightness(out.word & (1u << k) ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        lights[CARRY_LIGHT].setBrightnessSmooth(out.carry ? 1.f : 0.f, args.sampleTime);
        published.store(uint32_t(state.acc) | uint32_t(state.d) << 16, std::memory_order_relaxed);
        ratio.store(p.n << 8 | p.d, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        uint32_t saved = published.load(std::memory_order_relaxed);
        json_t* root = json_object();
        json_object_set_new(root, "accumulator", json_integer(saved & 0xffff));
        json_object_set_new(root, "denominator", json_integer(saved >> 16));
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto get = [&](const char* key, json_int_t lo, json_int_t hi, json_int_t fallback) {
            json_t* v = json_object_get(root, key);
            return json_is_integer(v) ? std::max(lo, std::min(hi, json_integer_value(v))) : fallback;
        };
        int d = int(get("denominator", 1, dspemu::counter2::MAX_D, 16));
        int64_t acc = get("accumulator", 0, int64_t(256) * d - 1, 0);
        state.clear(0, d); state.acc = acc;
        published.store(uint32_t(acc) | uint32_t(d) << 16);
    }
};

struct Counter2Display : widget::Widget {
    ByteCounter2* module = nullptr;
    bool showRatio = false;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, showRatio ? 13 : 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        std::string text;
        if (showRatio) {
            int r = module ? module->ratio.load(std::memory_order_relaxed) : (1 << 8 | 16);
            text = string::f("%d/%d", r >> 8, r & 0xff);
        } else {
            uint32_t s = module ? module->published.load(std::memory_order_relaxed) : (16u << 16);
            text = string::f("%02X", unsigned((s & 0xffff) / std::max(1u, s >> 16)));
        }
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteCounter2Widget : ModuleWidget {
    ByteCounter2Widget(ByteCounter2* module) {
        using namespace bytebus_layout::counter2;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Counter2.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::counter2::WIDTH-15, 365)));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, ByteCounter2::CLOCK_MODE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "INTERNAL", "ENGINE"}; addParam(source);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, ByteCounter2::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(N), module, ByteCounter2::N_PARAM));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(D), module, ByteCounter2::D_PARAM));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED), module, ByteCounter2::SEED));
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, ByteCounter2::LOAD_BUTTON));
        const Vec bits[] = {POS(BIT0), POS(BIT1), POS(BIT2), POS(BIT3), POS(BIT4), POS(BIT5), POS(BIT6), POS(BIT7)};
        for (int k = 0; k < 8; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, ByteCounter2::BIT0_LIGHT + k));
        for (bool ratio : {false, true}) {
            auto* display = createWidget<Counter2Display>(ratio ? POS(RATIO) : POS(WORD));
            display->box.size = ratio ? Vec(RATIO_W, RATIO_H) : Vec(WORD_W, WORD_H);
            display->module = module; display->showRatio = ratio; addChild(display);
        }
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, ByteCounter2::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, ByteCounter2::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RATIO_IN), module, ByteCounter2::RATIO_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, ByteCounter2::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, ByteCounter2::CLOCK_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CARRY_OUT), module, ByteCounter2::CARRY_OUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, ByteCounter2::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, ByteCounter2::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CARRY_LIGHT), module, ByteCounter2::CARRY_LIGHT));
#undef POS
    }
};
Model* modelCounter2 = createModel<ByteCounter2, ByteCounter2Widget>("Counter2");
