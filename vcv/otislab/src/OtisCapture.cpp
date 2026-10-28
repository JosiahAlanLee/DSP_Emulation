// SPDX-License-Identifier: BSD-3-Clause
// Otis Capture (docs/CAPTURE.md): records takes of a set number of beats into a slot of the Otis Kit
// directly to its left. Capture decides when a take runs, from its clock or its TEMPO; the kit writes
// the take into free sample memory and makes it the slot's new take only once it is complete.
#include "plugin.hpp"
#include "CaptureLink.hpp"
#include "KitLayout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <algorithm>
#include <atomic>

static const float CAPTURE_BEATS[] = {.25f, .5f, 1, 2, 3, 4, 6, 8, 12, 16, 32};
static const int CAPTURE_LENGTHS = int(sizeof(CAPTURE_BEATS) / sizeof(CAPTURE_BEATS[0]));
static const int CAPTURE_PPQN[] = {1, 2, 4, 8, 12, 24};

struct OtisCapture : Module {
    enum Params { SLOT, LENGTH, SOURCE, GAIN, TEMPO, ARM_BUTTON, NUM_PARAMS };
    enum Inputs { AUDIO_IN, CLOCK_IN, ARM_IN, NUM_INPUTS };
    enum Outputs { REC_OUT, DONE_OUT, NUM_OUTPUTS };
    enum Lights { ARM_LIGHT, REC_LIGHT, NUM_LIGHTS };

    otis::kit::CaptureStatus statusBuffers[2];   // owned here; the kit on the left writes them
    dsp::SchmittTrigger clock, armTrigger;
    dsp::PulseGenerator done;
    bool buttonHigh = false, armed = false, recording = false, takeClocked = false;
    int64_t pulsesLeft = 0, samplesLeft = 0;
    // A clocked take ends if no pulse arrives for two beats at TEMPO: the samples allowed between pulses,
    // and the samples left before the current wait runs out.
    int64_t pulseTimeout = 0, pulseWaitLeft = 0;
    std::atomic<int> ppqn{4};                    // clock pulses per beat
    // For the display (UI thread).
    std::atomic<bool> displayLinked{false}, displayArmed{false}, displayRecording{false};
    std::atomic<int> displayState{0};            // 0 idle, 1 no room, 2 truncated
    std::atomic<int> displaySlot{-1};
    std::atomic<float> displaySeconds{0}, displayRoom{0};
    std::atomic<uint32_t> displayTakes{0};

    OtisCapture() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(SLOT, 0, 7, 0, "Slot", {"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8"});
        configSwitch(LENGTH, 0, CAPTURE_LENGTHS - 1, 5, "Length",
            {"1/4 beat", "1/2 beat", "1 beat", "2 beats", "3 beats", "4 beats", "6 beats", "8 beats", "12 beats", "16 beats", "32 beats"});
        configSwitch(SOURCE, 0, 2, 0, "Source", {"IN", "The kit's own output", "IN and the kit's output"});
        configParam(GAIN, 0, 2, 1, "Gain", "%", 0, 100);
        configParam(TEMPO, 40, 240, 120, "Tempo: sets the take length without CLOCK, and ends a clocked take after two beats without a pulse", " BPM");
        getParamQuantity(TEMPO)->snapEnabled = true;
        configButton(ARM_BUTTON, "Arm: the next take starts on the next CLOCK pulse (at once without CLOCK)");
        configInput(AUDIO_IN, "Audio to record (±5 V full scale)");
        configInput(CLOCK_IN, "Clock: takes start on a pulse and last LENGTH beats of pulses");
        configInput(ARM_IN, "Arm trigger");
        configOutput(REC_OUT, "High while a take is recording");
        configOutput(DONE_OUT, "Trigger when a take ends");
        leftExpander.producerMessage = &statusBuffers[0]; leftExpander.consumerMessage = &statusBuffers[1];
    }
    void stop() { recording = false; done.trigger(1e-3f); }
    void process(const ProcessArgs& args) override {
        Module* left = leftExpander.module;
        bool linked = left && left->model == modelOtisKit;
        auto* status = linked ? static_cast<const otis::kit::CaptureStatus*>(leftExpander.consumerMessage) : nullptr;
        if (status && status->magic != otis::kit::CaptureStatus::MAGIC) status = nullptr;
        bool button = params[ARM_BUTTON].getValue() >= .5f;
        bool armEdge = armTrigger.process(inputs[ARM_IN].getVoltage(), .1f, 2.f) | (button && !buttonHigh);
        buttonHigh = button;
        if (armEdge && linked && !recording) armed = true;
        bool clocked = inputs[CLOCK_IN].isConnected();
        bool pulse = clock.process(clocked ? inputs[CLOCK_IN].getVoltage() : 0.f, .1f, 2.f);
        float beats = CAPTURE_BEATS[std::max(0, std::min(CAPTURE_LENGTHS - 1, int(std::round(params[LENGTH].getValue()))))];
        if (!linked) { armed = false; if (recording) stop(); }
        else if (recording) {
            // A take covers the samples from its start pulse up to, not including, the pulse that ends it.
            // Keep the tempo fallback measured from the start, even while counting pulses.
            --samplesLeft;
            if (takeClocked && clocked) {
                // Ends on its last pulse, or where the clock has stopped for two beats at TEMPO.
                --pulseWaitLeft;
                if (pulse ? --pulsesLeft <= 0 : pulseWaitLeft <= 0) stop();
                else if (pulse) pulseWaitLeft = pulseTimeout;
            } else if (samplesLeft <= 0) stop();
        } else if (armed && (!clocked || pulse)) {
            armed = false; recording = true; takeClocked = clocked;
            pulsesLeft = std::max<int64_t>(1, std::llround(beats * ppqn.load(std::memory_order_relaxed)));
            float tempo = std::isfinite(params[TEMPO].getValue()) ? clamp(params[TEMPO].getValue(), 40.f, 240.f) : 120.f;
            samplesLeft = std::max<int64_t>(1, std::llround(double(beats) * 60 / tempo * args.sampleRate));
            pulseWaitLeft = pulseTimeout = std::max<int64_t>(1, std::llround(2.0 * 60 / tempo * args.sampleRate));
        }
        if (linked) if (auto* request = static_cast<otis::kit::CaptureRequest*>(left->rightExpander.producerMessage)) {
            float in = inputs[AUDIO_IN].getVoltage() / 5.f;
            request->magic = otis::kit::CaptureRequest::MAGIC; request->recording = recording;
            request->slot = int(std::round(params[SLOT].getValue())); request->source = int(std::round(params[SOURCE].getValue()));
            request->gain = params[GAIN].getValue(); request->input = std::isfinite(in) ? in : 0.f;
            left->rightExpander.messageFlipRequested = true;
        }
        outputs[REC_OUT].setVoltage(recording ? 10.f : 0.f);
        outputs[DONE_OUT].setVoltage(done.process(args.sampleTime) ? 10.f : 0.f);
        lights[ARM_LIGHT].setBrightness(armed ? 1.f : 0.f);
        lights[REC_LIGHT].setBrightness(recording ? 1.f : 0.f);
        displayLinked.store(linked, std::memory_order_relaxed); displayArmed.store(armed, std::memory_order_relaxed);
        displayRecording.store(recording, std::memory_order_relaxed);
        if (status) {
            displayState.store(status->noRoom ? 1 : status->truncated ? 2 : 0, std::memory_order_relaxed);
            displaySlot.store(status->slot, std::memory_order_relaxed);
            displaySeconds.store(float(status->frames / status->rate), std::memory_order_relaxed);
            displayRoom.store(float(status->room / status->rate), std::memory_order_relaxed);
            displayTakes.store(status->takes, std::memory_order_relaxed);
        }
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); armed = recording = takeClocked = false; pulsesLeft = samplesLeft = pulseTimeout = pulseWaitLeft = 0; ppqn = 4;
    }
    json_t* dataToJson() override {
        json_t* root = json_object(); json_object_set_new(root, "ppqn", json_integer(ppqn.load())); return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* v = json_object_get(root, "ppqn"); int n = json_is_integer(v) ? int(json_integer_value(v)) : 4;
        ppqn = std::find(std::begin(CAPTURE_PPQN), std::end(CAPTURE_PPQN), n) != std::end(CAPTURE_PPQN) ? n : 4;
    }
};

struct CaptureDisplay : widget::Widget {
    OtisCapture* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(args.vg, nvgRGB(14, 20, 23)); nvgFill(args.vg);
        auto font = APP->window->uiFont; if (!font) return;
        nvgFontFaceId(args.vg, font->handle); nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        std::string top = "NO KIT", bottom = "PLACE RIGHT OF KIT";
        if (module && module->displayLinked.load()) {
            int slot = module->displaySlot.load(), state = module->displayState.load();
            if (module->displayRecording.load()) top = string::f("REC %.2f s", module->displaySeconds.load());
            else if (module->displayArmed.load()) top = "ARMED";
            else if (state == 1) top = "NO ROOM";
            else if (module->displayTakes.load() && slot >= 0)
                top = string::f("%sSLOT %d %.2f s", state == 2 ? "CUT " : "", slot + 1, module->displaySeconds.load());
            else top = "READY";
            bottom = string::f("ROOM %.1f s", module->displayRoom.load());
        } else if (!module) { top = "CAPTURE"; bottom = "OTIS KIT"; }
        nvgFillColor(args.vg, nvgRGB(143, 220, 196)); nvgFontSize(args.vg, 12);
        nvgText(args.vg, box.size.x / 2, box.size.y * .36f, top.c_str(), nullptr);
        nvgFillColor(args.vg, nvgRGB(127, 148, 156)); nvgFontSize(args.vg, 8);
        nvgText(args.vg, box.size.x / 2, box.size.y * .76f, bottom.c_str(), nullptr);
    }
};
struct OtisCaptureWidget : ModuleWidget {
    OtisCaptureWidget(OtisCapture* module) {
        using namespace otis_kit_layout::capture;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/OtisCapture.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(WIDTH - 15, 365)));
        auto* display = createWidget<CaptureDisplay>(POS(STATUS));
        display->box.size = Vec(STATUS_W, STATUS_H); display->module = module; addChild(display);
        auto choice = [&](int id, Vec pos, Vec size, std::vector<std::string> labels) {
            auto* c = createParam<dspemu::panel::Choice>(pos, module, id); c->box.size = size; c->labels = labels; addParam(c);
        };
        choice(OtisCapture::SLOT, POS(SLOT), Vec(SLOT_W, SLOT_H), {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4", "SLOT 5", "SLOT 6", "SLOT 7", "SLOT 8"});
        choice(OtisCapture::LENGTH, POS(LENGTH), Vec(LENGTH_W, LENGTH_H),
            {"1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "3 BEATS", "4 BEATS", "6 BEATS", "8 BEATS", "12 BEATS", "16 BEATS", "32 BEATS"});
        choice(OtisCapture::SOURCE, POS(SOURCE), Vec(SOURCE_W, SOURCE_H), {"IN", "KIT", "IN + KIT"});
        addParam(createParamCentered<RoundBlackKnob>(POS(GAIN), module, OtisCapture::GAIN));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(TEMPO), module, OtisCapture::TEMPO));
        addParam(createParamCentered<VCVButton>(POS(ARM), module, OtisCapture::ARM_BUTTON));
        addChild(createLightCentered<SmallLight<YellowLight>>(POS(ARM_LIGHT), module, OtisCapture::ARM_LIGHT));
        addInput(createInputCentered<PJ301MPort>(POS(AUDIO_IN), module, OtisCapture::AUDIO_IN));
        addInput(createInputCentered<PJ301MPort>(POS(CLOCK_IN), module, OtisCapture::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(POS(ARM_IN), module, OtisCapture::ARM_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(REC_OUT), module, OtisCapture::REC_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(DONE_OUT), module, OtisCapture::DONE_OUT));
        addChild(createLightCentered<SmallLight<RedLight>>(POS(REC_LIGHT), module, OtisCapture::REC_LIGHT));
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<OtisCapture*>(module); if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("CLOCK pulses per beat", "", [m](Menu* child) {
            for (int n : CAPTURE_PPQN) child->addChild(createCheckMenuItem(string::f("%d", n) + (n == 4 ? " (Gorgon CLOCK: one per row)" : ""), "",
                [m, n] { return m->ppqn.load() == n; }, [m, n] { m->ppqn = n; }));
        }));
        menu->addChild(createMenuLabel("Takes record at Rack's sample rate into free kit memory"));
        menu->addChild(createMenuLabel("A slot's new take replaces it when complete; a hit already sounding keeps the old one"));
    }
};
Model* modelOtisCapture = createModel<OtisCapture, OtisCaptureWidget>("OtisCapture");
