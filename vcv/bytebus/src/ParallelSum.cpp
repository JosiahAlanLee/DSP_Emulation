// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/ParallelSum.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteParallelSum : Module {
    static constexpr int STAGES = dspemu::parallel_sum::STAGES;
    enum Params { SOURCE, SENSE, HOT, RATE, MASK, THRESHOLD, SEED, LOAD_BUTTON, WEIGHT, NUM_PARAMS = WEIGHT + STAGES };
    enum Inputs { CLOCK_IN, RESET_IN, INJECT_IN, MASK_IN, W_CV_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, GATE_OUT, CV_OUT, AUDIO_OUT, NUM_OUTPUTS };
    enum Lights { STAGE_LIGHT, CLOCK_LIGHT = STAGE_LIGHT + STAGES, RESET_LIGHT, HOT_LIGHT, INJECT_LIGHT, NUM_LIGHTS };
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    dspemu::parallel_sum::ParallelSum state;
    // The word in bits 0-7 and the mask in bits 8-15, for the displays; the word is saved.
    std::atomic<unsigned> published{0xb800};

    ByteParallelSum() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SOURCE, 0, 4, 0, "Clock source",
            {"Auto (CLOCK jack, then MASK bus CLK, then internal)", "CLOCK jack", "MASK bus CLK", "Internal", "Engine rate (unclocked bus)"});
        configSwitch(SENSE, 0, 1, 1, "Feedback sense", {"XOR (lockup 00)", "XNOR (lockup FF)"});
        configSwitch(HOT, 0, 1, 0, "Hot", {"Off (1.25 V a stage)", "On (6 V a stage, clipped at 10.5 V)"});
        configParam(RATE, -3, 11, 3, "Internal clock", " Hz", 2.f);
        configParam(MASK, 0, 255, 0xb8, "Feedback mask (stages tapped; replaced by the MASK bus when patched)");
        getParamQuantity(MASK)->snapEnabled = true;
        configParam(THRESHOLD, -5, 5, dspemu::parallel_sum::THRESHOLD, "INJECT threshold", " V");
        configParam(SEED, 0, 255, 0, "Seed word");
        getParamQuantity(SEED)->snapEnabled = true;
        configButton(LOAD_BUTTON, "Load seed and restart internal clock");
        for (int k = 0; k < STAGES; ++k) {
            configParam(WEIGHT + k, -1, 1, 1, string::f("Stage %d weight%s", k + 1, k == 0 ? " (newest bit)" : ""), "%", 0, 100);
            configLight(STAGE_LIGHT + k, string::f("Stage %d", k + 1));
        }
        configInput(CLOCK_IN, "Clock (rising edge)");
        configInput(RESET_IN, "Reset (loads the seed; takes priority over clock)");
        configInput(INJECT_IN, "Inject (XORed into the new bit through a comparator at THRESHOLD)");
        configInput(MASK_IN, "Mask byte bus (replaces the MASK knob; its CLK can clock the register)");
        configInput(W_CV_IN, "Weight CV (polyphonic: channel k adds 1 per 5 V to stage k + 1)");
        configOutput(BUS_OUT, "Register byte bus (9 channels when clocked, else 8)");
        configOutput(CLOCK_OUT, "Register clock");
        configOutput(GATE_OUT, "Gate (stage 1, the newest bit)");
        configOutput(CV_OUT, "CV (weighted sum of the set stages)");
        configOutput(AUDIO_OUT, "Audio (weighted sum, set stages +1, clear stages -1)");
        configLight(INJECT_LIGHT, "Inject comparator");
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); reader.reset(); state.clear(); published.store(0xb800);
    }
    void process(const ProcessArgs& args) override {
        namespace ps = dspemu::parallel_sum;
        using ps::ClockMode;
        auto finite = [](float v, float fallback) { return std::isfinite(v) ? v : fallback; };
        auto integer = [&](int id, float lo, float hi, float fallback) {
            return int(std::round(clamp(finite(params[id].getValue(), fallback), lo, hi)));
        };
        static constexpr ClockMode modes[] = {ClockMode::Auto, ClockMode::Jack, ClockMode::Bus, ClockMode::Internal, ClockMode::Engine};
        ps::Parameters p;
        p.mode = modes[integer(SOURCE, 0, 4, 0)];
        p.sense = integer(SENSE, 0, 1, 1) == 1 ? ps::Sense::Xnor : ps::Sense::Xor;
        p.hot = params[HOT].getValue() >= .5f;
        p.rateHz = std::exp2(clamp(finite(params[RATE].getValue(), 3), -3.f, 11.f));
        p.mask = uint8_t(integer(MASK, 0, 255, 0xb8));
        p.threshold = clamp(finite(params[THRESHOLD].getValue(), ps::THRESHOLD), -5.f, 5.f);
        p.seed = uint8_t(integer(SEED, 0, 255, 0));
        for (int k = 0; k < STAGES; ++k) p.weights[k] = clamp(finite(params[WEIGHT + k].getValue(), 0), -1.f, 1.f);
        ps::Inputs in;
        in.clock.data = reader.process(inputs[MASK_IN]);
        in.clock.clockConnected = inputs[CLOCK_IN].isConnected();
        in.clock.clock = inputs[CLOCK_IN].getVoltage();
        in.clock.reset = inputs[RESET_IN].isConnected() ? inputs[RESET_IN].getVoltage() : 0.f;
        in.clock.resetButton = params[LOAD_BUTTON].getValue() >= .5f;
        in.injectConnected = inputs[INJECT_IN].isConnected();
        in.inject = in.injectConnected ? inputs[INJECT_IN].getVoltage() : 0.f;
        int cvChannels = inputs[W_CV_IN].getChannels();
        for (int k = 0; k < STAGES; ++k) in.weightCv[k] = k < cvChannels ? inputs[W_CV_IN].getVoltage(k) : 0.f;
        auto out = state.tick(p, in, args.sampleRate);
        writer.write(outputs[BUS_OUT], out.word, out.clocked, out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? dspemu::bus::HIGH : 0.f);
        outputs[GATE_OUT].setVoltage(out.gate ? dspemu::bus::HIGH : 0.f);
        outputs[CV_OUT].setVoltage(out.cv);
        outputs[AUDIO_OUT].setVoltage(out.audio);
        for (int k = 0; k < STAGES; ++k) lights[STAGE_LIGHT + k].setBrightness(out.word >> k & 1 ? 1.f : 0.f);
        lights[CLOCK_LIGHT].setBrightnessSmooth(out.clk ? 1.f : 0.f, args.sampleTime);
        lights[RESET_LIGHT].setBrightnessSmooth(out.loaded ? 1.f : 0.f, args.sampleTime);
        lights[HOT_LIGHT].setBrightness(p.hot ? 1.f : 0.f);
        lights[INJECT_LIGHT].setBrightness(out.inject ? 1.f : 0.f);
        published.store(unsigned(out.word) | unsigned(out.mask) << 8, std::memory_order_relaxed);
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "word", json_integer(published.load(std::memory_order_relaxed) & 255));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* saved = json_object_get(root, "word");
        int value = json_is_integer(saved) ? int(std::max<json_int_t>(0, std::min<json_int_t>(255, json_integer_value(saved)))) : 0;
        reader.reset(); state.clear(uint8_t(value)); published.store(unsigned(value) | 0xb800u);
    }
};

struct ParallelSumDisplay : widget::Widget {
    ByteParallelSum* module = nullptr;
    int shift = 0;                      // 0: the word, 8: the mask
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgFontSize(args.vg, 16);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        unsigned value = module ? module->published.load(std::memory_order_relaxed) : 0xb800;
        std::string text = string::f("%02X", value >> shift & 255);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
    }
};
struct ByteParallelSumWidget : ModuleWidget {
    ByteParallelSumWidget(ByteParallelSum* module) {
        using namespace bytebus_layout::parallel_sum;
        using P = ByteParallelSum;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/ParallelSum.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::parallel_sum::WIDTH-15, 365)));
        auto* word = createWidget<ParallelSumDisplay>(POS(WORD));
        word->box.size = Vec(WORD_W, WORD_H); word->module = module; addChild(word);
        auto* mask = createWidget<ParallelSumDisplay>(POS(MASK_DISPLAY));
        mask->box.size = Vec(MASK_DISPLAY_W, MASK_DISPLAY_H); mask->module = module; mask->shift = 8; addChild(mask);
        auto* source = createParam<dspemu::panel::Choice>(POS(SOURCE), module, P::SOURCE);
        source->box.size = Vec(SOURCE_W, SOURCE_H);
        source->labels = {"AUTO", "JACK", "BUS CLK", "INTERNAL", "ENGINE"}; addParam(source);
        auto* sense = createParam<dspemu::panel::Choice>(POS(SENSE), module, P::SENSE);
        sense->box.size = Vec(SENSE_W, SENSE_H);
        sense->labels = {"XOR", "XNOR"}; addParam(sense);
        addParam(createParamCentered<RoundBlackKnob>(POS(RATE), module, P::RATE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(MASK), module, P::MASK));
        addParam(createParamCentered<RoundBlackKnob>(POS(THRESHOLD), module, P::THRESHOLD));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED), module, P::SEED));
        addParam(createParamCentered<VCVButton>(POS(LOAD), module, P::LOAD_BUTTON));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<YellowLight>>>(POS(HOT), module, P::HOT, P::HOT_LIGHT));
        const Vec weights[] = {POS(W0), POS(W1), POS(W2), POS(W3), POS(W4), POS(W5), POS(W6), POS(W7)};
        const Vec stages[] = {POS(STAGE0_LIGHT), POS(STAGE1_LIGHT), POS(STAGE2_LIGHT), POS(STAGE3_LIGHT),
            POS(STAGE4_LIGHT), POS(STAGE5_LIGHT), POS(STAGE6_LIGHT), POS(STAGE7_LIGHT)};
        for (int k = 0; k < P::STAGES; ++k) {
            addParam(createParamCentered<RoundSmallBlackKnob>(weights[k], module, P::WEIGHT + k));
            addChild(createLightCentered<SmallLight<GreenLight>>(stages[k], module, P::STAGE_LIGHT + k));
        }
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, P::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN), module, P::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(INJECT_IN), module, P::INJECT_IN));
        addInput(createInputCentered<PJ301MPort>(POS(MASK_IN), module, P::MASK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(W_CV_IN), module, P::W_CV_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT), module, P::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CLOCK_OUT), module, P::CLOCK_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(GATE_OUT), module, P::GATE_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(CV_OUT), module, P::CV_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(AUDIO_OUT), module, P::AUDIO_OUT));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(RESET_LIGHT), module, P::RESET_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(CLOCK_LIGHT), module, P::CLOCK_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(INJECT_LIGHT), module, P::INJECT_LIGHT));
#undef POS
    }
};
Model* modelParallelSum = createModel<ByteParallelSum, ByteParallelSumWidget>("ParallelSum");
