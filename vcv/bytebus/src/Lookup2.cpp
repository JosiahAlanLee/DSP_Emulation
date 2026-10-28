// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Lookup2.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteLookup2 : Module {
    enum Params { FAMILY, SHAPE, B_WORD, JUMP_BIT, RESET_BUTTON, NUM_PARAMS };
    enum Inputs { A_IN, B_IN, FAMILY_CV, SHAPE_CV, JUMP_IN, RESET_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, AUDIO_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT, BIT5_LIGHT,
        BIT6_LIGHT, BIT7_LIGHT, JUMP_LIGHT, NUM_LIGHTS };
    dspemu::lookup2::Engine engine;
    dspemu::bus::Reader readerA, readerB, jumpReader; dspemu::bus::Writer writer;
    dspemu::bus::Gate resetGate; bool resetButtonHigh = false;
    dspemu::lookup::Jump jump;
    std::atomic<int> jumpSteps{0};
    std::atomic<unsigned> displayedWord{0};

    ByteLookup2() {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        configSwitch(FAMILY,0,3,0,"Family",{"Logic","Arithmetic","Fold","Walsh"});
        configParam(SHAPE,0,64,0,"Shape (circular)");
        configParam(B_WORD,0,255,0,"B word (when B is unpatched)"); getParamQuantity(B_WORD)->snapEnabled = true;
        configSwitch(JUMP_BIT,0,7,0,"JUMP bit",{"Bit 0","Bit 1","Bit 2","Bit 3","Bit 4","Bit 5","Bit 6","Bit 7"});
        configButton(RESET_BUTTON,"Reset JUMP steps");
        configInput(A_IN,"A byte bus (its CLK is the output clock)");
        configInput(B_IN,"B byte bus (replaces the B word)");
        configInput(FAMILY_CV,"Family CV (1 V per family, clamped)");
        configInput(SHAPE_CV,"Shape CV (10 V per 64 shapes)");
        configInput(JUMP_IN,"Jump: SHAPE steps by one on each publication with the selected bit high (a gate is bit 0)");
        configInput(RESET_IN,"Reset JUMP steps");
        configOutput(BUS_OUT,"Result byte bus (9 channels when A is clocked, else 8)");
        configOutput(AUDIO_OUT,"Raw DAC (0x80 = 0 V; nominal ±5 V)");
    }
    void onReset(const ResetEvent& event) override {
        Module::onReset(event); engine.reset(); readerA.reset(); readerB.reset(); jumpReader.reset();
        resetGate.reset(); resetButtonHigh = false; jump.clear(); jumpSteps.store(0); displayedWord.store(0);
    }
    void process(const ProcessArgs& args) override {
        auto finite = [](float v) { return std::isfinite(v) ? v : 0.f; };
        auto cv = [&](int port) { return inputs[port].isConnected() ? finite(inputs[port].getVoltage()) : 0.f; };
        bool button = params[RESET_BUTTON].getValue() >= .5f;
        bool reset = resetGate.process(cv(RESET_IN)) | (button && !resetButtonHigh); resetButtonHigh = button;
        // JUMP is decoded before A, so a step applies on its own sample; RESET wins over it.
        int bit = int(std::round(clamp(finite(params[JUMP_BIT].getValue()),0.f,7.f)));
        bool stepped = jump.tick(jumpReader.process(inputs[JUMP_IN]),bit) && !reset;
        if (reset) jump.steps = 0;
        jumpSteps.store(jump.steps,std::memory_order_relaxed);
        dspemu::lookup2::Parameters p;
        p.family = int(std::round(clamp(finite(params[FAMILY].getValue()) + cv(FAMILY_CV),0.f,3.f)));
        p.shape = finite(params[SHAPE].getValue()) + 6.4 * cv(SHAPE_CV) + jump.steps;
        auto a = readerA.process(inputs[A_IN]);
        auto b = readerB.process(inputs[B_IN]);
        uint8_t bWord = b.connected ? b.rawWord : uint8_t(std::round(clamp(finite(params[B_WORD].getValue()),0.f,255.f)));
        auto out = engine.tick(p,a,bWord);
        writer.write(outputs[BUS_OUT],out.word,out.clocked,out.clk);
        outputs[AUDIO_OUT].setVoltage(dspemu::bus::toVolts(out.word));
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT+k].setBrightness(out.word & (1u<<k) ? 1.f : 0.f);
        lights[JUMP_LIGHT].setBrightnessSmooth(stepped ? 1.f : 0.f,args.sampleTime);
        displayedWord.store(out.word,std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root,"jump",json_integer(jumpSteps.load(std::memory_order_relaxed)));
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto* steps = json_object_get(root,"jump");
        int saved = json_is_integer(steps) ? int(std::max<json_int_t>(0,std::min<json_int_t>(63,json_integer_value(steps)))) : 0;
        resetGate.reset(); resetButtonHigh = false;
        engine.reset(); readerA.reset(); readerB.reset(); jumpReader.reset();
        jump.clear(); jump.steps = saved; jumpSteps.store(saved);
    }
};

struct Lookup2Display : widget::Widget {
    ByteLookup2* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font = APP->window->uiFont; if (!font) return;
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,16); nvgTextAlign(args.vg,NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(143,220,196));
        unsigned value = module ? module->displayedWord.load(std::memory_order_relaxed) : 0;
        nvgText(args.vg,box.size.x/2,box.size.y/2,string::f("%02X",value).c_str(),nullptr);
    }
};
struct ByteLookup2Widget : ModuleWidget {
    ByteLookup2Widget(ByteLookup2* module) {
        using namespace bytebus_layout::lookup2;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/Lookup2.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::lookup2::WIDTH-15,365)));
        auto* family = createParam<dspemu::panel::Choice>(POS(FAMILY),module,ByteLookup2::FAMILY);
        family->box.size = Vec(FAMILY_W,FAMILY_H);
        family->labels = {"LOGIC","ARITHMETIC","FOLD","WALSH"}; addParam(family);
        addParam(createParamCentered<RoundBlackKnob>(POS(SHAPE),module,ByteLookup2::SHAPE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(B_WORD),module,ByteLookup2::B_WORD));
        auto* jumpBit = createParam<dspemu::panel::Choice>(POS(JUMP_BIT),module,ByteLookup2::JUMP_BIT);
        jumpBit->box.size = Vec(JUMP_BIT_W,JUMP_BIT_H);
        jumpBit->labels = {"BIT 0","BIT 1","BIT 2","BIT 3","BIT 4","BIT 5","BIT 6","BIT 7"}; addParam(jumpBit);
        addParam(createParamCentered<VCVButton>(POS(RESET),module,ByteLookup2::RESET_BUTTON));
        const Vec bits[] = {POS(BIT0),POS(BIT1),POS(BIT2),POS(BIT3),POS(BIT4),POS(BIT5),POS(BIT6),POS(BIT7)};
        for (int k = 0; k < 8; ++k) addChild(createLightCentered<SmallLight<GreenLight>>(bits[k],module,ByteLookup2::BIT0_LIGHT+k));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(JUMP_LIGHT),module,ByteLookup2::JUMP_LIGHT));
        auto* display = createWidget<Lookup2Display>(POS(WORD)); display->box.size = Vec(WORD_W,WORD_H); display->module = module; addChild(display);
        addInput(createInputCentered<PJ301MPort>(POS(A_IN),module,ByteLookup2::A_IN));
        addInput(createInputCentered<PJ301MPort>(POS(B_IN),module,ByteLookup2::B_IN));
        addInput(createInputCentered<PJ301MPort>(POS(FAMILY_CV),module,ByteLookup2::FAMILY_CV));
        addInput(createInputCentered<PJ301MPort>(POS(SHAPE_CV),module,ByteLookup2::SHAPE_CV));
        addInput(createInputCentered<PJ301MPort>(POS(JUMP_IN),module,ByteLookup2::JUMP_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN),module,ByteLookup2::RESET_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT),module,ByteLookup2::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(AUDIO_OUT),module,ByteLookup2::AUDIO_OUT));
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<ByteLookup2*>(module); if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel(string::f("JUMP steps: +%d (RESET clears)",m->jumpSteps.load())));
    }
};
Model* modelLookup2 = createModel<ByteLookup2,ByteLookup2Widget>("Lookup2");
