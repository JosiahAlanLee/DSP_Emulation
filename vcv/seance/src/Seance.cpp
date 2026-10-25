// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "Layout.hpp"
#include "Chain.hpp"
using namespace seance;
using namespace seance_layout::core;

struct Seance : Module {
    enum Params { SIZE, SEED, MIX, RESET, NUM_PARAMS };
    enum Inputs { A_IN, B_IN, RESET_IN, SEED_CV, MIX_CV, NUM_INPUTS };
    enum Outputs { OUT, NUM_OUTPUTS };
    enum Lights { CHAIN_LIGHT, STALL_LIGHT, NUM_LIGHTS };

    Core core;
    dsp::SchmittTrigger resetTrigger, resetButton;
    std::atomic<int> shownLatency{0}, shownLength{0}, shownStalls{0};

    struct SizeQuantity : ParamQuantity {
        std::string getDisplayValueString() override { return string::f("%d", sizeOf(int(std::round(getValue())))); }
        void setDisplayValue(float v) override {
            int best = 0; for (int i = 1; i < SIZES; i++) if (std::abs(v - sizeOf(i)) < std::abs(v - sizeOf(best))) best = i;
            setValue(float(best));
        }
    };

    Seance() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam<SizeQuantity>(SIZE, 0, SIZES - 1, 2, "Frame size", " samples");
        getParamQuantity(SIZE)->snapEnabled = true;
        configParam(SEED, 0, 999, 0, "Seed");
        getParamQuantity(SEED)->snapEnabled = true;
        configParam(MIX, 0, 1, 1, "Dry / wet", "%", 0, 100);
        configButton(RESET, "Reset (frame 0, frame grid restarts here)");
        configInput(A_IN, "A (the signal)"); configInput(B_IN, "B (reference for Cross)");
        configInput(RESET_IN, "Reset trigger"); configInput(SEED_CV, "Seed CV (1 V per 100, added to the knob)");
        configInput(MIX_CV, "Dry / wet CV");
        configOutput(OUT, "Out");
        configBypass(A_IN, OUT);
        // The frame is published on this module's own right-hand slots; never flipped.
        rightExpander.producerMessage = rightExpander.consumerMessage = &core.frame;
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override { core.prepare(e.sampleRate); }
    void onReset(const ResetEvent& e) override { Module::onReset(e); core.clear(); }

    float cv(int input, float scale) { return inputs[input].isConnected() ? inputs[input].getVoltage() * scale : 0.f; }

    void process(const ProcessArgs& args) override {
        CoreParams p;
        p.sizeIndex = int(std::round(params[SIZE].getValue()));
        core.setSize(p.sizeIndex);
        int seed = int(std::round(params[SEED].getValue() + cv(SEED_CV, 100.f)));
        p.seed = uint32_t(((seed % 1000) + 1000) % 1000);
        p.mix = clamp(params[MIX].getValue() + cv(MIX_CV, .1f), 0.f, 1.f);
        if (resetTrigger.process(inputs[RESET_IN].getVoltage(), .1f, 1.f) | resetButton.process(params[RESET].getValue())) core.reset();
        bool chain = rightExpander.module && isExpander(rightExpander.module->model);
        // Rack audio is ±5 V nominal; ±10 V maps to ±1 internally (6 dB headroom).
        float a = inputs[A_IN].getVoltageSum() * .1f, b = inputs[B_IN].getVoltageSum() * .1f;
        outputs[OUT].setVoltage(core.tick(a, b, p, chain) * 10.f);
        lights[CHAIN_LIGHT].setBrightness(chain ? 1.f : 0.f);
        lights[STALL_LIGHT].setBrightnessSmooth(core.stalled ? 1.f : 0.f, args.sampleTime);
        if ((args.frame & 1023) == 0) {
            shownLatency.store(core.latency(), std::memory_order_relaxed);
            shownLength.store(chain ? core.frame.length.load(std::memory_order_relaxed) : 0, std::memory_order_relaxed);
            shownStalls.store(core.stalls, std::memory_order_relaxed);
        }
    }
};

struct SeanceWidget : ModuleWidget {
    SeanceWidget(Seance* m) {
        setModule(m);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Seance.svg")));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addParam(createParamCentered<RoundBlackSnapKnob>(Vec(KNOB_SIZE_X, KNOB_SIZE_Y), m, Seance::SIZE));
        addParam(createParamCentered<RoundBlackSnapKnob>(Vec(KNOB_SEED_X, KNOB_SEED_Y), m, Seance::SEED));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_MIX_X, KNOB_MIX_Y), m, Seance::MIX));
        addParam(createParamCentered<VCVButton>(Vec(BUTTON_RESET_X, BUTTON_RESET_Y), m, Seance::RESET));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(LIGHT_CHAIN_LIGHT_X, LIGHT_CHAIN_LIGHT_Y), m, Seance::CHAIN_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(LIGHT_STALL_LIGHT_X, LIGHT_STALL_LIGHT_Y), m, Seance::STALL_LIGHT));
#define IN(N) addInput(createInputCentered<PJ301MPort>(Vec(JACK_##N##_X, JACK_##N##_Y), m, Seance::N))
        IN(A_IN); IN(B_IN); IN(RESET_IN); IN(SEED_CV); IN(MIX_CV);
#undef IN
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(JACK_OUT_X, JACK_OUT_Y), m, Seance::OUT));
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<Seance*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        int lat = m->shownLatency.load();
        menu->addChild(createMenuLabel(string::f("Latency: %d samples (%.1f ms at %.0f Hz), dry path delayed to match", lat, 1000.f * lat / APP->engine->getSampleRate(), APP->engine->getSampleRate())));
        int len = m->shownLength.load();
        menu->addChild(createMenuLabel(len ? string::f("Chain: %d expander%s to the right, applied left to right", len, len == 1 ? "" : "s") : "Chain: none (place Séance expanders directly to the right)"));
        menu->addChild(createMenuLabel(string::f("Frames returned late: %d", m->shownStalls.load())));
    }
};

Model* modelSeance = createModel<Seance, SeanceWidget>("Seance");
