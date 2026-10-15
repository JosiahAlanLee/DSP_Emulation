// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "Layout.hpp"
#include "engine/Fx.hpp"
#include "../../shared/SampleShare.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
using namespace espish_layout;

// Clock divisions for TIME when CLOCK is patched, as multiples of the clock period.
static const float clockRatios[] = {1.f / 4, 1.f / 3, 1.f / 2, 2.f / 3, 3.f / 4, 1.f, 3.f / 2, 2.f, 3.f, 4.f};
static const char* clockNames[] = {"1/4", "1/3", "1/2", "2/3", "3/4", "1", "3/2", "2", "3", "4"};
constexpr int CLOCK_RATIOS = 10;

static float shiftHz(float knob, bool wide) {
    float a = std::abs(knob);
    if (a < .02f) return 0;  // precise centre
    float lo = .1f, hi = wide ? 2000.f : 20.f;
    float hz = lo * std::pow(hi / lo, (a - .02f) / .98f);
    return knob < 0 ? -hz : hz;
}

struct Espish : Module {
    enum Params { TIME, FEEDBACK, TONE, SHIFT, SPREAD, ECHO_MIX, SHIFT_RANGE, SHIFT_POS,
                  PREDELAY, SIZE, DECAY, DIFFUSION, DAMPING, MODULATION, SPACE_MIX, FREEZE,
                  MIX, ENGINE, CAPTURE, CAPTURE_LEN, NUM_PARAMS };
    enum Inputs { IN_L, IN_R, CLOCK_IN, CAPTURE_IN, FREEZE_IN, TIME_CV, FEEDBACK_CV, SHIFT_CV, DECAY_CV, MIX_CV, NUM_INPUTS };
    enum Outputs { OUT_L, OUT_R, NUM_OUTPUTS };
    enum Lights { LIMIT_LIGHT, FREEZE_LIGHT, CAPTURE_LIGHT, NUM_LIGHTS };

    espish::Engine engine;
    std::mutex engineMutex;  // held by the UI for reconfiguration; process() only try_locks
    dsp::SchmittTrigger clockTrigger, captureTrigger, captureButton;
    double clockPeriod = 0; long samplesSinceClock = 0; bool clockSeen = false;
    std::atomic<int> espRateIndex{0}; std::atomic<bool> espBudget{true};
    float sampleRate = 48000;

    // Capture share (UI thread writes, the reader's UI thread reads; mutex for safety).
    std::mutex shareMutex;
    std::vector<int16_t> sharePcm; double shareRate = 0; std::string shareName = "NO CAPTURE";
    std::atomic<uint32_t> shareRevision{0}; int captureCount = 0;
    dspemu::SampleShareV1 share{};

    struct ShiftQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            auto* m = dynamic_cast<Espish*>(module);
            bool wide = !m || m->params[SHIFT_RANGE].getValue() > .5f;
            return string::f("%+.2f", shiftHz(getValue(), wide));
        }
    };
    struct TimeQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            auto* m = dynamic_cast<Espish*>(module);
            if (m && m->inputs[CLOCK_IN].isConnected()) return std::string(clockNames[std::min(CLOCK_RATIOS - 1, int(getValue() * CLOCK_RATIOS))]) + " x clock";
            return ParamQuantity::getDisplayValueString();
        }
    };

    Espish() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam<TimeQuantity>(TIME, 0, 1, .6f, "Echo time", " s", 2000, .001f);
        configParam(FEEDBACK, 0, 1.1f, .4f, "Echo feedback", "%", 0, 100);
        configParam(TONE, -1, 1, 0, "Echo tone (dark / bright)");
        configParam<ShiftQuantity>(SHIFT, -1, 1, 0, "Frequency shift", " Hz");
        configParam(SPREAD, 0, 1, 0, "Spread (stereo / ping-pong)", "%", 0, 100);
        configParam(ECHO_MIX, 0, 1, .5f, "Echo mix", "%", 0, 100);
        configSwitch(SHIFT_RANGE, 0, 1, 1, "Shift range", {"Fine (±20 Hz)", "Wide (±2 kHz)"});
        configSwitch(SHIFT_POS, 0, 1, 0, "Shift position", {"In feedback loop", "Post (once)"});
        configParam(PREDELAY, 0, .25f, .02f, "Predelay", " ms", 0, 1000);
        configParam(SIZE, 0, 1, .5f, "Size", "%", 0, 100);
        configParam(DECAY, 0, 1, .55f, "Decay (RT60)", " s", 150, .2f);
        configParam(DIFFUSION, 0, 1, .6f, "Diffusion", "%", 0, 100);
        configParam(DAMPING, 0, 1, .4f, "Damping", "%", 0, 100);
        configParam(MODULATION, 0, 1, .3f, "Modulation", "%", 0, 100);
        configParam(SPACE_MIX, 0, 1, .35f, "Space mix", "%", 0, 100);
        configSwitch(FREEZE, 0, 1, 0, "Freeze", {"Off", "On"});
        configParam(MIX, 0, 1, .5f, "Dry / wet", "%", 0, 100);
        configSwitch(ENGINE, 0, 1, 0, "Engine", {"CLEAN (float, host rate)", "ESP (ES5510-style 24/16-bit math, reduced rate)"});
        configButton(CAPTURE, "Capture the recent past for OTIS");
        configParam(CAPTURE_LEN, 1, 20, 8, "Capture length", " s");
        configInput(IN_L, "Left / mono"); configInput(IN_R, "Right (normalled to left)");
        configInput(CLOCK_IN, "Clock (TIME becomes a clock ratio)");
        configInput(CAPTURE_IN, "Capture trigger"); configInput(FREEZE_IN, "Freeze gate");
        configInput(TIME_CV, "Time CV"); configInput(FEEDBACK_CV, "Feedback CV"); configInput(SHIFT_CV, "Shift CV");
        configInput(DECAY_CV, "Decay CV"); configInput(MIX_CV, "Dry / wet CV");
        configOutput(OUT_L, "Left"); configOutput(OUT_R, "Right");
        configBypass(IN_L, OUT_L); configBypass(IN_R, OUT_R);
        engine.prepare(sampleRate, 0);
        exposeShare();
    }

    void exposeShare() {
        share.magic = dspemu::SAMPLE_SHARE_MAGIC; share.version = dspemu::SAMPLE_SHARE_VERSION; share.context = this;
        // revision() is called on the reader's UI thread; it collects a pending capture first,
        // so a reader sees it even before this module's own widget has stepped.
        share.revision = [](void* c) -> uint32_t { auto* m = static_cast<Espish*>(c); m->collectCapture(); return m->shareRevision.load(std::memory_order_acquire); };
        share.copy = [](void* c, int16_t* dst, size_t capacity, double* rate, uint32_t* revision) -> size_t {
            auto* m = static_cast<Espish*>(c);
            std::lock_guard<std::mutex> guard(m->shareMutex);
            if (dst) std::memcpy(dst, m->sharePcm.data(), std::min(capacity, m->sharePcm.size()) * sizeof(int16_t));
            if (rate) *rate = m->shareRate;
            if (revision) *revision = m->shareRevision.load(std::memory_order_relaxed);
            return m->sharePcm.size();
        };
        share.name = [](void* c, char* buffer, size_t size) {
            if (!buffer || !size) return;
            auto* m = static_cast<Espish*>(c);
            std::lock_guard<std::mutex> guard(m->shareMutex);
            std::snprintf(buffer, size, "%s", m->shareName.c_str());
        };
        rightExpander.producerMessage = rightExpander.consumerMessage = &share;  // fixed; no flip is requested
    }

    // UI thread: move a finished capture into the share.
    void collectCapture() {
        std::vector<int16_t> pcm;
        if (!engine.capture.collect(pcm)) return;
        std::lock_guard<std::mutex> guard(shareMutex);
        shareRevision.fetch_add(1, std::memory_order_acq_rel);  // odd: changing
        sharePcm.swap(pcm); shareRate = engine.hostRate;
        shareName = string::f("ESPish capture %d", ++captureCount);
        shareRevision.fetch_add(1, std::memory_order_release);  // even: ready
    }

    void reconfigure(int rateIndex, bool budget) {
        std::lock_guard<std::mutex> guard(engineMutex);
        espRateIndex = rateIndex; espBudget = budget;
        if (engine.preparedRate != rateIndex) engine.prepareEsp(rateIndex);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        std::lock_guard<std::mutex> guard(engineMutex);
        sampleRate = e.sampleRate;
        engine.prepare(e.sampleRate, espRateIndex.load());
        clockSeen = false; samplesSinceClock = 0;
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        std::lock_guard<std::mutex> guard(engineMutex);
        engine.clear();
    }

    float cv(int input, float scale) { return inputs[input].isConnected() ? inputs[input].getVoltage() * scale : 0.f; }

    void process(const ProcessArgs& args) override {
        std::unique_lock<std::mutex> guard(engineMutex, std::try_to_lock);
        if (!guard.owns_lock()) { outputs[OUT_L].setVoltage(0); outputs[OUT_R].setVoltage(0); return; }

        // Clock period for synced echo time.
        samplesSinceClock++;
        if (clockTrigger.process(inputs[CLOCK_IN].getVoltage(), .1f, 1.f)) {
            if (clockSeen) clockPeriod = double(samplesSinceClock) / args.sampleRate;
            clockSeen = true; samplesSinceClock = 0;
        }

        espish::Params p;
        float timeKnob = clamp(params[TIME].getValue() + cv(TIME_CV, .1f), 0.f, 1.f);
        if (inputs[CLOCK_IN].isConnected() && clockPeriod > 0)
            p.echo.timeSec = float(clockPeriod) * clockRatios[std::min(CLOCK_RATIOS - 1, int(timeKnob * CLOCK_RATIOS))];
        else
            p.echo.timeSec = .001f * std::pow(2000.f, timeKnob);
        p.echo.feedback = clamp(params[FEEDBACK].getValue() + cv(FEEDBACK_CV, .11f), 0.f, 1.1f);
        p.echo.tone = params[TONE].getValue();
        p.echo.shiftHz = shiftHz(clamp(params[SHIFT].getValue() + cv(SHIFT_CV, .2f), -1.f, 1.f), params[SHIFT_RANGE].getValue() > .5f);
        p.echo.shiftPost = params[SHIFT_POS].getValue() > .5f;
        p.echo.spread = params[SPREAD].getValue();
        p.space.predelaySec = params[PREDELAY].getValue();
        p.space.size = params[SIZE].getValue();
        p.space.decaySec = .2f * std::pow(150.f, clamp(params[DECAY].getValue() + cv(DECAY_CV, .1f), 0.f, 1.f));
        p.space.diffusion = params[DIFFUSION].getValue();
        p.space.damping = params[DAMPING].getValue();
        p.space.modulation = params[MODULATION].getValue();
        p.space.freeze = params[FREEZE].getValue() > .5f || inputs[FREEZE_IN].getVoltage() >= 1.f;
        p.echoMix = params[ECHO_MIX].getValue();
        p.spaceMix = params[SPACE_MIX].getValue();
        p.mix = clamp(params[MIX].getValue() + cv(MIX_CV, .1f), 0.f, 1.f);
        p.esp = params[ENGINE].getValue() > .5f;
        p.espRate = espRateIndex.load(std::memory_order_relaxed);
        p.espBudget = espBudget.load(std::memory_order_relaxed);

        // Rack audio is ±5 V nominal; ±10 V maps to ±1 internally (6 dB headroom).
        float l = inputs[IN_L].getVoltage() * .1f;
        float r = inputs[IN_R].isConnected() ? inputs[IN_R].getVoltage() * .1f : l;
        auto y = engine.tick(l, r, p);
        outputs[OUT_L].setVoltage(y[0] * 10.f);
        outputs[OUT_R].setVoltage(y[1] * 10.f);

        bool capture = captureButton.process(params[CAPTURE].getValue()) | captureTrigger.process(inputs[CAPTURE_IN].getVoltage(), .1f, 1.f);
        if (capture) engine.capture.trigger(size_t(params[CAPTURE_LEN].getValue() * args.sampleRate));

        lights[LIMIT_LIGHT].setBrightnessSmooth(engine.limiter, args.sampleTime);
        lights[FREEZE_LIGHT].setBrightness(p.space.freeze ? 1.f : 0.f);
        lights[CAPTURE_LIGHT].setBrightness(engine.capture.state.load(std::memory_order_relaxed) ? 1.f : 0.f);
    }

    json_t* dataToJson() override {
        json_t* j = json_object();
        json_object_set_new(j, "espRate", json_integer(espRateIndex.load()));
        json_object_set_new(j, "espBudget", json_boolean(espBudget.load()));
        return j;
    }
    void dataFromJson(json_t* j) override {
        json_t* rate = json_object_get(j, "espRate");
        json_t* budget = json_object_get(j, "espBudget");
        reconfigure(rate ? int(std::max<json_int_t>(0, std::min<json_int_t>(2, json_integer_value(rate)))) : 0, budget ? json_is_true(budget) : true);
    }
};

struct EspishWidget : ModuleWidget {
    EspishWidget(Espish* m) {
        setModule(m);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Espish.svg")));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
#define KNOB(N) addParam(createParamCentered<RoundBlackKnob>(Vec(KNOB_##N##_X, KNOB_##N##_Y), m, Espish::N))
        KNOB(TIME); KNOB(FEEDBACK); KNOB(TONE); KNOB(SHIFT); KNOB(SPREAD); KNOB(ECHO_MIX);
        KNOB(PREDELAY); KNOB(SIZE); KNOB(DECAY); KNOB(DIFFUSION); KNOB(DAMPING); KNOB(MODULATION); KNOB(SPACE_MIX);
#undef KNOB
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(SMALL_MIX_X, SMALL_MIX_Y), m, Espish::MIX));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(SMALL_CAPTURE_LEN_X, SMALL_CAPTURE_LEN_Y), m, Espish::CAPTURE_LEN));
        addParam(createParamCentered<CKSS>(Vec(SWITCH_SHIFT_RANGE_X, SWITCH_SHIFT_RANGE_Y), m, Espish::SHIFT_RANGE));
        addParam(createParamCentered<CKSS>(Vec(SWITCH_SHIFT_POS_X, SWITCH_SHIFT_POS_Y), m, Espish::SHIFT_POS));
        addParam(createParamCentered<CKSS>(Vec(SWITCH_ENGINE_X, SWITCH_ENGINE_Y), m, Espish::ENGINE));
        addParam(createParamCentered<VCVButton>(Vec(BUTTON_CAPTURE_X, BUTTON_CAPTURE_Y), m, Espish::CAPTURE));
        addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(Vec(LATCH_FREEZE_X, LATCH_FREEZE_Y), m, Espish::FREEZE, Espish::FREEZE_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(LIGHT_LIMIT_LIGHT_X, LIGHT_LIMIT_LIGHT_Y), m, Espish::LIMIT_LIGHT));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(LIGHT_CAPTURE_LIGHT_X, LIGHT_CAPTURE_LIGHT_Y), m, Espish::CAPTURE_LIGHT));
#define IN(N) addInput(createInputCentered<PJ301MPort>(Vec(JACK_##N##_X, JACK_##N##_Y), m, Espish::N))
        IN(TIME_CV); IN(FEEDBACK_CV); IN(SHIFT_CV); IN(DECAY_CV); IN(MIX_CV); IN(CLOCK_IN); IN(CAPTURE_IN); IN(FREEZE_IN); IN(IN_L); IN(IN_R);
#undef IN
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(JACK_OUT_L_X, JACK_OUT_L_Y), m, Espish::OUT_L));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(JACK_OUT_R_X, JACK_OUT_R_Y), m, Espish::OUT_R));
    }
    void step() override {
        if (auto* m = dynamic_cast<Espish*>(module)) m->collectCapture();
        ModuleWidget::step();
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<Espish*>(module);
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("ESP engine (ES5510-style math, not chip emulation)"));
        static const char* rates[] = {"29.76 kHz (EPS-16 Plus, 20 voices)", "44.64 kHz (\"44 kHz\" effects, 13 voices)", "78.125 kHz (not a period rate)"};
        menu->addChild(createSubmenuItem("ESP rate", rates[m->espRateIndex.load()], [m](Menu* sub) {
            for (int i = 0; i < 3; i++)
                sub->addChild(createCheckMenuItem(rates[i], "", [m, i] { return m->espRateIndex.load() == i; },
                                                  [m, i] { m->reconfigure(i, m->espBudget.load()); }));
        }));
        menu->addChild(createBoolMenuItem("ESP echo memory budget (64K words)", "", [m] { return m->espBudget.load(); },
                                          [m](bool on) { m->reconfigure(m->espRateIndex.load(), on); }));
        menu->addChild(new MenuSeparator);
        std::string status;
        { std::lock_guard<std::mutex> guard(m->shareMutex); status = m->sharePcm.empty() ? "No capture yet" : string::f("%s, %.1f s", m->shareName.c_str(), m->sharePcm.size() / m->shareRate); }
        menu->addChild(createMenuLabel(status));
        menu->addChild(createMenuLabel("Place OTIS directly to the right to import captures"));
    }
};

Model* modelEspish = createModel<Espish, EspishWidget>("ESPish");
