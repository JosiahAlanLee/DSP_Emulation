// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Pipeline.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct BytePipeline : Module {
    enum Params { CLOCK_MODE, RATE, DELAY, SPREAD, LOOP_BUTTON, CLEAR_BUTTON, NUM_PARAMS };
    enum Inputs { BUS_IN, CLOCK_IN, RESET_IN, LOOP_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT,
        BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, CLOCK_LIGHT, RESET_LIGHT, LOOP_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    dspemu::bus::Gate loopGate;
    dspemu::pipeline::Pipeline state;
    std::atomic<unsigned> displayedWord{0};
    // The history for saving, mirrored from the audio thread under a sequence lock.
    std::array<std::atomic<uint8_t>, dspemu::pipeline::STAGES> saved{};
    std::atomic<int> savedHead{0};
    std::atomic<uint32_t> sequence{0};

    BytePipeline() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(CLOCK_MODE, 0, 4, 0, "Clock source",
            {"Auto (CLOCK jack, then bus CLK, then internal)", "CLOCK jack", "Bus CLK", "Internal", "Engine rate (unclocked bus)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(DELAY, 1, 64, 16, "Delay", " edges");
        configParam(SPREAD, 0, 8, 0, "Spread (bit k is delayed k x SPREAD more edges)", " edges per bit");
        for (int id : {DELAY, SPREAD}) getParamQuantity(id)->snapEnabled = true;
        configSwitch(LOOP_BUTTON, 0, 1, 0, "Loop", {"Off", "On (the last DELAY words repeat)"});
        configButton(CLEAR_BUTTON, "Clear the history to 00 and restart the internal clock");
        configInput(BUS_IN, "Byte bus (optional CLK on channel 9)");
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (clears the history to 00; takes priority over clock)");
        configInput(LOOP_IN, "Loop gate (loops while high, as well as the LOOP button)");
        configOutput(BUS_OUT, "Delayed byte bus (9 channels; 8 in Engine mode)");
        configOutput(CLOCK_OUT, "Published-word clock (one sample pulses)");
    }
    void mirror(bool all) {
        uint32_t s = sequence.load(std::memory_order_relaxed);
        sequence.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        if (all) for (int k = 0; k < dspemu::pipeline::STAGES; ++k) saved[k].store(state.ring[k], std::memory_order_relaxed);
        else saved[state.head].store(state.ring[state.head], std::memory_order_relaxed);
        savedHead.store(state.head, std::memory_order_relaxed);
        sequence.store(s + 2, std::memory_order_release);
    }
    void restore(const std::array<uint8_t, dspemu::pipeline::STAGES>& oldestFirst) {
        reader.reset(); loopGate.reset(); state.clear();
        state.ring = oldestFirst; state.head = dspemu::pipeline::STAGES - 1;
        mirror(true);
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); reader.reset(); loopGate.reset(); state.clear(); mirror(true); displayedWord.store(0);
    }
    void process(const ProcessArgs& args) override {
        using namespace dspemu::pipeline;
        Parameters p;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        p.mode = ClockMode(integer(CLOCK_MODE, 0, 4, 0));
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.delay = integer(DELAY, 1, 64, 16);
        p.spread = integer(SPREAD, 0, 8, 0);
        loopGate.process(inputs[LOOP_IN].isConnected() ? inputs[LOOP_IN].getVoltage() : 0.f);
        p.loop = params[LOOP_BUTTON].getValue() >= .5f || loopGate.high;
        dspemu::pipeline::Inputs in;
        in.data = reader.process(inputs[BUS_IN]);
        in.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock = inputs[CLOCK_IN].getVoltage();
        in.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.resetButton = params[CLEAR_BUTTON].getValue() >= .5f;
        auto out = state.tick(p, in, args.sampleRate);
        if (out.cleared) mirror(true);
        else if (out.advanced) mirror(false);
        writer.write(outputs[BUS_OUT], out.word, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT + k].setBrightness(out.word & (1u << k) ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.cleared ? 1.f : 0.f, args.sampleTime);
        lights[LOOP_LIGHT].setBrightness(p.loop ? 1.f : 0.f);
        displayedWord.store(out.word, std::memory_order_relaxed);
    }
    // The history, oldest first: the word that entered 127 edges ago through the latest.
    std::array<uint8_t, dspemu::pipeline::STAGES> history() const {
        std::array<uint8_t, dspemu::pipeline::STAGES> raw{}, out{};
        int head = 0; uint32_t before, after;
        do {
            before = sequence.load(std::memory_order_acquire);
            for (int k = 0; k < dspemu::pipeline::STAGES; ++k) raw[k] = saved[k].load(std::memory_order_relaxed);
            head = savedHead.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            after = sequence.load(std::memory_order_relaxed);
        } while (before != after || (before & 1));
        for (int k = 0; k < dspemu::pipeline::STAGES; ++k) out[k] = raw[(head + 1 + k) & (dspemu::pipeline::STAGES - 1)];
        return out;
    }
    json_t* dataToJson() override {
        json_t* root = json_object(); json_t* words = json_array();
        for (uint8_t w : history()) json_array_append_new(words, json_integer(w));
        json_object_set_new(root, "history", words);
        return root;
    }
    void dataFromJson(json_t* root) override {
        std::array<uint8_t, dspemu::pipeline::STAGES> words{};
        json_t* list = json_object_get(root, "history");
        size_t n = json_is_array(list) ? std::min<size_t>(json_array_size(list), words.size()) : 0;
        // A shorter list fills the most recent stages; values are bounded to a byte.
        for (size_t k = 0; k < n; ++k) {
            json_t* v = json_array_get(list, json_array_size(list) - n + k);
            json_int_t w = json_is_integer(v) ? json_integer_value(v) : 0;
            words[words.size() - n + k] = uint8_t(std::max<json_int_t>(0, std::min<json_int_t>(255, w)));
        }
        restore(words);
    }
};

struct PipelineDisplay : widget::Widget {
    BytePipeline* module = nullptr;
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
struct BytePipelineWidget : ModuleWidget {
    BytePipelineWidget(BytePipeline* module) {
        using namespace bytebus_layout::pipeline;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/Pipeline.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::pipeline::WIDTH-15, 365)));
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, BytePipeline::CLOCK_MODE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "BUS", "INTERNAL", "ENGINE"}; addParam(source);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, BytePipeline::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(DELAY), module, BytePipeline::DELAY));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SPREAD), module, BytePipeline::SPREAD));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(POS(LOOP), module,
            BytePipeline::LOOP_BUTTON, BytePipeline::LOOP_LIGHT));
        addParam(createParamCentered<VCVButton>(POS(CLEAR), module, BytePipeline::CLEAR_BUTTON));
        const Vec bits[] = {POS(BIT0), POS(BIT1), POS(BIT2), POS(BIT3), POS(BIT4), POS(BIT5), POS(BIT6), POS(BIT7)};
        for (int k = 0; k < 8; ++k)
            addChild(createLightCentered<SmallLight<GreenLight>>(bits[k], module, BytePipeline::BIT0_LIGHT + k));
        auto* display = createWidget<PipelineDisplay>(POS(WORD));
        display->box.size = Vec(WORD_W, WORD_H); display->module = module; addChild(display);
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN), module, BytePipeline::BUS_IN));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, BytePipeline::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, BytePipeline::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(LOOP_IN), module, BytePipeline::LOOP_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, BytePipeline::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, BytePipeline::CLOCK_OUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, BytePipeline::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, BytePipeline::CLOCK_LIGHT));
#undef POS
    }
};
Model* modelPipeline = createModel<BytePipeline, BytePipelineWidget>("Pipeline");
