// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Lookup.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <osdialog.h>
#include <atomic>
#include <iomanip>
#include <memory>
#include <sstream>

struct LookupTableSlot {
    dspemu::lookup::Table values{};
    std::string name, source;
    bool select = false;
};
static std::string lookupHash(const dspemu::lookup::Table& table) {
    std::ostringstream text;
    text << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << dspemu::lookup::hash(table.data(),table.size());
    return text.str();
}
struct ByteLookup : Module {
    enum Params { FAMILY, SHAPE, PHASE, SEED, MODE, CLOCK_POLICY, RESET_BUTTON, JUMP_BIT, NUM_PARAMS };
    enum Inputs { BUS_IN, FAMILY_CV, SHAPE_CV, PHASE_CV, RESET_IN, SIGNAL_IN, JUMP_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, AUDIO_OUT, NUM_OUTPUTS };
    enum Lights { BIT0_LIGHT, BIT1_LIGHT, BIT2_LIGHT, BIT3_LIGHT, BIT4_LIGHT, BIT5_LIGHT,
        BIT6_LIGHT, BIT7_LIGHT, LOCK_LIGHT, JUMP_LIGHT, NUM_LIGHTS };
    std::shared_ptr<const dspemu::lookup::Library> library;
    dspemu::lookup::Engine engine;
    dspemu::bus::Reader reader, jumpReader; dspemu::bus::Writer writer;
    dspemu::lookup::Jump jump;
    std::atomic<int> jumpSteps{0}; // jump.steps, for saving from the UI thread
    dspemu::bus::Gate resetGate; bool resetButtonHigh = false;
    std::atomic<unsigned> displayedWord{128};
    std::atomic<bool> libraryMismatch{false};
    std::atomic<LookupTableSlot*> pending{nullptr}, retired{nullptr};
    LookupTableSlot* live = nullptr; // Audio thread.
    LookupTableSlot* uiTable = nullptr; // Latest published slot; UI thread.
    std::string libraryError, tableError;

    explicit ByteLookup(std::shared_ptr<const dspemu::lookup::Library> supplied = {}) : library(std::move(supplied)) {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        configSwitch(FAMILY,0,6,0,"Family",{"Harmonic","Formant","Fold","Curve","Walsh","Rhythm","Custom"});
        configParam(SHAPE,0,64,0,"Shape (circular)");
        configParam(PHASE,-1,1,0,"Phase"," cycles");
        configParam(SEED,0,255,0,"Pattern seed (Walsh / Rhythm)"); getParamQuantity(SEED)->snapEnabled = true;
        configSwitch(MODE,0,2,0,"Phase mode",{"Map","Track rising ramp","Track falling ramp"});
        configSwitch(CLOCK_POLICY,0,1,0,"Map clock policy",{"Follow bus CLK","Engine rate (unclocked output)"});
        configButton(RESET_BUTTON,"Reset ramp follower and JUMP steps");
        configSwitch(JUMP_BIT,0,7,0,"JUMP bit",{"Bit 0","Bit 1","Bit 2","Bit 3","Bit 4","Bit 5","Bit 6","Bit 7"});
        configInput(BUS_IN,"Byte address; optional CLK on channel 9");
        configInput(FAMILY_CV,"Family CV (1 V per family, clamped)");
        configInput(SHAPE_CV,"Shape CV (10 V per 64 shapes)");
        configInput(PHASE_CV,"Phase CV (1 V per cycle)");
        configInput(RESET_IN,"Reset ramp follower and JUMP steps");
        configInput(SIGNAL_IN,"Analog address (±5 V); used when BUS IN is unconnected");
        configInput(JUMP_IN,"Jump: SHAPE steps by one on each publication with the selected bit high (a gate is bit 0)");
        configOutput(BUS_OUT,"Transformed byte bus"); configOutput(AUDIO_OUT,"Raw DAC (0x80 = 0 V; nominal ±5 V)");
        if (!library) try {
            if (!pluginInstance) throw std::runtime_error("Lookup library path unavailable");
            library = std::make_shared<dspemu::lookup::Library>(dspemu::lookup::Library::load(asset::plugin(pluginInstance,"res/lookup-v1.bin")));
        } catch (const std::exception& error) { libraryError = error.what(); library = std::make_shared<dspemu::lookup::Library>(); }
    }
    ~ByteLookup() { delete live; delete pending.load(); delete retired.load(); }
    void collect() { if (auto* old = retired.exchange(nullptr,std::memory_order_acq_rel)) delete old; }
    void publish(LookupTableSlot* slot) {
        collect(); uiTable = slot;
        if (auto* old = pending.exchange(slot,std::memory_order_acq_rel)) delete old;
    }
    void loadJson(json_t* root, bool select) {
        auto stringValue = [](json_t* root,const char* key) -> std::string {
            auto* v = json_object_get(root,key); return json_is_string(v) ? json_string_value(v) : "";
        };
        if (!json_is_object(root) || stringValue(root,"format") != "ByteBusLab.LookupTable" ||
            json_integer_value(json_object_get(root,"version")) != 1) throw std::runtime_error("Unsupported Lookup table format/version");
        json_t* values = json_object_get(root,"values");
        if (!json_is_array(values) || json_array_size(values) != 256) throw std::runtime_error("Table needs exactly 256 integer values");
        auto slot = std::make_unique<LookupTableSlot>();
        for (int n = 0; n < 256; ++n) {
            auto* v = json_array_get(values,n);
            if (!json_is_integer(v) || json_integer_value(v) < 0 || json_integer_value(v) > 255) throw std::runtime_error("Table values must be integers 0–255");
            slot->values[n] = uint8_t(json_integer_value(v));
        }
        if (stringValue(root,"hash") != lookupHash(slot->values)) throw std::runtime_error("Table fingerprint mismatch");
        slot->name = stringValue(root,"name"); if (slot->name.size() > 128) throw std::runtime_error("Table name exceeds 128 bytes");
        if (auto* source = json_object_get(root,"source")) {
            if (!json_is_object(source)) throw std::runtime_error("Table source must be an object");
            char* text = json_dumps(source,JSON_COMPACT | JSON_SORT_KEYS);
            if (text) { slot->source = text; std::free(text); }
            if (slot->source.size() > 65536) throw std::runtime_error("Table source exceeds 64 KiB");
        }
        slot->select = select; publish(slot.release());
    }
    void loadFile(const std::string& path) {
        try {
            std::ifstream file(path,std::ios::binary);
            std::string bytes; char buffer[4096];
            while (file.read(buffer,sizeof(buffer)) || file.gcount()) {
                bytes.append(buffer,size_t(file.gcount()));
                if (bytes.size() > 1024 * 1024) throw std::runtime_error("Table file exceeds 1 MiB");
            }
            json_error_t error; json_t* root = json_loadb(bytes.data(),bytes.size(),JSON_REJECT_DUPLICATES,&error);
            if (!root) throw std::runtime_error("Cannot read table JSON");
            try { loadJson(root,true); } catch (...) { json_decref(root); throw; }
            json_decref(root); tableError.clear();
        } catch (const std::exception& error) { tableError = error.what(); }
    }
    void process(const ProcessArgs& args) override {
        if (!retired.load(std::memory_order_acquire)) if (auto* slot = pending.exchange(nullptr,std::memory_order_acq_rel)) {
            if (live) retired.store(live,std::memory_order_release);
            live = slot;
            if (slot->select) { params[FAMILY].setValue(6); params[SHAPE].setValue(32); }
        }
        auto finite = [](float v) { return std::isfinite(v) ? v : 0.f; };
        auto cv = [&](int port) { return inputs[port].isConnected() ? finite(inputs[port].getVoltage()) : 0.f; };
        dspemu::lookup::Parameters p;
        p.family = int(std::round(clamp(finite(params[FAMILY].getValue()) + cv(FAMILY_CV),0.f,6.f)));
        p.shape = finite(params[SHAPE].getValue()) + 6.4 * cv(SHAPE_CV);
        p.phase = finite(params[PHASE].getValue()) + cv(PHASE_CV);
        p.seed = uint8_t(std::round(clamp(finite(params[SEED].getValue()),0.f,255.f)));
        p.mode = dspemu::lookup::Mode(int(std::round(clamp(finite(params[MODE].getValue()),0.f,2.f))));
        p.engineClock = params[CLOCK_POLICY].getValue() >= .5f;
        bool button = params[RESET_BUTTON].getValue() >= .5f;
        bool reset = resetGate.process(cv(RESET_IN)) | (button && !resetButtonHigh); resetButtonHigh = button;
        // JUMP is decoded before the address is evaluated, so a step applies on its own sample.
        // RESET wins over a simultaneous step.
        int bit = int(std::round(clamp(finite(params[JUMP_BIT].getValue()),0.f,7.f)));
        bool stepped = jump.tick(jumpReader.process(inputs[JUMP_IN]),bit) && !reset;
        if (reset) jump.steps = 0;
        p.shape += jump.steps; jumpSteps.store(jump.steps,std::memory_order_relaxed);
        auto data = reader.process(inputs[BUS_IN]);
        if (!data.connected && inputs[SIGNAL_IN].isConnected()) {
            data.connected = data.updated = true;
            data.rawWord = data.word = dspemu::bus::fromVolts(inputs[SIGNAL_IN].getVoltage());
        }
        dspemu::lookup::Output out;
        if (!libraryMismatch.load(std::memory_order_relaxed) || p.family == dspemu::lookup::Custom)
            out = engine.tick(*library,p,data,args.sampleRate,reset,live ? &live->values : nullptr);
        else {
            out.clocked = data.clocked && p.mode == dspemu::lookup::Mode::Map && !p.engineClock;
            out.clk = out.clocked && data.clk;
        }
        writer.write(outputs[BUS_OUT],out.word,out.clocked,out.clk);
        outputs[AUDIO_OUT].setVoltage(dspemu::bus::toVolts(out.word));
        displayedWord.store(out.word,std::memory_order_relaxed);
        for (int k = 0; k < 8; ++k) lights[BIT0_LIGHT+k].setBrightness(out.word & (1u<<k) ? 1.f : 0.f);
        lights[LOCK_LIGHT].setBrightness(out.locked ? 1.f : 0.f);
        lights[JUMP_LIGHT].setBrightnessSmooth(stepped ? 1.f : 0.f,args.sampleTime);
    }
    void onReset(const ResetEvent& event) override {
        Module::onReset(event); engine.reset(); reader.reset(); resetGate.reset(); resetButtonHigh = false;
        jumpReader.reset(); jump.clear(); jumpSteps.store(0);
        libraryMismatch.store(false); displayedWord.store(128);
    }
    json_t* dataToJson() override {
        json_t* root = json_object(); json_object_set_new(root,"library_sha256",json_string(dspemu::lookup::LIBRARY_SHA256));
        json_object_set_new(root,"jump",json_integer(jumpSteps.load(std::memory_order_relaxed)));
        if (uiTable) {
            json_t* table = json_object();
            json_object_set_new(table,"format",json_string("ByteBusLab.LookupTable"));
            json_object_set_new(table,"version",json_integer(1)); json_object_set_new(table,"name",json_string(uiTable->name.c_str()));
            json_object_set_new(table,"hash",json_string(lookupHash(uiTable->values).c_str()));
            json_t* values = json_array(); for (uint8_t v : uiTable->values) json_array_append_new(values,json_integer(v));
            json_object_set_new(table,"values",values);
            if (!uiTable->source.empty()) json_object_set_new(table,"source",json_loads(uiTable->source.c_str(),0,nullptr));
            json_object_set_new(root,"custom",table);
        }
        return root;
    }
    void dataFromJson(json_t* root) override {
        auto* id = json_object_get(root,"library_sha256");
        libraryMismatch.store(json_is_string(id) && std::string(json_string_value(id)) != dspemu::lookup::LIBRARY_SHA256);
        if (auto* table = json_object_get(root,"custom")) try { loadJson(table,false); tableError.clear(); }
        catch (const std::exception& error) { tableError = error.what(); }
        else {
            auto slot = std::make_unique<LookupTableSlot>(); for (int n = 0; n < 256; ++n) slot->values[n] = n;
            slot->name = "Identity"; publish(slot.release());
        }
        engine.reset(); reader.reset();
        // Patches before ByteBusLab 2.5 have no JUMP steps.
        auto* steps = json_object_get(root,"jump");
        int saved = json_is_integer(steps) ? int(std::max<json_int_t>(0,std::min<json_int_t>(63,json_integer_value(steps)))) : 0;
        jumpReader.reset(); jump.clear(); jump.steps = saved; jumpSteps.store(saved);
    }
};

struct LookupDisplay : widget::Widget {
    ByteLookup* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font = APP->window->uiFont; if (!font) return;
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,16); nvgTextAlign(args.vg,NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(143,220,196));
        unsigned value = module ? module->displayedWord.load(std::memory_order_relaxed) : 128;
        nvgText(args.vg,box.size.x/2,box.size.y/2,string::f("%02X",value).c_str(),nullptr);
    }
};
struct ByteLookupWidget : ModuleWidget {
    ByteLookupWidget(ByteLookup* module) {
        using namespace bytebus_layout::lookup;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/Lookup.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::lookup::WIDTH-15,365)));
        auto* family = createParam<dspemu::panel::Choice>(POS(FAMILY),module,ByteLookup::FAMILY);
        family->box.size = Vec(FAMILY_W,FAMILY_H);
        family->labels = {"HARMONIC","FORMANT","FOLD","CURVE","WALSH","RHYTHM","CUSTOM"}; addParam(family);
        addParam(createParamCentered<RoundBlackKnob>(POS(SHAPE),module,ByteLookup::SHAPE));
        addParam(createParamCentered<RoundBlackKnob>(POS(PHASE),module,ByteLookup::PHASE));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(SEED),module,ByteLookup::SEED));
        auto* mode = createParam<dspemu::panel::Choice>(POS(MODE),module,ByteLookup::MODE);
        mode->box.size = Vec(MODE_W,MODE_H);
        mode->labels = {"MAP","RISE","FALL"}; addParam(mode);
        addParam(createParamCentered<VCVButton>(POS(RESET),module,ByteLookup::RESET_BUTTON));
        const Vec bits[] = {POS(BIT0),POS(BIT1),POS(BIT2),POS(BIT3),POS(BIT4),POS(BIT5),POS(BIT6),POS(BIT7)};
        for (int k = 0; k < 8; ++k) addChild(createLightCentered<SmallLight<GreenLight>>(bits[k],module,ByteLookup::BIT0_LIGHT+k));
        auto* display = createWidget<LookupDisplay>(POS(WORD)); display->box.size = Vec(WORD_W,WORD_H); display->module = module; addChild(display);
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(LOCK),module,ByteLookup::LOCK_LIGHT));
        addInput(createInputCentered<PJ301MPort>(POS(FAMILY_CV),module,ByteLookup::FAMILY_CV));
        addInput(createInputCentered<PJ301MPort>(POS(SHAPE_CV),module,ByteLookup::SHAPE_CV));
        addInput(createInputCentered<PJ301MPort>(POS(PHASE_CV),module,ByteLookup::PHASE_CV));
        addInput(createInputCentered<PJ301MPort>(POS(SIGNAL_IN),module,ByteLookup::SIGNAL_IN));
        addInput(createInputCentered<PJ301MPort>(POS(BUS_IN),module,ByteLookup::BUS_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(BUS_OUT),module,ByteLookup::BUS_OUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(POS(AUDIO_OUT),module,ByteLookup::AUDIO_OUT));
        addInput(createInputCentered<PJ301MPort>(POS(RESET_IN),module,ByteLookup::RESET_IN));
        addInput(createInputCentered<PJ301MPort>(POS(JUMP_IN),module,ByteLookup::JUMP_IN));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(JUMP_LIGHT),module,ByteLookup::JUMP_LIGHT));
        auto* jumpBit = createParam<dspemu::panel::Choice>(POS(JUMP_BIT),module,ByteLookup::JUMP_BIT);
        jumpBit->box.size = Vec(JUMP_BIT_W,JUMP_BIT_H);
        jumpBit->labels = {"BIT 0","BIT 1","BIT 2","BIT 3","BIT 4","BIT 5","BIT 6","BIT 7"}; addParam(jumpBit);
#undef POS
    }
    void step() override {
        if (auto* m = dynamic_cast<ByteLookup*>(module)) m->collect();
        ModuleWidget::step();
    }
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<ByteLookup*>(module); if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load original Lookup table (.json)…","",[m] {
            auto* filters = osdialog_filters_parse("Lookup table:json");
            char* path = osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if (path) { m->loadFile(path); std::free(path); }
        }));
        menu->addChild(createMenuLabel("Custom: " + (m->uiTable ? m->uiTable->name : std::string("Identity"))));
        for (int policy = 0; policy < 2; ++policy)
            menu->addChild(createCheckMenuItem(policy ? "Map at engine rate (8 channels)" : "Map follows bus CLK", "",
                [m,policy] { return int(m->params[ByteLookup::CLOCK_POLICY].getValue()) == policy; },
                [m,policy] { m->getParamQuantity(ByteLookup::CLOCK_POLICY)->setValue(float(policy)); }));
        menu->addChild(createMenuLabel(string::f("JUMP steps: +%d (RESET clears)",m->jumpSteps.load())));
        if (!m->tableError.empty()) menu->addChild(createMenuLabel(m->tableError));
        if (!m->libraryError.empty()) menu->addChild(createMenuLabel(m->libraryError));
        if (m->libraryMismatch.load()) menu->addChild(createMenuLabel("Patch requests a different library; built-in output is neutral"));
    }
};
Model* modelLookup = createModel<ByteLookup,ByteLookupWidget>("Lookup");
