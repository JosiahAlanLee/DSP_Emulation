// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Wheel.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteWheel : Module {
    enum Params { SOURCE, STRIDE, RATE, LENGTH, OFFSET, SEED, LOAD_BUTTON, CAPTURE_BUTTON, NUM_PARAMS };
    enum Inputs { CLOCK_IN, RESET_IN, CAPTURE_IN, NUM_INPUTS };
    enum Outputs { LOOP_OUT, FREE_OUT, CLOCK_OUT, U_OUT, G_OUT, LOOP_GATE_OUT, NOISE_GATE_OUT, NUM_OUTPUTS };
    enum Lights { BIT_LIGHT, CLOCK_LIGHT = BIT_LIGHT + 8, RESET_LIGHT, ARMED_LIGHT, NUM_LIGHTS };
    dspemu::bus::Writer loopWriter, freeWriter;
    dspemu::wheel::Wheel state;
    std::atomic<unsigned> loopWord{0};
    std::atomic<uint32_t> savedFree{0}, savedLoop{0};

    ByteWheel() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SOURCE, 0, 3, 0, "Clock source",
            {"Auto (CLOCK jack, else internal)", "CLOCK jack", "Internal", "Engine rate (unclocked buses)"});
        configSwitch(STRIDE, 0, 2, 0, "Window stride", {"1 (consecutive stages)", "2", "3 (spread over all 24)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        std::vector<std::string> lengths{"Off (continues FREE: one 48-stage line)"};
        for (int k = 1; k <= dspemu::wheel::STAGES; ++k) lengths.push_back(std::to_string(k));
        configSwitch(LENGTH, 0, dspemu::wheel::STAGES, 16, "Loop length", lengths);
        configParam(OFFSET, 0, 23, 0, "Window offset (stage)", "", 0.f, 1.f, 1.f);
        configParam(SEED, 0, 255, 0, "Seed byte (repeated in all three bytes of both registers)");
        for (int id : {OFFSET, SEED}) getParamQuantity(id)->snapEnabled = true;
        configButton(LOAD_BUTTON, "Load seed and restart internal clock");
        configButton(CAPTURE_BUTTON, "Capture (copy FREE into LOOP on the next edge)");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (loads the seed; takes priority over clock)");
        configInput(CAPTURE_IN, "Capture trigger");
        configOutput(LOOP_OUT, "LOOP byte bus (9 channels when clocked, else 8)");
        configOutput(FREE_OUT, "FREE byte bus (9 channels when clocked, else 8)");
        configOutput(CLOCK_OUT, "Word clock");
        configOutput(U_OUT, "U: the LOOP word as a DAC, -5 to +5 V");
        configOutput(G_OUT, "G: the LOOP word's set bits counted, -5 to +5 V in nine levels");
        configOutput(LOOP_GATE_OUT, "Loop gate (LOOP stage 1)");
        configOutput(NOISE_GATE_OUT, "Noise gate (FREE stage 1)");
        for (int k = 0; k < 8; ++k) configLight(BIT_LIGHT + k, string::f("LOOP bit %d", k));
        configLight(ARMED_LIGHT, "Capture armed");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); state.clear(); loopWord.store(0); savedFree.store(0); savedLoop.store(0);
    }
    void process(const ProcessArgs& args) override {
        namespace wh = dspemu::wheel;
        using wh::ClockMode;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        static constexpr ClockMode modes[] = {ClockMode::Auto, ClockMode::Jack, ClockMode::Internal, ClockMode::Engine};
        wh::Parameters p;
        p.mode = modes[integer(SOURCE, 0, 3, 0)];
        p.stride = integer(STRIDE, 0, 2, 0) + 1;
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.length = integer(LENGTH, 0, wh::STAGES, 16);
        p.offset = integer(OFFSET, 0, 23, 0);
        p.seed = uint8_t(integer(SEED, 0, 255, 0));
        wh::Inputs in;
        in.clock.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock.clock = inputs[CLOCK_IN].getVoltage();
        in.clock.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.clock.resetButton = params[LOAD_BUTTON].getValue() >= .5f;
        in.capture = inputs[CAPTURE_IN].isConnected() ? inputs[CAPTURE_IN].getVoltage() : 0.f;
        in.captureButton = params[CAPTURE_BUTTON].getValue() >= .5f;
        auto out = state.tick(p, in, args.sampleRate);
        loopWriter.write(outputs[LOOP_OUT], out.loopWord, out.clocked, out.clk);
        freeWriter.write(outputs[FREE_OUT], out.freeWord, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        outputs[U_OUT].setVoltage(out.u);
        outputs[G_OUT].setVoltage(out.g);
        outputs[LOOP_GATE_OUT].setVoltage(out.loopGate ? dspemu::bus::HIGH : 0.f);
        outputs[NOISE_GATE_OUT].setVoltage(out.noiseGate ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < 8; ++k) lights[BIT_LIGHT + k].setBrightness(out.loopWord >> k & 1 ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        lights[ARMED_LIGHT].setBrightness(out.armed ? 1.f : 0.f);
        loopWord.store(out.loopWord, std::memory_order_relaxed);
        savedFree.store(out.free, std::memory_order_relaxed);
        savedLoop.store(out.loop, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "free", json_integer(savedFree.load(std::memory_order_relaxed) & dspemu::wheel::ALL));
        json_object_set_new(root, "loop", json_integer(savedLoop.load(std::memory_order_relaxed) & dspemu::wheel::ALL));
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto load = [&](const char* key) {
            json_t* saved = json_object_get(root, key);
            return json_is_integer(saved) ? uint32_t(std::max<json_int_t>(0, std::min<json_int_t>(dspemu::wheel::ALL, json_integer_value(saved)))) : 0u;
        };
        uint32_t freeState = load("free"), loopState = load("loop");
        state.clear(freeState, loopState); savedFree.store(freeState); savedLoop.store(loopState);
    }
};

struct WheelDisplay : widget::Widget {
    ByteWheel* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        unsigned value = module ? module->loopWord.load(std::memory_order_relaxed) : 0;
        std::string text = string::f("%02X", value & 255);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteWheelWidget : ModuleWidget {
    ByteWheelWidget(ByteWheel* module) {
        using namespace bytebus_layout::wheel;
        using W = ByteWheel;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Wheel.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::wheel::WIDTH-15, 365)));
        auto* display = createWidget<WheelDisplay>(POS(LOOP_DISPLAY));
        display->box.size = Vec(LOOP_DISPLAY_W, LOOP_DISPLAY_H); display->module = module; addChild(display);
        const Vec bits[] = {POS(BIT0_LIGHT), POS(BIT1_LIGHT), POS(BIT2_LIGHT), POS(BIT3_LIGHT),
            POS(BIT4_LIGHT), POS(BIT5_LIGHT), POS(BIT6_LIGHT), POS(BIT7_LIGHT)};
        for (int k = 0; k < 8; ++k) addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, W::BIT_LIGHT + k));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, W::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "INTERNAL", "ENGINE"}; addParam(source);
        auto* stride = createParam<dspemu::panel::Choice>(POS(STRIDE), module, W::STRIDE);
        stride->box.size = Vec(STRIDE_W, STRIDE_H);
        stride->labels = {"1", "2", "3"}; addParam(stride);
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, W::LOAD_BUTTON));
        addParam(createParamCentered<VCVButton>(POS(CAPTURE), module, W::CAPTURE_BUTTON));
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, W::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(LENGTH), module, W::LENGTH));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(OFFSET), module, W::OFFSET));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED), module, W::SEED));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, W::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, W::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(CAPTURE_IN), module, W::CAPTURE_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(LOOP_OUT), module, W::LOOP_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(FREE_OUT), module, W::FREE_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, W::CLOCK_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(U_OUT), module, W::U_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(G_OUT), module, W::G_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(LOOP_GATE_OUT), module, W::LOOP_GATE_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(NOISE_GATE_OUT), module, W::NOISE_GATE_OUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, W::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(ARMED_LIGHT), module, W::ARMED_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, W::CLOCK_LIGHT));
#undef POS
    }
};
Model* modelWheel = createModel<ByteWheel, ByteWheelWidget>("Wheel");
