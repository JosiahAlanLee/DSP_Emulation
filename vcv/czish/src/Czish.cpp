// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/CzVoice.hpp"
#include "engine/Sysex.hpp"
#include "../../shared/PanelWidgets.hpp"
#include "Layout.hpp"
#include <osdialog.h>
#include <array>
#include <atomic>
#include <fstream>

struct CzishModule : Module {
    // The prototype's IDs stay in place. Its two envelope-time parameters are
    // migrated to the new step envelopes when an old patch is loaded.
    enum Params {
        WAVE,
        WINDOW,
        SHAPE,
        ATTACK,
        RELEASE,
        LEVEL,
        TRACK,
        RECON,
        LINE_MODE,
        MOD,
        EDIT_LINE,
        WAVE_SECOND,
        WAVE_ENABLE,
        WAVE_LINE2,
        WINDOW_LINE2,
        SECOND_LINE2,
        ENABLE_LINE2,
        OCTAVE,
        DETUNE,
        FINE,
        DETUNE_SIGN,
        VIB_WAVE,
        VIB_DELAY,
        VIB_RATE,
        VIB_DEPTH,
        VIB_ON,
        DCA_KEY1,
        DCW_KEY1,
        DCA_KEY2,
        DCW_KEY2,
        ENV_SELECT,
        STEP_SELECT,
        TONE,
        NOISE,
        CZ_MODEL,
        BEND_RANGE,
        PORTA,
        PORTA_TIME,
        TUNING,
        CHIP_PM,
        CHIP_MUTE,
        ENV_BASE,
        ENV_SUS = ENV_BASE + 96,
        ENV_END = ENV_SUS + 6,
        NUM_PARAMS = ENV_END + 6
    };
    enum Inputs { VOCT, GATE, DCW_CV, XOR_BUS, VIB_CV, BEND_CV, NUM_INPUTS };
    enum Outputs { AUDIO, BUS_OUT, POLY_AUDIO, NUM_OUTPUTS };
    enum Lights { OVERFLOW_LIGHT, NUM_LIGHTS };
    dspemu::cz::VoiceEngine engine;
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    std::atomic<unsigned> displayed{0};
    std::atomic<bool> pendingReset{false};
    dspemu::cz::Tone cachedTone, sourceTemplate;
    uint64_t lastControl = ~uint64_t(0);
    std::array<float, 9> lowA{}, lowB{};
    CzishModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        const std::vector<std::string> waves = {
            "Saw",         "Square",    "Pulse",     "CHIP: silent DC",
            "Double sine", "Saw pulse", "Resonance", "CHIP: double pulse"};
        const std::vector<std::string> windows = {
            "None",        "Saw (Resonance I)", "Triangle (Resonance II)", "Trapezoid (Resonance III)",
            "CHIP: pulse", "CHIP: double saw",  "CHIP: double saw 6",      "CHIP: double saw 7"};
        configSwitch(WAVE, 0, 7, 0, "Line 1 first wave", waves);
        configSwitch(WINDOW, 0, 7, 0, "Line 1 window", windows);
        configParam(SHAPE, 0, 127, 127, "DCW envelope depth", " / 127");
        configParam(ATTACK, 2, 2000, 2, "Legacy attack; imported into DCA steps", " ms");
        configParam(RELEASE, 2, 4000, 250, "Legacy release; imported into DCA steps", " ms");
        configParam(LEVEL, 0, 8, 4, "Output gain", "x");
        configParam(TRACK, -.5f, .5f, 0, "Provisional DAC range tracking error", "%", 0, 100);
        configSwitch(RECON, 0, 1, 1, "Reconstruction", {"Minimum-phase hold", "Clean sinc"});
        configSwitch(
            LINE_MODE, 0, 3, 0, "Line select",
            {"1: eight notes", "2: eight notes", "1 + 1 prime: four notes", "1 + 2 prime: four notes"});
        configSwitch(MOD, 0, 2, 0, "Paired-line modulation", {"Off", "Ring", "Noise"});
        configSwitch(EDIT_LINE, 0, 1, 0, "Edit waveform/key follow line", {"Line 1", "Line 2"});
        configSwitch(WAVE_SECOND, 0, 7, 0, "Line 1 second wave", waves);
        configSwitch(WAVE_ENABLE, 0, 1, 0, "Line 1 second wave", {"Off", "Alternate cycles"});
        configSwitch(WAVE_LINE2, 0, 7, 0, "Line 2 first wave", waves);
        configSwitch(WINDOW_LINE2, 0, 7, 0, "Line 2 window", windows);
        configSwitch(SECOND_LINE2, 0, 7, 0, "Line 2 second wave", waves);
        configSwitch(ENABLE_LINE2, 0, 1, 0, "Line 2 second wave", {"Off", "Alternate cycles"});
        configParam(OCTAVE, -1, 1, 0, "Octave");
        getParamQuantity(OCTAVE)->snapEnabled = true;
        configParam(DETUNE, 0, 47, 0, "Detuned line coarse offset", " semitones");
        getParamQuantity(DETUNE)->snapEnabled = true;
        configParam(FINE, 0, 60, 0, "Detuned line fine offset", " / 60 semitone");
        getParamQuantity(FINE)->snapEnabled = true;
        configSwitch(DETUNE_SIGN, 0, 1, 0, "Detune direction", {"Up", "Down"});
        configSwitch(VIB_WAVE, 0, 3, 0, "Vibrato waveform", {"Triangle", "Saw up", "Saw down", "Square"});
        configParam(VIB_DELAY, 0, 99, 0, "Vibrato delay (provisional time curve)");
        configParam(VIB_RATE, 0, 99, 50, "Vibrato rate (provisional curve)");
        configParam(VIB_DEPTH, 0, 99, 0, "Vibrato depth (provisional curve)");
        configSwitch(VIB_ON, 0, 1, 1, "Vibrato", {"Off", "On"});
        for (int id : {VIB_DELAY, VIB_RATE, VIB_DEPTH})
            getParamQuantity(id)->snapEnabled = true;
        for (auto item : {std::pair<int, const char *>{DCA_KEY1, "Line 1 DCA key follow"},
                          {DCW_KEY1, "Line 1 DCW key follow"},
                          {DCA_KEY2, "Line 2 DCA key follow"},
                          {DCW_KEY2, "Line 2 DCW key follow"}}) {
            configParam(item.first, 0, 9, 0, item.second);
            getParamQuantity(item.first)->snapEnabled = true;
        }
        configSwitch(ENV_SELECT, 0, 5, 2, "Envelope", {"DCO 1", "DCW 1", "DCA 1", "DCO 2", "DCW 2", "DCA 2"});
        configParam(STEP_SELECT, 0, 7, 0, "Edit step", "", 0, 1, 1);
        getParamQuantity(STEP_SELECT)->snapEnabled = true;
        configParam(TONE, 1000, 24000, 20000, "DAC low-pass; maximum bypasses", " Hz");
        configParam(NOISE, -120, -40, -120, "Analog-side noise; minimum is off", " dBFS RMS");
        configSwitch(CZ_MODEL, 0, 1, 0, "Provisional Casio DAC model", {"Instantaneous", "Syllabic"});
        configParam(BEND_RANGE, 0, 12, 2, "Pitch bend range", " semitones");
        getParamQuantity(BEND_RANGE)->snapEnabled = true;
        configSwitch(PORTA, 0, 1, 0, "Portamento", {"Off", "On"});
        configParam(PORTA_TIME, 0, 99, 0, "Portamento time (provisional curve)");
        configParam(TUNING, 400, 480, 440, "Master tuning", " Hz");
        configSwitch(CHIP_PM, 0, 3, 0, "CHIP pitch modulation override",
                     {"Follow MOD", "Disabled", "Other voice", "Noise"});
        configSwitch(CHIP_MUTE, 0, 1, 0, "CHIP mute partner", {"Off", "On"});
        dspemu::cz::Tone init;
        for (int e = 0; e < 6; ++e) {
            auto &env = init.line[e / 3].env[e % 3];
            for (int s = 0; s < 8; ++s) {
                int id = ENV_BASE + e * 16 + s * 2;
                configParam(id, 0, 99, env.rate[s], string::f("Envelope %d step %d rate", e + 1, s + 1));
                configParam(id + 1, 0, 99, env.level[s],
                            string::f("Envelope %d step %d level", e + 1, s + 1));
                getParamQuantity(id)->snapEnabled = getParamQuantity(id + 1)->snapEnabled = true;
            }
            configParam(ENV_SUS + e, -1, 7, env.sustain,
                        string::f("Envelope %d sustain step; -1 is none", e + 1));
            getParamQuantity(ENV_SUS + e)->snapEnabled = true;
            configParam(ENV_END + e, 0, 7, env.end, string::f("Envelope %d end step", e + 1), "", 0, 1, 1);
            getParamQuantity(ENV_END + e)->snapEnabled = true;
        }
        configInput(VOCT, "Pitch; C4 = 0 V; mono broadcasts");
        configInput(GATE, "Gate; eight single-line or four paired notes");
        configInput(DCW_CV, "DCW envelope offset; 10 V spans full range");
        configInput(XOR_BUS, "Encoded DAC high-byte XOR mask; optional channel 9 clock");
        configInput(VIB_CV, "Vibrato depth; 10 V spans 0-99");
        configInput(BEND_CV, "Bend; ±5 V spans selected bend range");
        configOutput(AUDIO, "Mono sum through provisional CZ DAC");
        configOutput(BUS_OUT, "Encoded summed DAC high byte after XOR; clock safely divided");
        configOutput(POLY_AUDIO, "Independent note outputs; paired lines sum per note; extension");
        configLight(OVERFLOW_LIGHT, "Input channels exceed active line mode");
    }
    static float finite(float x, float fallback = 0) { return std::isfinite(x) ? x : fallback; }
    int integer(int id, int lo, int hi) {
        float v = params[id].getValue();
        return std::clamp(int(std::lround(std::isfinite(v) ? v : float(lo))), lo, hi);
    }
    dspemu::cz::Tone readTone(bool withSource = false) {
        dspemu::cz::Tone t = withSource ? sourceTemplate : dspemu::cz::Tone{};
        t.mode = integer(LINE_MODE, 0, 3);
        t.mod = integer(MOD, 0, 2);
        t.octave = integer(OCTAVE, -1, 1);
        t.detune = integer(DETUNE, 0, 47);
        t.fine = integer(FINE, 0, 60);
        t.detuneSign = integer(DETUNE_SIGN, 0, 1) ? -1 : 1;
        t.vibWave = integer(VIB_WAVE, 0, 3);
        t.vibDelay = integer(VIB_DELAY, 0, 99);
        t.vibRate = integer(VIB_RATE, 0, 99);
        t.vibDepth = integer(VIB_DEPTH, 0, 99);
        t.vibrato = integer(VIB_ON, 0, 1);
        t.portamento = integer(PORTA, 0, 1);
        t.portamentoTime = integer(PORTA_TIME, 0, 99);
        t.bendRange = integer(BEND_RANGE, 0, 12);
        t.tuning = std::clamp(finite(params[TUNING].getValue(), 440), 400.f, 480.f);
        t.chipPitchMod = integer(CHIP_PM, 0, 3);
        t.muteOther = integer(CHIP_MUTE, 0, 1);
        for (int l = 0; l < 2; ++l) {
            auto &line = t.line[l];
            line.wave = integer(l ? WAVE_LINE2 : WAVE, 0, 7);
            line.window = integer(l ? WINDOW_LINE2 : WINDOW, 0, 7);
            line.second = integer(l ? SECOND_LINE2 : WAVE_SECOND, 0, 7);
            line.alternate = integer(l ? ENABLE_LINE2 : WAVE_ENABLE, 0, 1);
            line.dcaKey = integer(l ? DCA_KEY2 : DCA_KEY1, 0, 9);
            line.dcwKey = integer(l ? DCW_KEY2 : DCW_KEY1, 0, 9);
            for (int e = 0; e < 3; ++e) {
                int index = l * 3 + e;
                auto &env = line.env[e];
                env.end = integer(ENV_END + index, 0, 7);
                env.sustain = integer(ENV_SUS + index, -1, 7);
                for (int s = 0; s < 8; ++s) {
                    env.rate[s] = integer(ENV_BASE + index * 16 + s * 2, 0, 99);
                    env.level[s] = integer(ENV_BASE + index * 16 + s * 2 + 1, 0, 99);
                }
            }
        }
        return t;
    }
    void setTone(const dspemu::cz::Tone &t) {
        auto set = [&](int id, int v) { params[id].setValue(float(v)); };
        set(LINE_MODE, t.mode);
        set(MOD, t.mod);
        set(OCTAVE, t.octave);
        set(DETUNE, t.detune);
        set(FINE, t.fine);
        set(DETUNE_SIGN, t.detuneSign < 0);
        set(VIB_WAVE, t.vibWave);
        set(VIB_DELAY, t.vibDelay);
        set(VIB_RATE, t.vibRate);
        set(VIB_DEPTH, t.vibDepth);
        set(CHIP_PM, t.chipPitchMod);
        set(CHIP_MUTE, t.muteOther);
        for (int l = 0; l < 2; ++l) {
            auto &line = t.line[l];
            set(l ? WAVE_LINE2 : WAVE, line.wave);
            set(l ? WINDOW_LINE2 : WINDOW, line.window);
            set(l ? SECOND_LINE2 : WAVE_SECOND, line.second);
            set(l ? ENABLE_LINE2 : WAVE_ENABLE, line.alternate);
            set(l ? DCA_KEY2 : DCA_KEY1, line.dcaKey);
            set(l ? DCW_KEY2 : DCW_KEY1, line.dcwKey);
            for (int e = 0; e < 3; ++e) {
                int n = l * 3 + e;
                auto &env = line.env[e];
                set(ENV_SUS + n, env.sustain);
                set(ENV_END + n, env.end);
                for (int s = 0; s < 8; ++s) {
                    set(ENV_BASE + n * 16 + s * 2, env.rate[s]);
                    set(ENV_BASE + n * 16 + s * 2 + 1, env.level[s]);
                }
            }
        }
        sourceTemplate = t;
        pendingReset.store(true, std::memory_order_release);
    }
    float filtered(float x, int channel, double host) {
        float tone = std::clamp(finite(params[TONE].getValue(), 20000), 1000.f, 24000.f);
        if (tone >= 23999) {
            lowA[channel] = lowB[channel] = x;
            return x;
        }
        float a = 1.f - std::exp(float(-2. * dspemu::rate::pi * tone / host));
        lowA[channel] += a * (x - lowA[channel]);
        lowB[channel] += a * (lowA[channel] - lowB[channel]);
        return lowB[channel];
    }
    void process(const ProcessArgs &args) override {
        if (pendingReset.exchange(false, std::memory_order_acquire)) {
            engine.reset();
            lastControl = ~uint64_t(0);
            lowA = {};
            lowB = {};
        }
        if (lastControl != engine.deviceFrames / 64) {
            cachedTone = readTone();
            lastControl = engine.deviceFrames / 64;
        }
        std::array<dspemu::cz::Input, 8> in{};
        int count = std::clamp(std::max(inputs[VOCT].getChannels(), inputs[GATE].getChannels()), 0, 8);
        for (int c = 0; c < count; ++c) {
            in[c] = {finite(inputs[VOCT].getPolyVoltage(c)), finite(inputs[GATE].getPolyVoltage(c)),
                     finite(inputs[DCW_CV].getPolyVoltage(c)), finite(inputs[VIB_CV].getPolyVoltage(c)),
                     finite(inputs[BEND_CV].getPolyVoltage(c))};
        }
        auto bus = reader.process(inputs[XOR_BUS]);
        dspemu::cz::RenderSettings p;
        p.depth = std::clamp(finite(params[SHAPE].getValue(), 127), 0.f, 127.f);
        p.tracking = std::clamp(finite(params[TRACK].getValue()), -.5f, .5f);
        p.noiseDb = std::clamp(finite(params[NOISE].getValue(), -120), -120.f, -40.f);
        p.czModel = integer(CZ_MODEL, 0, 1) ? dspemu::dac::CzModel::Syllabic : dspemu::dac::CzModel::Instant;
        p.polyEnabled = outputs[POLY_AUDIO].isConnected();
        double host = std::isfinite(args.sampleRate) && args.sampleRate >= 1000 ? args.sampleRate : 48000;
        float x = engine.process(in, count, cachedTone, bus.connected ? bus.word : 0, host,
                                 params[RECON].getValue() >= .5f, p);
        float gain = 5.f * std::clamp(finite(params[LEVEL].getValue(), 4), 0.f, 8.f);
        outputs[AUDIO].setVoltage(std::clamp(filtered(x, 0, host) * gain, -10.f, 10.f));
        int poly = engine.outputChannels(cachedTone);
        outputs[POLY_AUDIO].setChannels(poly);
        for (int c = 0; c < poly; ++c)
            outputs[POLY_AUDIO].setVoltage(
                std::clamp(filtered(engine.polySamples[c], c + 1, host) * gain, -10.f, 10.f), c);
        auto &tap = engine.output.tap;
        writer.write(outputs[BUS_OUT], tap.word, true, tap.clk);
        displayed.store(unsigned(tap.word) | (tap.divider << 8), std::memory_order_relaxed);
        lights[OVERFLOW_LIGHT].setBrightness(
            std::max(inputs[VOCT].getChannels(), inputs[GATE].getChannels()) >
            dspemu::cz::noteLimit(cachedTone));
    }
    json_t *dataToJson() override {
        auto *j = json_object();
        json_object_set_new(j, "schema", json_integer(2));
        if (sourceTemplate.sourceValid) {
            auto bytes = dspemu::cz::sysex::encode(sourceTemplate);
            auto *a = json_array();
            for (auto b : bytes)
                json_array_append_new(a, json_integer(b));
            json_object_set_new(j, "sourceSysEx", a);
        }
        return j;
    }
    void dataFromJson(json_t *j) override {
        sourceTemplate = {};
        auto *a = json_object_get(j, "sourceSysEx");
        if (json_is_array(a) && json_array_size(a) <= 264) {
            std::vector<uint8_t> b;
            for (size_t i = 0; i < json_array_size(a); ++i) {
                auto *v = json_array_get(a, i);
                if (!json_is_integer(v) || json_integer_value(v) < 0 || json_integer_value(v) > 255) {
                    b.clear();
                    break;
                }
                b.push_back(uint8_t(json_integer_value(v)));
            }
            std::string error;
            dspemu::cz::sysex::decode(b, sourceTemplate, error);
        }
        pendingReset.store(true, std::memory_order_release);
        lastControl = ~uint64_t(0);
    }
    void fromJson(json_t *j) override {
        Module::fromJson(j);
        auto *data = json_object_get(j, "data");
        if (!json_is_integer(json_object_get(data, "schema"))) {
            for (int l = 0; l < 2; ++l) {
                int dcw = l * 3 + 1, dca = l * 3 + 2;
                params[ENV_BASE + dcw * 16 + 1].setValue(99);
                params[ENV_BASE + dca * 16].setValue(float(dspemu::cz::fromCode(
                    dspemu::cz::EnvKind::Dca,
                    dspemu::cz::rateForMilliseconds(finite(params[ATTACK].getValue(), 2)), true)));
                params[ENV_BASE + dca * 16 + 2].setValue(float(dspemu::cz::fromCode(
                    dspemu::cz::EnvKind::Dca,
                    dspemu::cz::rateForMilliseconds(finite(params[RELEASE].getValue(), 250)), true)));
            }
        }
        lastControl = ~uint64_t(0);
    }
    void onReset(const ResetEvent &e) override {
        Module::onReset(e);
        engine.reset();
        reader.reset();
        displayed.store(0);
        lastControl = ~uint64_t(0);
        lowA = {};
        lowB = {};
        sourceTemplate = {};
    }
};

struct EnvelopeDisplay : widget::Widget {
    CzishModule *module = nullptr;
    void draw(const DrawArgs &a) override {
        auto *vg = a.vg;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 3);
        nvgFillColor(vg, nvgRGB(14, 20, 23));
        nvgFill(vg);
        auto font = APP->window->uiFont;
        if (!font)
            return;
        int e = module ? module->integer(CzishModule::ENV_SELECT, 0, 5) : 2,
            s = module ? module->integer(CzishModule::STEP_SELECT, 0, 7) : 0;
        auto tone = module ? module->readTone() : dspemu::cz::Tone{};
        auto &env = tone.line[e / 3].env[e % 3];
        const char *names[] = {"DCO 1", "DCW 1", "DCA 1", "DCO 2", "DCW 2", "DCA 2"};
        nvgFontFaceId(vg, font->handle);
        nvgFontSize(vg, 10);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(vg, nvgRGB(143, 220, 196));
        std::string text = string::f("%s  S%d  R%02d L%02d%s%s", names[e], s + 1, env.rate[s],
                                     s == env.end ? 0 : env.level[s], s == env.sustain ? " SUS" : "",
                                     s == env.end ? " END" : "");
        nvgText(vg, 6, 3, text.c_str(), nullptr);
        unsigned bus = module ? module->displayed.load() : 512;
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        nvgText(vg, box.size.x - 6, 3, string::f("%02X / %u", bus & 255, bus >> 8).c_str(), nullptr);
        nvgBeginPath(vg);
        nvgMoveTo(vg, 6, box.size.y - 4);
        for (int i = 0; i <= env.end; ++i) {
            float x = 6 + (box.size.x - 12) * (i + 1) / float(env.end + 1),
                  y = box.size.y - 4 - (box.size.y - 19) * (i == env.end ? 0 : env.level[i]) / 99.f;
            nvgLineTo(vg, x, y);
        }
        nvgStrokeColor(vg, nvgRGB(143, 220, 196));
        nvgStrokeWidth(vg, 1.3f);
        nvgStroke(vg);
    }
};
struct RemappedKnob : RoundBlackKnob {
    bool dragging = false;
    void onDragStart(const event::DragStart &e) override {
        dragging = true;
        RoundBlackKnob::onDragStart(e);
    }
    void onDragEnd(const event::DragEnd &e) override {
        RoundBlackKnob::onDragEnd(e);
        dragging = false;
    }
};
template <int Role> struct EnvKnob : RemappedKnob {
    // The LEVEL knob on an END step points at zero, the level the voice uses,
    // but the stored level is left alone so an imported tone's bytes survive.
    bool showZero = false;
    void step() override {
        bool zero = showZero;
        if (auto *m = dynamic_cast<CzishModule *>(dragging ? nullptr : module)) {
            int e = m->integer(CzishModule::ENV_SELECT, 0, 5), s = m->integer(CzishModule::STEP_SELECT, 0, 7);
            paramId = CzishModule::ENV_BASE + e * 16 + s * 2 + Role;
            zero = Role == 1 && s == m->integer(CzishModule::ENV_END + e, 0, 7);
        } else if (dragging)
            zero = false;
        if (zero != showZero) {
            showZero = zero;
            ChangeEvent change;
            onChange(change);
        }
        RoundBlackKnob::step();
    }
    void onChange(const ChangeEvent &e) override {
        RoundBlackKnob::onChange(e);
        if (showZero) {
            math::Vec center = sw->box.getCenter();
            tw->identity();
            tw->translate(center);
            tw->rotate(minAngle);
            tw->translate(center.neg());
            fb->setDirty();
        }
    }
};
struct LineChoice : dspemu::panel::Choice {
    int first = 0, second = 0;
    void step() override {
        if (auto *m = dynamic_cast<CzishModule *>(module))
            paramId = m->integer(CzishModule::EDIT_LINE, 0, 1) ? second : first;
        Choice::step();
    }
};
struct SecondChoice : LineChoice {
    void draw(const DrawArgs &a) override {
        auto *m = dynamic_cast<CzishModule *>(module);
        bool on = m && m->integer(m->integer(CzishModule::EDIT_LINE, 0, 1) ? CzishModule::ENABLE_LINE2
                                                                           : CzishModule::WAVE_ENABLE,
                                  0, 1);
        auto original = labels;
        if (!on)
            labels.assign(8, "OFF");
        Choice::draw(a);
        labels = std::move(original);
    }
    void onButton(const event::Button &e) override {
        auto *m = dynamic_cast<CzishModule *>(module);
        if (!m || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) {
            ParamWidget::onButton(e);
            return;
        }
        int enable =
            m->integer(CzishModule::EDIT_LINE, 0, 1) ? CzishModule::ENABLE_LINE2 : CzishModule::WAVE_ENABLE;
        int wave = paramId;
        auto set = [m, enable, wave](int value) {
            auto *group = new history::ComplexAction;
            group->name = "set second CZ wave";
            for (auto edit : {std::pair<int, int>{enable, value >= 0}, {wave, std::max(0, value)}}) {
                float old = m->params[edit.first].getValue();
                m->params[edit.first].setValue(float(edit.second));
                if (old == m->params[edit.first].getValue())
                    continue;
                auto *h = new history::ParamChange;
                h->moduleId = m->id;
                h->paramId = edit.first;
                h->oldValue = old;
                h->newValue = m->params[edit.first].getValue();
                group->push(h);
            }
            APP->history->push(group);
        };
        auto *menu = createMenu();
        menu->addChild(createMenuItem("Off", "", [set] { set(-1); }));
        for (size_t i = 0; i < labels.size(); ++i)
            menu->addChild(createMenuItem(labels[i], "", [set, i] { set(int(i)); }));
        e.consume(this);
    }
};
struct LineKnob : RemappedKnob {
    int first = 0, second = 0;
    void step() override {
        if (auto *m = dynamic_cast<CzishModule *>(dragging ? nullptr : module))
            paramId = m->integer(CzishModule::EDIT_LINE, 0, 1) ? second : first;
        RoundBlackKnob::step();
    }
};
struct EnvChoice : dspemu::panel::Choice {
    int base = 0;
    void step() override {
        if (auto *m = dynamic_cast<CzishModule *>(module))
            paramId = base + m->integer(CzishModule::ENV_SELECT, 0, 5);
        Choice::step();
    }
};
struct CzishWidget : ModuleWidget {
    CzishWidget(CzishModule *m) {
        using namespace czish_layout::voice;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(m);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Czish.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0, 0)));
        addChild(createWidget<ScrewSilver>(Vec(WIDTH - 15, 365)));
        auto choice = [&](Vec pos, float w, int param, std::vector<std::string> labels) {
            auto *p = createParam<dspemu::panel::Choice>(pos, m, param);
            p->box.size = Vec(w, 23);
            p->labels = std::move(labels);
            p->previewIndex = param == CzishModule::ENV_SELECT ? 2 : 0;
            addParam(p);
        };
        choice(POS(MODE), MODE_W, CzishModule::LINE_MODE, {"1", "2", "1 + 1'", "1 + 2'"});
        choice(POS(MOD), MOD_W, CzishModule::MOD, {"OFF", "RING", "NOISE"});
        choice(POS(EDIT), EDIT_W, CzishModule::EDIT_LINE, {"LINE 1", "LINE 2"});
        const std::vector<std::string> wave = {"SAW",         "SQUARE",    "PULSE",     "CHIP: DC",
                                               "DOUBLE SINE", "SAW PULSE", "RESONANCE", "CHIP: PULSE2"};
        auto lineChoice = [&](Vec pos, float w, int a, int b, std::vector<std::string> labels) {
            auto *p = createParam<LineChoice>(pos, m, a);
            p->first = a;
            p->second = b;
            p->box.size = Vec(w, 23);
            p->labels = std::move(labels);
            addParam(p);
        };
        lineChoice(POS(WAVE), WAVE_W, CzishModule::WAVE, CzishModule::WAVE_LINE2, wave);
        auto *secondChoice = createParam<SecondChoice>(POS(SECOND), m, CzishModule::WAVE_SECOND);
        secondChoice->first = CzishModule::WAVE_SECOND;
        secondChoice->second = CzishModule::SECOND_LINE2;
        secondChoice->box.size = Vec(SECOND_W, 23);
        secondChoice->labels = wave;
        addParam(secondChoice);
        lineChoice(POS(WINDOW), WINDOW_W, CzishModule::WINDOW, CzishModule::WINDOW_LINE2,
                   {"NONE", "SAW", "TRIANGLE", "TRAPEZOID", "CHIP: PULSE", "CHIP: SAW2", "CHIP: SAW6",
                    "CHIP: SAW7"});
        for (int role = 0; role < 2; ++role) {
            auto *p = createParamCentered<LineKnob>(role ? POS(DCW_KEY) : POS(DCA_KEY), m,
                                                    role ? CzishModule::DCW_KEY1 : CzishModule::DCA_KEY1);
            p->first = role ? CzishModule::DCW_KEY1 : CzishModule::DCA_KEY1;
            p->second = role ? CzishModule::DCW_KEY2 : CzishModule::DCA_KEY2;
            addParam(p);
        }
        choice(POS(ENV), ENV_W, CzishModule::ENV_SELECT,
               {"DCO 1", "DCW 1", "DCA 1", "DCO 2", "DCW 2", "DCA 2"});
        choice(POS(STEP), STEP_W, CzishModule::STEP_SELECT, {"1", "2", "3", "4", "5", "6", "7", "8"});
        addParam(createParamCentered<EnvKnob<0>>(POS(ENV_RATE), m, CzishModule::ENV_BASE));
        addParam(createParamCentered<EnvKnob<1>>(POS(ENV_LEVEL), m, CzishModule::ENV_BASE + 1));
        for (int role = 0; role < 2; ++role) {
            auto *p = createParam<EnvChoice>(role ? POS(END) : POS(SUSTAIN), m,
                                             role ? CzishModule::ENV_END : CzishModule::ENV_SUS);
            p->base = role ? CzishModule::ENV_END : CzishModule::ENV_SUS;
            p->box.size = Vec(role ? END_W : SUSTAIN_W, 23);
            p->previewIndex = 1;
            p->labels = role ? std::vector<std::string>{"1", "2", "3", "4", "5", "6", "7", "8"}
                             : std::vector<std::string>{"OFF", "1", "2", "3", "4", "5", "6", "7", "8"};
            addParam(p);
        }
        auto *display = createWidget<EnvelopeDisplay>(POS(DISPLAY));
        display->box.size = Vec(DISPLAY_W, DISPLAY_H);
        display->module = m;
        addChild(display);
        addParam(createParamCentered<CKSS>(POS(RECON), m, CzishModule::RECON));
        addParam(createParamCentered<RoundBlackKnob>(POS(SHAPE), m, CzishModule::SHAPE));
        addParam(createParamCentered<RoundBlackKnob>(POS(LEVEL), m, CzishModule::LEVEL));
        const Vec controls[] = {POS(OCTAVE),    POS(DETUNE),   POS(FINE),
                                POS(VIB_DELAY), POS(VIB_RATE), POS(VIB_DEPTH)};
        const int ids[] = {CzishModule::OCTAVE,    CzishModule::DETUNE,   CzishModule::FINE,
                           CzishModule::VIB_DELAY, CzishModule::VIB_RATE, CzishModule::VIB_DEPTH};
        for (int i = 0; i < 6; ++i)
            addParam(createParamCentered<RoundBlackKnob>(controls[i], m, ids[i]));
        const Vec ins[] = {POS(VOCT), POS(GATE), POS(DCW_CV), POS(XOR_BUS), POS(VIB_CV), POS(BEND_CV)};
        for (int i = 0; i < 6; ++i)
            addInput(createInputCentered<PJ301MPort>(ins[i], m, i));
        const Vec outs[] = {POS(AUDIO), POS(BUS_OUT), POS(POLY_AUDIO)};
        for (int i = 0; i < 3; ++i)
            addOutput(createOutputCentered<DarkPJ301MPort>(outs[i], m, i));
        addChild(createLightCentered<SmallLight<RedLight>>(POS(OVERFLOW), m, CzishModule::OVERFLOW_LIGHT));
#undef POS
    }
    void appendContextMenu(Menu *menu) override {
        auto *m = dynamic_cast<CzishModule *>(module);
        if (!m)
            return;
        menu->addChild(new MenuSeparator);
        auto index = [&](const char *name, int id, std::vector<std::string> names) {
            menu->addChild(createIndexSubmenuItem(
                name, names, [m, id] { return int(m->params[id].getValue()); },
                [m, id](int v) { m->params[id].setValue(float(v)); }));
        };
        index("Second wave, line 1", CzishModule::WAVE_ENABLE, {"Off", "Alternate cycles"});
        index("Second wave, line 2", CzishModule::ENABLE_LINE2, {"Off", "Alternate cycles"});
        index("Detune direction", CzishModule::DETUNE_SIGN, {"Up", "Down"});
        index("Vibrato wave", CzishModule::VIB_WAVE, {"Triangle", "Saw up", "Saw down", "Square"});
        index("Vibrato", CzishModule::VIB_ON, {"Off", "On"});
        index("Portamento", CzishModule::PORTA, {"Off", "On"});
        index("Casio DAC (provisional)", CzishModule::CZ_MODEL, {"Instantaneous", "Syllabic: 1 ms / 50 ms"});
        menu->addChild(createSubmenuItem("Performance and DAC settings", "", [m](Menu *sub) {
            for (int id : {CzishModule::PORTA_TIME, CzishModule::BEND_RANGE, CzishModule::TUNING,
                           CzishModule::TRACK, CzishModule::TONE, CzishModule::NOISE}) {
                auto *slider = new ui::Slider;
                slider->box.size.x = 240;
                slider->quantity = m->getParamQuantity(id);
                sub->addChild(slider);
            }
        }));
        index("CHIP: pitch modulation outside firmware", CzishModule::CHIP_PM,
              {"Follow MOD", "Disabled", "Other voice", "Noise"});
        index("CHIP: cancel partner from sum", CzishModule::CHIP_MUTE, {"Off", "On"});
        menu->addChild(createMenuItem("Import single-tone .syx…", "", [m] {
            auto *filters = osdialog_filters_parse("Casio single tone:syx");
            char *path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
            osdialog_filters_free(filters);
            if (!path)
                return;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            free(path);
            if (!file || file.tellg() > 264) {
                osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
                                 "Expected one 263/264-byte CZ-101 tone file.");
                return;
            }
            file.seekg(0);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
            dspemu::cz::Tone tone;
            std::string error;
            if (!dspemu::cz::sysex::decode(bytes, tone, error)) {
                osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, error.c_str());
                return;
            }
            auto *action = new history::ModuleChange;
            action->name = "import CZ tone";
            action->moduleId = m->id;
            action->oldModuleJ = m->toJson();
            m->setTone(tone);
            action->newModuleJ = m->toJson();
            APP->history->push(action);
        }));
        menu->addChild(createMenuItem("Export single-tone .syx…", "", [m] {
            auto tone = m->readTone(true);
            if (!dspemu::cz::sysex::hardwareTone(tone)) {
                osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
                                 "CHIP extensions are saved in Rack patches. Select supported waves/windows "
                                 "and disable CHIP overrides before exporting a hardware tone.");
                return;
            }
            auto *filters = osdialog_filters_parse("Casio single tone:syx");
            char *path = osdialog_file(OSDIALOG_SAVE, nullptr, "CZish-tone.syx", filters);
            osdialog_filters_free(filters);
            if (!path)
                return;
            auto bytes = dspemu::cz::sysex::encode(tone);
            std::ofstream file(path, std::ios::binary);
            free(path);
            file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            if (!file)
                osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not write tone file.");
        }));
    }
};
Model *modelCzish = createModel<CzishModule, CzishWidget>("CZish");
