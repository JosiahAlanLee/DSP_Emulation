// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Fan.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteFan : Module {
    enum Params { SOURCE, DIR, UNSELECTED, GLITCH, RATE, LENGTH, SEED, LOAD_BUTTON, NUM_PARAMS };
    enum Inputs { CLOCK_IN, RESET_IN, DIR_IN, BUS_IN, COM_IN, IN_A, IN_B, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, EOC_OUT, COM_OUT, OUT_A, OUT_B, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        CLOCK_LIGHT, EOC_LIGHT, RESET_LIGHT, GLITCH_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    dspemu::fan::Fan state;
    // The published address, with bit 8 set while BUS IN owns it; the internal count is saved.
    std::atomic<unsigned> published{0};
    std::atomic<unsigned> count{0};

    ByteFan() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SOURCE, 0, 3, 0, "Clock source",
            {"Auto (CLOCK jack, else internal)", "CLOCK jack", "Internal", "Engine rate (unclocked bus)"});
        configSwitch(DIR, 0, 1, 0, "Direction", {"Up", "Down"});
        configSwitch(UNSELECTED, 0, 1, 0, "Unselected outputs", {"Zero (0 V)", "Hold (last value carried)"});
        configSwitch(GLITCH, 0, 1, 0, "Glitch", {"Off", "On (late enable: overlap or dropout when bit 4 changes)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(LENGTH, 1, 32, 32, "Length (steps)");
        configParam(SEED, 0, 31, 0, "Seed step", "", 0.f, 1.f, 1.f);
        for (int id : {LENGTH, SEED}) getParamQuantity(id)->snapEnabled = true;
        configButton(LOAD_BUTTON, "Load seed and restart internal clock");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (loads the seed step; takes priority over clock)");
        configInput(DIR_IN, "Direction gate (inverts the DIR switch while high)");
        configInput(BUS_IN, "Address byte bus (bits 0-4; replaces the internal counter)");
        configInput(COM_IN, "Common input (routed to the selected output; unpatched, 10 V)");
        configInput(IN_A, "Inputs 1-16 (16-channel polyphonic)");
        configInput(IN_B, "Inputs 17-32 (16-channel polyphonic)");
        configOutput(BUS_OUT, "Address byte bus (9 channels when clocked, else 8)");
        configOutput(CLOCK_OUT, "Published-address clock");
        configOutput(EOC_OUT, "End of cycle (one sample pulse on each counter wrap)");
        configOutput(COM_OUT, "Common output (the selected input)");
        configOutput(OUT_A, "Outputs 1-16 (16-channel polyphonic)");
        configOutput(OUT_B, "Outputs 17-32 (16-channel polyphonic)");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); reader.reset(); state.clear(); published.store(0); count.store(0);
    }
    void process(const ProcessArgs& args) override {
        namespace fn = dspemu::fan;
        using fn::ClockMode;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        static constexpr ClockMode modes[] = {ClockMode::Auto, ClockMode::Jack, ClockMode::Internal, ClockMode::Engine};
        fn::Parameters p;
        p.mode = modes[integer(SOURCE, 0, 3, 0)];
        p.down = integer(DIR, 0, 1, 0) == 1;
        p.unselected = integer(UNSELECTED, 0, 1, 0) == 1 ? fn::Unselected::Hold : fn::Unselected::Zero;
        p.glitch = params[GLITCH].getValue() >= .5f;
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.length = integer(LENGTH, 1, 32, 32);
        p.seed = uint8_t(integer(SEED, 0, 31, 0));
        fn::Inputs in;
        in.address = reader.process(inputs[BUS_IN]);
        in.clock.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock.clock = inputs[CLOCK_IN].getVoltage();
        in.clock.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.clock.resetButton = params[LOAD_BUTTON].getValue() >= .5f;
        in.dir = inputs[DIR_IN].isConnected() ? inputs[DIR_IN].getVoltage() : 0.f;
        in.comConnected = inputs[COM_IN].isConnected();
        in.com = in.comConnected ? inputs[COM_IN].getVoltage() : 0.f;
        for (int half = 0; half < 2; ++half) {
            Input& poly = inputs[half ? IN_B : IN_A];
            int channels = poly.getChannels();
            for (int ch = 0; ch < fn::HALF; ++ch) {
                float v = ch < channels ? poly.getVoltage(ch) : 0.f;
                in.in[half * fn::HALF + ch] = std::isfinite(v) ? v : 0.f;
            }
        }
        if (!std::isfinite(in.com)) in.com = 0.f;
        auto out = state.tick(p, in, args.sampleRate);
        writer.write(outputs[BUS_OUT], out.address, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        outputs[EOC_OUT].setVoltage(out.eoc ? dspemu::bus::HIGH : 0.f);
        outputs[COM_OUT].setVoltage(out.com);
        for (int half = 0; half < 2; ++half) {
            Output& poly = outputs[half ? OUT_B : OUT_A];
            poly.setChannels(fn::HALF);
            for (int ch = 0; ch < fn::HALF; ++ch) poly.setVoltage(out.out[half * fn::HALF + ch], ch);
        }
        for (int k = 0; k < 5; ++k) lights[BIT0_LIGHT + k].setBrightness(out.address & (1u << k) ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[EOC_LIGHT].setBrightnessSmooth(out.eoc ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        lights[GLITCH_LIGHT].setBrightness(p.glitch ? 1.f : 0.f);
        published.store(out.address | (out.following ? 0x100u : 0u), std::memory_order_relaxed);
        count.store(state.count, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "count", json_integer(count.load(std::memory_order_relaxed) & 31));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* saved = json_object_get(root, "count");
        int value = json_is_integer(saved) ? int(std::max<json_int_t>(0, std::min<json_int_t>(31, json_integer_value(saved)))) : 0;
        reader.reset(); state.clear(uint8_t(value)); count.store(unsigned(value)); published.store(unsigned(value));
    }
};

struct FanDisplay : widget::Widget {
    ByteFan* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        unsigned value = module ? module->published.load(std::memory_order_relaxed) : 0;
        std::string text = value & 0x100 ? string::f("BUS %u", (value & 31) + 1) : string::f("%u", (value & 31) + 1);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteFanWidget : ModuleWidget {
    ByteFanWidget(ByteFan* module) {
        using namespace bytebus_layout::fan;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Fan.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::fan::WIDTH-15, 365)));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, ByteFan::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "INTERNAL", "ENGINE"}; addParam(source);
        auto* dir = createParam<dspemu::panel::Choice>(POS(DIR), module, ByteFan::DIR);
        dir->box.size = Vec(DIR_W, DIR_H);
        dir->labels = {"UP", "DOWN"}; addParam(dir);
        auto* unselected = createParam<dspemu::panel::Choice>(POS(UNSELECTED), module, ByteFan::UNSELECTED);
        unselected->box.size = Vec(UNSELECTED_W, UNSELECTED_H);
        unselected->labels = {"ZERO", "HOLD"}; addParam(unselected);
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(POS(GLITCH), module,
            ByteFan::GLITCH, ByteFan::GLITCH_LIGHT));
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, ByteFan::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(LENGTH), module, ByteFan::LENGTH));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED), module, ByteFan::SEED));
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, ByteFan::LOAD_BUTTON));
        const Vec bits[] = {POS(BIT0), POS(BIT1), POS(BIT2), POS(BIT3), POS(BIT4)};
        for (int k = 0; k < 5; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, ByteFan::BIT0_LIGHT + k));
        auto* display = createWidget<FanDisplay>(POS(ADDRESS));
        display->box.size = Vec(ADDRESS_W, ADDRESS_H); display->module = module; addChild(display);
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, ByteFan::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, ByteFan::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(DIR_IN), module, ByteFan::DIR_IN));
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN), module, ByteFan::BUS_IN));
        addInput(createInputCentered<PJ301MPort>(POS(COM_IN), module, ByteFan::COM_IN));
        addInput(createInputCentered<PJ301MPort>(POS(IN_A), module, ByteFan::IN_A));
        addInput(createInputCentered<PJ301MPort>(POS(IN_B), module, ByteFan::IN_B));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, ByteFan::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, ByteFan::CLOCK_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(EOC_OUT), module, ByteFan::EOC_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(COM_OUT), module, ByteFan::COM_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(OUT_A), module, ByteFan::OUT_A));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(OUT_B), module, ByteFan::OUT_B));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, ByteFan::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, ByteFan::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(EOC_LIGHT), module, ByteFan::EOC_LIGHT));
#undef POS
    }
};
Model* modelFan = createModel<ByteFan, ByteFanWidget>("Fan");
