// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "../../shared/DacStage.hpp"
#include "../../shared/ByteBus.hpp"
#include "../../shared/PanelWidgets.hpp"
#include "Layout.hpp"
#include <array>
#include <atomic>

struct DacStageModule : Module {
    enum Params {
        PROFILE,
        BITS,
        RATE,
        DRIVE,
        TRACK,
        LEVEL,
        TONE,
        AA,
        RECON,
        NOISE,
        CZ_MODEL,
        ROUND,
        YM_CLOCK,
        NUM_PARAMS
    };
    enum Inputs { SIGNAL, RATE_CV, DRIVE_CV, XOR_BUS, BITS_CV, NUM_INPUTS };
    enum Outputs { AUDIO, BUS_OUT, NUM_OUTPUTS };
    std::array<dspemu::dac::Stage, 16> stages{};
    std::array<float, 16> filterA{}, filterB{};
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    std::atomic<unsigned> displayed{0};
    float lastTone = -1, lastRate = -1, alpha = 1;
    int lastProfile = 0;
    std::atomic<float> activeRate{55930.39f};
    std::atomic<int> activeBits{12};

    DacStageModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(PROFILE, 0, 2, 0, "Converter profile",
                     {"YM floating point", "CZ model (provisional)", "Linear"});
        configParam(BITS, 4, 16, 12, "Linear resolution", " bits");
        getParamQuantity(BITS)->snapEnabled = true;
        configParam(RATE, 1000, 96000, 55930.39f, "Device clock (YM and Linear)", " Hz");
        configParam(DRIVE, -24, 24, 0, "Input drive", " dB");
        configParam(TRACK, -.5f, .5f, 0, "Provisional CZ range tracking error", "%", 0, 100);
        configParam(LEVEL, -24, 12, 0, "Output level", " dB");
        configParam(TONE, 1000, 24000, 24000, "Output low-pass; maximum bypasses", " Hz");
        configSwitch(AA, 0, 1, 0, "Input anti-alias filter", {"Off (Hermite)", "Sinc and half-band cascade"});
        configSwitch(RECON, 0, 1, 1, "Reconstruction", {"Minimum-phase band-limited hold", "Clean sinc"});
        configParam(NOISE, -120, -40, -120, "Analog-side noise; minimum is off", " dBFS RMS");
        configSwitch(CZ_MODEL, 0, 1, 0, "Provisional Casio compander",
                     {"Instantaneous", "Syllabic (1 ms attack / 50 ms release)"});
        configSwitch(ROUND, 0, 1, 0, "Linear rounding", {"Floor", "Nearest; ties away from zero"});
        configParam(YM_CLOCK, 1000000, 8000000, 3579545, "YM chip clock", " Hz");
        configInput(SIGNAL, "Polyphonic audio; ±5 V = full scale");
        configInput(RATE_CV, "Clock rate CV; 1 V/octave (YM and Linear)");
        configInput(DRIVE_CV, "Drive CV; 6 dB per volt");
        configInput(XOR_BUS, "XOR mask for the encoded high byte; optional CLK on channel 9");
        configInput(BITS_CV, "Linear resolution; one bit per volt");
        configOutput(AUDIO, "Polyphonic converted audio");
        configBypass(SIGNAL, AUDIO);
        configOutput(BUS_OUT,
                     "Voice 1 encoded high byte after XOR; channel 9 is a safely divided device clock");
        for (size_t c = 0; c < stages.size(); ++c) {
            stages[c].noiseSeed = 0x444143 + 127 * c;
            stages[c].reset();
        }
    }

    static float finite(float x, float fallback = 0) { return std::isfinite(x) ? x : fallback; }
    void process(const ProcessArgs &args) override {
        using namespace dspemu::dac;
        int profileIndex = int(std::lround(std::clamp(finite(params[PROFILE].getValue()), 0.f, 2.f)));
        auto profile = Profile(profileIndex);
        int bits = int(std::lround(std::clamp(
            finite(params[BITS].getValue(), 12) + finite(inputs[BITS_CV].getVoltage()), 4.f, 16.f)));
        double host = std::isfinite(args.sampleRate) && args.sampleRate >= 1000 ? args.sampleRate : 48000.;
        double rate =
            profile == Profile::Casio
                ? 40000.
                : std::clamp(double(finite(params[RATE].getValue(), 55930)) *
                                 std::exp2(std::clamp(double(finite(inputs[RATE_CV].getVoltage())), -8., 8.)),
                             1000., 192000.);
        if (profile == Profile::Yamaha) {
            double base = finite(params[YM_CLOCK].getValue(), 3579545) / 64.;
            rate = std::clamp(rate * base / (3579545. / 64.), base * .25, std::min(192000., base * 4));
        }
        if (profileIndex != lastProfile) {
            if (params[TONE].getValue() == (lastProfile == 1 ? 20000.f : 24000.f))
                params[TONE].setValue(profileIndex == 1 ? 20000.f : 24000.f);
            if (profileIndex == 2 && std::abs(params[RATE].getValue() - 55930.39f) < 1)
                params[RATE].setValue(float(host));
            lastProfile = profileIndex;
        }
        activeRate.store(float(rate), std::memory_order_relaxed);
        activeBits.store(bits, std::memory_order_relaxed);
        float driveDb = std::clamp(
            finite(params[DRIVE].getValue()) + 6 * finite(inputs[DRIVE_CV].getVoltage()), -24.f, 24.f);
        float drive = std::pow(10.f, driveDb / 20.f);
        float level = std::pow(10.f, std::clamp(finite(params[LEVEL].getValue()), -24.f, 12.f) / 20.f);
        float tone = std::clamp(finite(params[TONE].getValue(), 24000), 1000.f, 24000.f);
        float track = std::clamp(finite(params[TRACK].getValue()), -.5f, .5f);
        bool aa = params[AA].getValue() >= .5f, clean = params[RECON].getValue() >= .5f;
        if (tone != lastTone || args.sampleRate != lastRate) {
            alpha = 1.f - std::exp(float(-2. * 3.141592653589793 * tone / host));
            lastTone = tone;
            lastRate = args.sampleRate;
        }
        auto bus = reader.process(inputs[XOR_BUS]);
        uint8_t mask = bus.connected ? bus.word : 0;
        int channels = std::clamp(inputs[SIGNAL].getChannels(), 1, 16);
        outputs[AUDIO].setChannels(channels);
        for (int c = 0; c < channels; ++c) {
            stages[c].czModel = params[CZ_MODEL].getValue() >= .5f ? CzModel::Syllabic : CzModel::Instant;
            stages[c].nearest = params[ROUND].getValue() >= .5f;
            stages[c].noiseDb = std::clamp(finite(params[NOISE].getValue(), -120), -120.f, -40.f);
            float input = finite(inputs[SIGNAL].getPolyVoltage(c)) * (.2f * drive);
            float converted = stages[c].process(input, profile, bits, mask, track, rate, host, aa, clean);
            if (tone < 23999.f) {
                filterA[c] += alpha * (converted - filterA[c]);
                filterB[c] += alpha * (filterA[c] - filterB[c]);
                converted = filterB[c];
            } else
                filterA[c] = filterB[c] = converted;
            outputs[AUDIO].setVoltage(std::clamp(5.f * converted * level, -10.f, 10.f), c);
        }
        const auto &tap = stages[0].tap;
        writer.write(outputs[BUS_OUT], tap.word, true, tap.clk);
        displayed.store(unsigned(tap.word) | (tap.divider << 8), std::memory_order_relaxed);
    }
    json_t *dataToJson() override {
        auto *j = json_object();
        json_object_set_new(j, "schema", json_integer(2));
        return j;
    }
    void dataFromJson(json_t *) override { lastProfile = int(params[PROFILE].getValue()); }
    void onReset(const ResetEvent &e) override {
        Module::onReset(e);
        for (auto &s : stages)
            s.reset();
        filterA = {};
        filterB = {};
        reader.reset();
        displayed.store(0);
        lastTone = lastRate = -1;
    }
};

struct DacStageDisplay : widget::Widget {
    DacStageModule *module = nullptr;
    void draw(const DrawArgs &args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23));
        nvgFill(args.vg);
        auto font = APP->window->uiFont;
        if (!font)
            return;
        unsigned value = module ? module->displayed.load(std::memory_order_relaxed) : (3u << 8);
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, 12);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, nvgRGB(143, 220, 196));
        nvgText(args.vg, box.size.x / 2, box.size.y / 2,
                string::f("%02X / %u", value & 255, value >> 8).c_str(), nullptr);
    }
};
struct DacRateDisplay : widget::Widget {
    DacStageModule *module = nullptr;
    void draw(const DrawArgs &a) override {
        auto font = APP->window->uiFont;
        if (!font)
            return;
        nvgFontFaceId(a.vg, font->handle);
        nvgFontSize(a.vg, 9);
        nvgTextAlign(a.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(a.vg, nvgRGB(143, 220, 196));
        float rate = module ? module->activeRate.load() : 55930.39f;
        int bits = module ? module->activeBits.load() : 12;
        nvgText(a.vg, box.size.x / 2, box.size.y / 2, string::f("%.0f Hz  /  %d BIT", rate, bits).c_str(),
                nullptr);
    }
};
struct DacStageWidget : ModuleWidget {
    DacStageWidget(DacStageModule *module) {
        using namespace czish_layout::dac;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/DacStage.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(czish_layout::dac::WIDTH - 15, 365)));
        auto *profile = createParam<dspemu::panel::Choice>(POS(PROFILE), module, DacStageModule::PROFILE);
        profile->box.size = Vec(PROFILE_W, PROFILE_H);
        profile->labels = {"YM FP", "CZ MODEL", "LINEAR"};
        addParam(profile);
        const Vec knobs[] = {POS(BITS), POS(RATE), POS(DRIVE), POS(TRACK), POS(LEVEL), POS(TONE)};
        for (int i = 0; i < 6; ++i)
            addParam(createParamCentered<RoundBlackKnob>(knobs[i], module, DacStageModule::BITS + i));
        auto *display = createWidget<DacStageDisplay>(POS(WORD));
        display->box.size = Vec(WORD_W, WORD_H);
        display->module = module;
        addChild(display);
        addParam(createParamCentered<CKSS>(POS(AA), module, DacStageModule::AA));
        addParam(createParamCentered<CKSS>(POS(RECON), module, DacStageModule::RECON));
        const Vec ins[] = {POS(SIGNAL), POS(RATE_CV), POS(DRIVE_CV), POS(XOR_BUS)};
        const Vec outs[] = {POS(AUDIO), POS(BUS_OUT)};
        for (int i = 0; i < 4; ++i)
            addInput(createInputCentered<PJ301MPort>(ins[i], module, i));
        for (int i = 0; i < 2; ++i)
            addOutput(createOutputCentered<DarkPJ301MPort>(outs[i], module, i));
        addInput(createInputCentered<PJ301MPort>(POS(BITS_CV), module, DacStageModule::BITS_CV));
        addParam(createParamCentered<RoundBlackKnob>(POS(NOISE), module, DacStageModule::NOISE));
        auto *rateDisplay = createWidget<DacRateDisplay>(POS(RATE_READ));
        rateDisplay->box.size = Vec(RATE_READ_W, RATE_READ_H);
        rateDisplay->module = module;
        addChild(rateDisplay);
#undef POS
    }
    void appendContextMenu(Menu *menu) override {
        auto *m = dynamic_cast<DacStageModule *>(module);
        if (!m)
            return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createIndexSubmenuItem(
            "Casio model (provisional)", {"Instantaneous", "Syllabic: 1 ms / 50 ms"},
            [m] { return int(m->params[DacStageModule::CZ_MODEL].getValue()); },
            [m](int v) { m->params[DacStageModule::CZ_MODEL].setValue(float(v)); }));
        menu->addChild(createIndexSubmenuItem(
            "Linear rounding", {"Floor", "Nearest (ties away from zero)"},
            [m] { return int(m->params[DacStageModule::ROUND].getValue()); },
            [m](int v) { m->params[DacStageModule::ROUND].setValue(float(v)); }));
        menu->addChild(createSubmenuItem("YM chip clock", "", [m](Menu *sub) {
            for (float clock : {3579545.f, 4000000.f, 8000000.f})
                sub->addChild(createMenuItem(string::f("%.6g MHz", clock / 1e6), "", [m, clock] {
                    m->params[DacStageModule::YM_CLOCK].setValue(clock);
                }));
            auto *slider = new ui::Slider;
            slider->box.size.x = 240;
            slider->quantity = m->getParamQuantity(DacStageModule::YM_CLOCK);
            sub->addChild(slider);
        }));
    }
};
Model *modelDacStage = createModel<DacStageModule, DacStageWidget>("DACStage");
