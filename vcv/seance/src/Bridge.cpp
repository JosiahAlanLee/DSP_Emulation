// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "Layout.hpp"
#include "Chain.hpp"
#include "engine/Bridge.hpp"
#include "../../shared/ByteBus.hpp"

struct SeanceBridge : seance::Expander {
    enum Params { THRESHOLD, HYSTERESIS, SLEW, NUM_PARAMS };
    enum Inputs { BUS_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, NUM_OUTPUTS };
    enum Lights { LINK_LIGHT, BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT,
        BIT4_LIGHT, BIT5_LIGHT, BIT6_LIGHT, BIT7_LIGHT, NUM_LIGHTS };
    seance::SpectralBridge bridge;
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    const seance::FrameV1* linkedFrame = nullptr;  // identity only; never dereferenced

    SeanceBridge() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(THRESHOLD, -72, 0, -36, "Band activity threshold (0 dBFS = 10 V RMS)", " dBFS");
        configParam(HYSTERESIS, 0, 24, 6, "Activity hysteresis (release below threshold)", " dB");
        configParam(SLEW, 0, 250, 20, "Mask gain time constant", " ms");
        configInput(BUS_IN, "Band mask (bit 0 = lowest band; unpatched = all open)");
        configOutput(BUS_OUT, "Pre-mask band activity (9 channels; one-sample frame CLK)");
    }
    void clear() { bridge.clear(); reader.reset(); linkedFrame = nullptr; }
    void onReset(const ResetEvent& e) override { Module::onReset(e); clear(); }
    void onSampleRateChange(const SampleRateChangeEvent& e) override { clear(); }

    void step(bool bypassed) {
        int position = 0;
        auto* f = seance::findFrame(leftExpander.module, position);
        if (f != linkedFrame || bypassed) { clear(); linkedFrame = f; }
        shownPosition.store(f ? position : 0, std::memory_order_relaxed);
        bool published = false;
        if (f) {
            auto data = bypassed ? dspemu::bus::Sample{} : reader.process(inputs[BUS_IN]);
            uint8_t mask = data.connected ? data.word : 255;
            seance::BridgeParams p;
            p.thresholdDb = params[THRESHOLD].getValue();
            p.hysteresisDb = params[HYSTERESIS].getValue();
            p.slewMs = params[SLEW].getValue();
            bool last = !(rightExpander.module && seance::isExpander(rightExpander.module->model));
            seance::serve(*f, position, last, bypassed, [&](seance::FrameV1& frame) {
                bridge.process(frame, mask, p); published = true;
            });
        }
        writer.write(outputs[BUS_OUT], bridge.activity, true, published);
        lights[LINK_LIGHT].setBrightness(f ? 1.f : 0.f);
        for (int b = 0; b < 8; ++b)
            lights[BIT0_LIGHT + b].setBrightness(bridge.activity & (1u << b) ? 1.f : 0.f);
    }
    void process(const ProcessArgs& args) override { step(false); }
    void processBypass(const ProcessArgs& args) override { step(true); }
};

struct SeanceBridgeWidget : ModuleWidget {
    SeanceBridgeWidget(SeanceBridge* m) {
        using namespace seance_layout::bridge;
        setModule(m); setPanel(createPanel(asset::plugin(pluginInstance, "res/SeanceBridge.svg")));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(seance_layout::bridge::WIDTH - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_THRESHOLD_X, KNOB_THRESHOLD_Y), m, SeanceBridge::THRESHOLD));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_HYSTERESIS_X, KNOB_HYSTERESIS_Y), m, SeanceBridge::HYSTERESIS));
        addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_SLEW_X, KNOB_SLEW_Y), m, SeanceBridge::SLEW));
        addInput(createInputCentered<PJ301MPort>(Vec(JACK_BUS_IN_X, JACK_BUS_IN_Y), m, SeanceBridge::BUS_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(JACK_BUS_OUT_X, JACK_BUS_OUT_Y), m, SeanceBridge::BUS_OUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(LIGHT_LINK_LIGHT_X, LIGHT_LINK_LIGHT_Y), m, SeanceBridge::LINK_LIGHT));
        for (int b = 0; b < 8; ++b)
            addChild(createLightCentered<SmallLight<GreenLight>>(Vec(LIGHT_BIT0_LIGHT_X + b * (LIGHT_BIT7_LIGHT_X - LIGHT_BIT0_LIGHT_X) / 7, LIGHT_BIT0_LIGHT_Y), m, SeanceBridge::BIT0_LIGHT + b));
    }
    void appendContextMenu(Menu* menu) override {
        if (auto* m = dynamic_cast<SeanceBridge*>(module)) m->linkMenu(menu);
    }
};
Model* modelSeanceBridge = createModel<SeanceBridge, SeanceBridgeWidget>("SeanceBridge");
