// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Register.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ClockedRegister : Module {
    enum Params { CLOCK_MODE, RATE, RESET_WORD, RESET_BUTTON, NUM_PARAMS };
    enum Inputs { BUS_IN, CLOCK_IN, RESET_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, CLOCK_LIGHT, RESET_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    dspemu::byte_register::Register state;
    std::atomic<unsigned> displayedWord{0};

    ClockedRegister() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(CLOCK_MODE, 0, 4, 0, "Clock source",
            {"Auto (CLOCK jack, then bus CLK, then internal)", "CLOCK jack", "Bus CLK", "Internal", "Engine rate (unclocked bus)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(RESET_WORD, 0, 255, 0, "Reset word");
        getParamQuantity(RESET_WORD)->snapEnabled = true;
        configButton(RESET_BUTTON, "Load reset word and restart internal clock");
        configInput(BUS_IN, "Byte bus (LSB first; optional CLK on channel 9)");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (publishes reset word; takes priority over clock)");
        configOutput(BUS_OUT, "Held byte bus (9 channels; 8 in Engine mode)");
        configOutput(CLOCK_OUT, "Published-word clock (one sample pulses)");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); reader.reset(); state.clear(); displayedWord.store(0);
    }
    void process(const ProcessArgs& args) override {
        using namespace dspemu::byte_register;
        Parameters p;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        p.mode = ClockMode(int(std::round(clamp(finite(params[CLOCK_MODE].getValue(), 0), 0.f, 4.f))));
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.resetWord = uint8_t(std::round(clamp(finite(params[RESET_WORD].getValue(), 0), 0.f, 255.f)));
        dspemu::byte_register::Inputs in;
        in.data = reader.process(inputs[BUS_IN]);
        in.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock = inputs[CLOCK_IN].getVoltage();
        in.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.resetButton = params[RESET_BUTTON].getValue() >= .5f;
        auto out = state.tick(p, in, args.sampleRate);
        writer.write(outputs[BUS_OUT], out.word, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT + k].setBrightness(out.word & (1u << k) ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        displayedWord.store(out.word, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "word", json_integer(displayedWord.load(std::memory_order_relaxed)));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* saved = json_object_get(root, "word");
        int value = json_is_integer(saved) ? int(std::max<json_int_t>(0, std::min<json_int_t>(255, json_integer_value(saved)))) : 0;
        reader.reset(); state.clear(uint8_t(value)); displayedWord.store(unsigned(value));
    }
};

struct WordDisplay : widget::Widget {
    ClockedRegister* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        unsigned word = module ? module->displayedWord.load(std::memory_order_relaxed) : 0;
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, string::f("%02X", word).c_str(), nullptr);
    }
};
struct ClockedRegisterWidget : ModuleWidget {
    ClockedRegisterWidget(ClockedRegister* module) {
        using namespace bytebus_layout::reg;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Register.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::reg::WIDTH-15, 365)));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, ClockedRegister::CLOCK_MODE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "BUS", "INTERNAL", "ENGINE"}; addParam(source);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, ClockedRegister::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(RESET_WORD), module, ClockedRegister::RESET_WORD));
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, ClockedRegister::RESET_BUTTON));
        const Vec bits[] = {POS(BIT0), POS(BIT1), POS(BIT2), POS(BIT3), POS(BIT4), POS(BIT5), POS(BIT6), POS(BIT7)};
        for (int k = 0; k < 8; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, ClockedRegister::BIT0_LIGHT + k));
        auto* display = createWidget<WordDisplay>(POS(WORD));
        display->box.size = Vec(WORD_W, WORD_H); display->module = module; addChild(display);
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN), module, ClockedRegister::BUS_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, ClockedRegister::BUS_OUT));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, ClockedRegister::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, ClockedRegister::RESET_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, ClockedRegister::CLOCK_OUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, ClockedRegister::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, ClockedRegister::RESET_LIGHT));
#undef POS
    }
};
Model* modelClockedRegister = createModel<ClockedRegister, ClockedRegisterWidget>("ClockedRegister");
