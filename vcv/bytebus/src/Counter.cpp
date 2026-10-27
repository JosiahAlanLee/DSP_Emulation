// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Counter.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteCounter : Module {
    enum Params { CLOCK_MODE, RATE, INC, MODULUS, SEED, LOAD_BUTTON, RUN_MODE, NUM_PARAMS };
    enum Inputs { BUS_IN, CLOCK_IN, RESET_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, CARRY_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, CLOCK_LIGHT, RESET_LIGHT, CARRY_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    dspemu::counter::Counter state;
    // The published word, with bit 8 set while Pendulum is counting down; saved together.
    std::atomic<unsigned> published{0};

    ByteCounter() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(CLOCK_MODE, 0, 4, 0, "Clock source",
            {"Auto (CLOCK jack, then bus CLK, then internal)", "CLOCK jack", "Bus CLK", "Internal", "Engine rate (unclocked bus)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(INC, 1, 255, 1, "Increment (when BUS IN is unpatched)");
        configParam(MODULUS, 2, 256, 256, "Modulus");
        configParam(SEED, 0, 255, 0, "Seed word");
        for (int id : {INC, MODULUS, SEED}) getParamQuantity(id)->snapEnabled = true;
        configButton(LOAD_BUTTON, "Load seed and restart internal clock");
        configSwitch(RUN_MODE, 0, 2, 0, "Run mode", {"Loop (wrap at MOD)", "One-shot (hold at MOD - 1)", "Pendulum (reflect at 0 and MOD - 1)"});
        configInput(BUS_IN, "Increment byte bus (replaces INC; optional CLK on channel 9)");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (publishes the seed; takes priority over clock)");
        configOutput(BUS_OUT, "Count byte bus (9 channels; 8 in Engine mode)");
        configOutput(CLOCK_OUT, "Published-word clock (one sample pulses)");
        configOutput(CARRY_OUT, "Carry (one sample pulse on a wrap, on reaching the One-shot hold, or on a Pendulum reflection)");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); reader.reset(); state.clear(); published.store(0);
    }
    void process(const ProcessArgs& args) override {
        using namespace dspemu::counter;
        Parameters p;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        p.mode = ClockMode(integer(CLOCK_MODE, 0, 4, 0));
        p.run = RunMode(integer(RUN_MODE, 0, 2, 0));
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.increment = uint8_t(integer(INC, 1, 255, 1));
        p.modulus = integer(MODULUS, 2, 256, 256);
        p.seed = uint8_t(integer(SEED, 0, 255, 0));
        dspemu::counter::Inputs in;
        in.data = reader.process(inputs[BUS_IN]);
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
        published.store(out.word | (out.down ? 0x100u : 0u), std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        unsigned state = published.load(std::memory_order_relaxed);
        json_object_set_new(root, "word", json_integer(state & 0xff));
        json_object_set_new(root, "down", json_boolean(state & 0x100));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* saved = json_object_get(root, "word");
        int value = json_is_integer(saved) ? int(std::max<json_int_t>(0, std::min<json_int_t>(255, json_integer_value(saved)))) : 0;
        // 2.3 patches have no direction: Pendulum starts up.
        bool down = json_is_true(json_object_get(root, "down"));
        reader.reset(); state.clear(uint8_t(value), down); published.store(unsigned(value) | (down ? 0x100u : 0u));
    }
};

struct CountDisplay : widget::Widget {
    ByteCounter* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        unsigned word = module ? module->published.load(std::memory_order_relaxed) & 0xff : 0;
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, string::f("%02X", word).c_str(), nullptr);
    }
};
struct ByteCounterWidget : ModuleWidget {
    ByteCounterWidget(ByteCounter* module) {
        using namespace bytebus_layout::counter;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Counter.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::counter::WIDTH-15, 365)));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, ByteCounter::CLOCK_MODE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "BUS", "INTERNAL", "ENGINE"}; addParam(source);
        auto* run = createParam<dspemu::panel::Choice>(POS(MODE), module, ByteCounter::RUN_MODE);
        run->box.size = Vec(MODE_W, MODE_H);
        run->labels = {"LOOP", "ONE-SHOT", "PENDULUM"}; addParam(run);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, ByteCounter::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(INC), module, ByteCounter::INC));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(MOD), module, ByteCounter::MODULUS));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED), module, ByteCounter::SEED));
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, ByteCounter::LOAD_BUTTON));
        const Vec bits[] = {POS(BIT0), POS(BIT1), POS(BIT2), POS(BIT3), POS(BIT4), POS(BIT5), POS(BIT6), POS(BIT7)};
        for (int k = 0; k < 8; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, ByteCounter::BIT0_LIGHT + k));
        auto* display = createWidget<CountDisplay>(POS(WORD));
        display->box.size = Vec(WORD_W, WORD_H); display->module = module; addChild(display);
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN), module, ByteCounter::BUS_IN));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, ByteCounter::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, ByteCounter::RESET_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, ByteCounter::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, ByteCounter::CLOCK_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CARRY_OUT), module, ByteCounter::CARRY_OUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, ByteCounter::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, ByteCounter::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CARRY_LIGHT), module, ByteCounter::CARRY_LIGHT));
#undef POS
    }
};
Model* modelCounter = createModel<ByteCounter, ByteCounterWidget>("Counter");
