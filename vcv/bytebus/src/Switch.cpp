// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Switch.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteSwitch : Module {
    enum Params { SOURCE, MODE, BIT, RATE, B_WORD, C_WORD, NUM_PARAMS };
    enum Inputs { A_IN, B_IN, C_IN, CLOCK_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, CLOCK_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader readerA, readerB, readerC;
    dspemu::bus::Writer writer;
    dspemu::byte_switch::Switch state;
    std::atomic<unsigned> displayedWord{0};

    ByteSwitch() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SOURCE, 0, 4, 0, "Clock source",
            {"Auto (CLOCK jack, else follow A)", "CLOCK jack", "Follow A (transparent)", "Internal", "Engine rate (unclocked bus)"});
        configSwitch(MODE, 0, 2, 0, "Mode",
            {"Select (BIT of C chooses A or B)", "Bits (each bit of C chooses A or B)", "Mix (A + (B - A) x C / 255)"});
        configSwitch(BIT, 0, 7, 0, "Select bit", {"Bit 0", "Bit 1", "Bit 2", "Bit 3", "Bit 4", "Bit 5", "Bit 6", "Bit 7"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(B_WORD, 0, 255, 0, "B word (when B is unpatched)");
        configParam(C_WORD, 0, 255, 0, "C word (when C is unpatched)");
        for (int id : {B_WORD, C_WORD}) getParamQuantity(id)->snapEnabled = true;
        configInput(A_IN, "A byte bus (its CLK is the output clock when following A)");
        configInput(B_IN, "B byte bus (replaces the B word)");
        configInput(C_IN, "C control byte bus (replaces the C word; a gate is bit 0)");
        configInput(CLOCK_IN, "Clock (rising edge; reclocks the output)");
        configOutput(BUS_OUT, "Result byte bus (9 channels when clocked, else 8)");
        configOutput(CLOCK_OUT, "Published-word clock");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); readerA.reset(); readerB.reset(); readerC.reset(); state.clear(); displayedWord.store(0);
    }
    void process(const ProcessArgs& args) override {
        using namespace dspemu::byte_switch;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        Parameters p;
        p.source = Source(integer(SOURCE, 0, 4, 0));
        p.op = Op(integer(MODE, 0, 2, 0));
        p.bit = integer(BIT, 0, 7, 0);
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.b = uint8_t(integer(B_WORD, 0, 255, 0));
        p.c = uint8_t(integer(C_WORD, 0, 255, 0));
        dspemu::byte_switch::Inputs in;
        in.a = readerA.process(inputs[A_IN]);
        in.b = readerB.process(inputs[B_IN]);
        in.c = readerC.process(inputs[C_IN]);
        in.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock = inputs[CLOCK_IN].getVoltage();
        auto out = state.tick(p, in, args.sampleRate);
        writer.write(outputs[BUS_OUT], out.word, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT + k].setBrightness(out.word & (1u << k) ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        displayedWord.store(out.word, std::memory_order_relaxed);
    }
};

struct SwitchDisplay : widget::Widget {
    ByteSwitch* module = nullptr;
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
struct ByteSwitchWidget : ModuleWidget {
    ByteSwitchWidget(ByteSwitch* module) {
        using namespace bytebus_layout::byte_switch;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Switch.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::byte_switch::WIDTH-15, 365)));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, ByteSwitch::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "FOLLOW A", "INTERNAL", "ENGINE"}; addParam(source);
        auto* mode = createParam<dspemu::panel::Choice>(POS(MODE), module, ByteSwitch::MODE);
        mode->box.size = Vec(MODE_W, MODE_H);
        mode->labels = {"SELECT", "BITS", "MIX"}; addParam(mode);
        auto* bit = createParam<dspemu::panel::Choice>(POS(SEL_BIT), module, ByteSwitch::BIT);
        bit->box.size = Vec(SEL_BIT_W, SEL_BIT_H);
        bit->labels = {"BIT 0", "BIT 1", "BIT 2", "BIT 3", "BIT 4", "BIT 5", "BIT 6", "BIT 7"}; addParam(bit);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, ByteSwitch::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(B_WORD), module, ByteSwitch::B_WORD));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(C_WORD), module, ByteSwitch::C_WORD));
        const Vec bits[] = {POS(BIT0), POS(BIT1), POS(BIT2), POS(BIT3), POS(BIT4), POS(BIT5), POS(BIT6), POS(BIT7)};
        for (int k = 0; k < 8; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, ByteSwitch::BIT0_LIGHT + k));
        auto* display = createWidget<SwitchDisplay>(POS(WORD));
        display->box.size = Vec(WORD_W, WORD_H); display->module = module; addChild(display);
        addInput(createInputCentered<PJ301MPort>(POS(A_IN), module, ByteSwitch::A_IN));
        addInput(createInputCentered<PJ301MPort>(POS(B_IN), module, ByteSwitch::B_IN));
        addInput(createInputCentered<PJ301MPort>(POS(C_IN), module, ByteSwitch::C_IN));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, ByteSwitch::CLOCK_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, ByteSwitch::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, ByteSwitch::CLOCK_OUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, ByteSwitch::CLOCK_LIGHT));
#undef POS
    }
};
Model* modelSwitch = createModel<ByteSwitch, ByteSwitchWidget>("Switch");
