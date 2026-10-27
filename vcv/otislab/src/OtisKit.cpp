// SPDX-License-Identifier: BSD-3-Clause
// Otis Kit (docs/KIT.md): a bank of eight sample slots on OTIS's ES5505 playback arithmetic. Slot s
// plays on chip voice s over its own region of one sample memory; a hit chooses the slot from SLOT's
// bits 0-2, or from its GATE channel when SLOT is unpatched. Playback only; capture comes later.
#include "plugin.hpp"
#include "engine/Kit.hpp"
#include "KitLayout.hpp"
#include "../../shared/SampleFile.hpp"
#include "../../shared/ByteBus.hpp"
#include <osdialog.h>
#include <algorithm>
#include <atomic>
#include <mutex>

using otis::kit::SLOTS;

struct OtisKit : Module {
    enum Params { ENUMS(TUNE, SLOTS), ENUMS(DECAY, SLOTS), ENUMS(LEVEL, SLOTS), ENUMS(FILTER, SLOTS),
        ENUMS(START, SLOTS), ENUMS(REV, SLOTS), NUM_PARAMS };
    enum Inputs { GATE_IN, SLOT_IN, PITCH_IN, LEVEL_IN, NUM_INPUTS };
    enum Outputs { OUT, NUM_OUTPUTS };
    enum Lights { ENUMS(HIT_LIGHT, SLOTS), NUM_LIGHTS };

    otis::Engine engine;
    std::mutex engineMutex;                      // UI-thread installs; the audio thread only try-locks
    otis::kit::Layout layout;                    // guarded by engineMutex
    std::array<otis::kit::Slot, SLOTS> slots;    // UI thread: the bank's source audio
    dspemu::bus::Reader slotReader;
    std::array<dsp::SchmittTrigger, 16> gates;
    std::array<float, SLOTS> hitPitch{}, hitLevel{}; // latched from the hit's channel
    std::array<float, SLOTS> flash{};
    std::atomic<int> engineMode{0};              // chip clock: 0 = 16 voices, 1 = 14, 2 = 8
    std::atomic<bool> dropSample{false};         // interpolation off (degradation)
    std::string lastError;                       // UI thread

    OtisKit() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (int s = 0; s < SLOTS; ++s) {
            configParam(TUNE + s, -24, 24, 0, string::f("Slot %d tune", s + 1), " semitones");
            configParam(DECAY + s, 0, 1, 1, string::f("Slot %d decay (fully clockwise holds)", s + 1));
            configParam(LEVEL + s, 0, 1, 1, string::f("Slot %d level", s + 1), "%", 0, 100);
            configParam(FILTER + s, 0, 1, 1, string::f("Slot %d filter (K1)", s + 1), "%", 0, 100);
            configParam(START + s, 0, 1, 0, string::f("Slot %d start", s + 1), "%", 0, 100);
            configSwitch(REV + s, 0, 1, 0, string::f("Slot %d direction", s + 1), {"Forward", "Reverse"});
            hitLevel[size_t(s)] = 1;
        }
        configInput(GATE_IN, "Hits, polyphonic: channel n plays slot n unless SLOT is patched");
        configInput(SLOT_IN, "Slot select: bits 0-2 choose the slot on each hit (optional CLK on channel 9)");
        configInput(PITCH_IN, "Pitch, 1 V/oct, polyphonic, latched per hit from the hit's channel");
        configInput(LEVEL_IN, "Level, 0-10 V, polyphonic, latched per hit (unpatched = full)");
        configOutput(OUT, "Audio (±5 V full scale)");
        install();
    }
    // UI thread: pack the slots into one memory image and hand it to the engine. Sounding hits stop.
    void install() {
        std::vector<int16_t> image;
        auto packed = otis::kit::pack(slots, image);
        std::lock_guard<std::mutex> guard(engineMutex);
        engine.install(image, otis::kit::BANK_RATE, std::max<size_t>(packed.frames, 0), false);
        layout = packed;
    }
    void loadSlot(int s, const std::string& path) {
        try {
            auto wav = dspemu::loadWav(path, otis::memoryWords, false);   // mono: two channels are averaged
            slots[size_t(s)].pcm = std::move(wav.pcm); slots[size_t(s)].rate = wav.rate;
            slots[size_t(s)].name = dspemu::baseName(path); lastError.clear();
            install();
        } catch (const std::exception& e) { lastError = e.what(); WARN("Otis Kit: %s", e.what()); }
    }
    // A folder's WAVs, in name order, fill slots 1-8.
    void loadFolder(const std::string& dir) {
        std::vector<std::string> wavs;
        for (auto& entry : system::getEntries(dir)) {
            std::string ext = string::lowercase(system::getExtension(entry));
            if (ext == ".wav" && system::isFile(entry)) wavs.push_back(entry);
        }
        std::sort(wavs.begin(), wavs.end());
        lastError.clear();
        for (size_t k = 0; k < wavs.size() && k < size_t(SLOTS); ++k) {
            try {
                auto wav = dspemu::loadWav(wavs[k], otis::memoryWords, false);
                slots[k].pcm = std::move(wav.pcm); slots[k].rate = wav.rate; slots[k].name = dspemu::baseName(wavs[k]);
            } catch (const std::exception& e) { lastError = e.what(); }
        }
        install();
    }
    void clearSlot(int s) { slots[size_t(s)] = {}; install(); }
    std::string slotLabel(int s) const {
        const auto& slot = slots[size_t(s)];
        if (slot.pcm.size() < 2) return string::f("Slot %d: empty", s + 1);
        return string::f("Slot %d: %s (%.2f s)%s", s + 1, slot.name.c_str(), slot.pcm.size() / slot.rate,
            layout.truncated[size_t(s)] ? " [TRUNCATED: bank full]" : "");
    }
    void process(const ProcessArgs& args) override {
        std::unique_lock<std::mutex> guard(engineMutex, std::try_to_lock);
        if (!guard.owns_lock()) { outputs[OUT].setVoltage(0); return; }
        auto& c = engine.controls;
        c.engineMode = engineMode.load(std::memory_order_relaxed); c.filterMode = 2; c.gated = false; c.declick = true;
        c.loop = 0; c.interpBits = dropSample.load(std::memory_order_relaxed) ? 0 : 9; c.windowMode = 0;
        for (int s = 0; s < SLOTS; ++s) {
            size_t i = size_t(s);
            c.regionBegin[i] = layout.begin[i]; c.regionEnd[i] = layout.end[i];
            c.startv[i] = params[START + s].getValue(); c.endv[i] = 1;
            c.pitch[i] = float(params[TUNE + s].getValue() / 12 + layout.octaves[i] + hitPitch[i]);
            c.level[i] = params[LEVEL + s].getValue() * hitLevel[i];
            c.k1v[i] = params[FILTER + s].getValue(); c.k2v[i] = 1;
            c.loopv[i] = 0; c.reversev[i] = params[REV + s].getValue() > .5f ? 1 : 0;
            c.decaySeconds[i] = float(otis::kit::decaySeconds(params[DECAY + s].getValue()));
        }
        // A hit on channel ch plays slot slotFor(...), with that channel's PITCH and LEVEL.
        auto slotBus = slotReader.process(inputs[SLOT_IN]);
        int channels = std::min(16, std::max(1, inputs[GATE_IN].getChannels()));
        for (int ch = 0; ch < 16; ++ch) {
            if (!gates[size_t(ch)].process(ch < channels ? inputs[GATE_IN].getVoltage(ch) : 0.f, .1f, 2.f)) continue;
            int s = otis::kit::slotFor(slotBus.connected, slotBus.word, ch);
            if (!layout.playable(s)) continue;
            auto finite = [](float v) { return std::isfinite(v) ? v : 0.f; };
            hitPitch[size_t(s)] = inputs[PITCH_IN].isConnected() ? otis::clamp(finite(inputs[PITCH_IN].getPolyVoltage(ch)), -10.f, 10.f) : 0.f;
            hitLevel[size_t(s)] = inputs[LEVEL_IN].isConnected() ? otis::clamp(finite(inputs[LEVEL_IN].getPolyVoltage(ch)) / 10, 0.f, 1.f) : 1.f;
            c.pitch[size_t(s)] = float(params[TUNE + s].getValue() / 12 + layout.octaves[size_t(s)] + hitPitch[size_t(s)]);
            c.level[size_t(s)] = params[LEVEL + s].getValue() * hitLevel[size_t(s)];
            engine.trigger(s);
            flash[size_t(s)] = 1;
        }
        float out = engine.process(0.f) * 5.f;
        outputs[OUT].setVoltage(std::isfinite(out) ? out : 0.f);
        for (int s = 0; s < SLOTS; ++s) {
            lights[HIT_LIGHT + s].setBrightnessSmooth(flash[size_t(s)], args.sampleTime * 4);
            flash[size_t(s)] = 0;
        }
    }
    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        std::lock_guard<std::mutex> guard(engineMutex); engine.setHostRate(e.sampleRate);
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        engineMode = 0; dropSample = false; slotReader.reset();
        for (auto& g : gates) g.reset();
        hitPitch.fill(0); hitLevel.fill(1);
        for (auto& s : slots) s = {};
        install();
    }
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "engineMode", json_integer(engineMode.load()));
        json_object_set_new(root, "dropSample", json_boolean(dropSample.load()));
        json_t* list = json_array();
        for (const auto& slot : slots) {
            if (slot.pcm.size() < 2) { json_array_append_new(list, json_null()); continue; }
            json_t* o = json_object();
            json_object_set_new(o, "name", json_string(slot.name.c_str()));
            json_object_set_new(o, "rate", json_real(slot.rate));
            json_object_set_new(o, "pcm16le", json_string(dspemu::encodePcm16(slot.pcm.data(), slot.pcm.size()).c_str()));
            json_array_append_new(list, o);
        }
        json_object_set_new(root, "slots", list);
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* mode = json_object_get(root, "engineMode");
        engineMode = json_is_integer(mode) ? int(otis::clamp<json_int_t>(json_integer_value(mode), 0, 2)) : 0;
        dropSample = json_is_true(json_object_get(root, "dropSample"));
        json_t* list = json_object_get(root, "slots");
        for (int s = 0; s < SLOTS; ++s) {
            slots[size_t(s)] = {};
            json_t* o = json_is_array(list) ? json_array_get(list, size_t(s)) : nullptr;
            const char* text = json_is_object(o) ? json_string_value(json_object_get(o, "pcm16le")) : nullptr;
            if (!text) continue;
            double rate = json_number_value(json_object_get(o, "rate"));
            if (!std::isfinite(rate) || rate < 1000 || rate > 384000) continue;
            // A damaged slot is left empty; the others still load.
            try { slots[size_t(s)].pcm = dspemu::decodePcm16(text, otis::memoryWords); }
            catch (const std::exception& e) { lastError = e.what(); continue; }
            slots[size_t(s)].rate = rate;
            const char* name = json_string_value(json_object_get(o, "name"));
            slots[size_t(s)].name = name ? name : "RESTORED";
        }
        install();
    }
};

struct OtisKitWidget : ModuleWidget {
    OtisKitWidget(OtisKit* module) {
        using namespace otis_kit_layout::kit;
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance, "res/OtisKit.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(WIDTH - 15, 365)));
        const Vec hit[] = {{HIT0_X, HIT0_Y}, {HIT1_X, HIT1_Y}, {HIT2_X, HIT2_Y}, {HIT3_X, HIT3_Y},
                           {HIT4_X, HIT4_Y}, {HIT5_X, HIT5_Y}, {HIT6_X, HIT6_Y}, {HIT7_X, HIT7_Y}};
        for (int s = 0; s < SLOTS; ++s) {
            float y = hit[s].y;
            addChild(createLightCentered<SmallLight<GreenLight>>(hit[s], module, OtisKit::HIT_LIGHT + s));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(TUNE0_X, y), module, OtisKit::TUNE + s));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(DECAY0_X, y), module, OtisKit::DECAY + s));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(LEVEL0_X, y), module, OtisKit::LEVEL + s));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(FILTER0_X, y), module, OtisKit::FILTER + s));
            addParam(createParamCentered<RoundSmallBlackKnob>(Vec(START0_X, y), module, OtisKit::START + s));
            addParam(createParamCentered<CKSS>(Vec(REV0_X, y), module, OtisKit::REV + s));
        }
        addInput(createInputCentered<PJ301MPort>(Vec(GATE_IN_X, GATE_IN_Y), module, OtisKit::GATE_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(SLOT_IN_X, SLOT_IN_Y), module, OtisKit::SLOT_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(PITCH_IN_X, PITCH_IN_Y), module, OtisKit::PITCH_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(LEVEL_IN_X, LEVEL_IN_Y), module, OtisKit::LEVEL_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(OUT_X, OUT_Y), module, OtisKit::OUT));
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<OtisKit*>(module); if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load a folder of WAVs into slots 1-8…", "", [m] {
            char* path = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
            if (path) { m->loadFolder(path); std::free(path); }
        }));
        for (int s = 0; s < SLOTS; ++s) menu->addChild(createSubmenuItem(m->slotLabel(s), "", [m, s](Menu* child) {
            child->addChild(createMenuItem("Load WAV…", "", [m, s] {
                auto* filters = osdialog_filters_parse("WAV:wav,WAV");
                char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters); osdialog_filters_free(filters);
                if (path) { m->loadSlot(s, path); std::free(path); }
            }));
            child->addChild(createMenuItem("Clear", "", [m, s] { m->clearSlot(s); }));
        }));
        if (!m->lastError.empty()) menu->addChild(createMenuLabel("Error: " + m->lastError));
        menu->addChild(createSubmenuItem("Chip clock", "", [m](Menu* child) {
            const char* names[] = {"29.8 kHz (16-voice clock)", "44.6 kHz (14-voice clock)", "78.1 kHz (8-voice clock)"};
            for (int k = 0; k < 3; ++k) child->addChild(createCheckMenuItem(names[k], "",
                [m, k] { return m->engineMode.load() == k; }, [m, k] { m->engineMode = k; }));
        }));
        menu->addChild(createBoolMenuItem("Drop-sample playback (no interpolation)", "", [m] { return m->dropSample.load(); }, [m](bool on) { m->dropSample = on; }));
        menu->addChild(createMenuLabel(string::f("Bank: %.1f of %.1f s at 44.1 kHz", m->layout.frames / otis::kit::BANK_RATE,
            otis::memoryWords / otis::kit::BANK_RATE)));
    }
};
Model* modelOtisKit = createModel<OtisKit, OtisKitWidget>("OtisKit");
