// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/CzVoice.hpp"
#include "../../shared/PanelWidgets.hpp"
#include "Layout.hpp"
#include <array>
#include <atomic>

struct CzishModule : Module {
    enum Params { WAVE, WINDOW, SHAPE, ATTACK, RELEASE, LEVEL, TRACK, RECON, NUM_PARAMS };
    enum Inputs { VOCT, GATE, DCW_CV, XOR_BUS, NUM_INPUTS };
    enum Outputs { AUDIO, BUS_OUT, NUM_OUTPUTS };
    dspemu::cz::VoiceEngine engine;
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    std::atomic<unsigned> displayed{0};

    CzishModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(WAVE, 0, 7, 0, "Chip wave", {"Saw", "Square", "Pulse", "Silent (undocumented)",
            "Double sine", "Saw pulse", "Resonance", "Double pulse (undocumented)"});
        configSwitch(WINDOW, 0, 5, 0, "Chip window", {"None", "Saw", "Triangle", "Trapezoid", "Pulse (undocumented)", "Double saw (undocumented)"});
        configParam(SHAPE, 0, 127, 32, "DCW", " / 127");
        configParam(ATTACK, 2, 2000, 2, "Attack (provisional mapping)", " ms");
        configParam(RELEASE, 2, 4000, 250, "Release (provisional mapping)", " ms");
        configParam(LEVEL, 0, 8, 2, "Output gain", "x");
        configParam(TRACK, -.5f, .5f, 0, "Provisional CZ DAC range tracking error", "%", 0, 100);
        configSwitch(RECON,0,1,1,"Reconstruction",{"Raw hold","Clean sinc"});
        configInput(VOCT, "Pitch; C4 = 0 V; mono broadcasts");
        configInput(GATE, "Polyphonic gate; up to eight single-line voices");
        configInput(DCW_CV, "DCW CV; 10 V spans the full range; mono broadcasts");
        configInput(XOR_BUS, "XOR mask for the encoded DAC high byte; optional CLK on channel 9");
        configOutput(AUDIO, "Mono sum through provisional CZ DAC");
        configOutput(BUS_OUT, "Encoded summed DAC high byte after XOR; channel 9 is a safely divided device clock");
    }
    static float finite(float x, float fallback = 0) { return std::isfinite(x) ? x : fallback; }
    void process(const ProcessArgs& args) override {
        dspemu::cz::Parameters p;
        p.wave=int(std::lround(std::clamp(finite(params[WAVE].getValue()),0.f,7.f)));
        p.window=int(std::lround(std::clamp(finite(params[WINDOW].getValue()),0.f,5.f)));
        p.shape=finite(params[SHAPE].getValue()); p.attackMs=finite(params[ATTACK].getValue(),2);
        p.releaseMs=finite(params[RELEASE].getValue(),250); p.tracking=finite(params[TRACK].getValue());
        std::array<dspemu::cz::Input,8> in{};
        int channels=std::clamp(std::max(inputs[VOCT].getChannels(),inputs[GATE].getChannels()),0,8);
        for(int c=0;c<channels;++c) {
            in[c].pitch=finite(inputs[VOCT].getPolyVoltage(c)); in[c].gate=finite(inputs[GATE].getPolyVoltage(c));
            in[c].dcw=finite(inputs[DCW_CV].getPolyVoltage(c));
        }
        auto bus=reader.process(inputs[XOR_BUS]);
        float sample=engine.process(in,channels,p,bus.connected?bus.word:0,args.sampleRate,params[RECON].getValue()>=.5f);
        float level=std::clamp(finite(params[LEVEL].getValue(),2),0.f,8.f);
        outputs[AUDIO].setVoltage(std::clamp(5.f*sample*level,-10.f,10.f));
        const auto& tap=engine.output.tap; writer.write(outputs[BUS_OUT],tap.word,true,tap.clk);
        displayed.store(unsigned(tap.word)|(tap.divider<<8),std::memory_order_relaxed);
    }
    void onReset(const ResetEvent& e) override { Module::onReset(e); engine.reset(); reader.reset(); displayed.store(0); }
};
struct CzishDisplay : widget::Widget {
    CzishModule* module=nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font=APP->window->uiFont; if(!font) return;
        unsigned value=module?module->displayed.load(std::memory_order_relaxed):(2u<<8);
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,12);
        nvgTextAlign(args.vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE); nvgFillColor(args.vg,nvgRGB(143,220,196));
        nvgText(args.vg,box.size.x/2,box.size.y/2,string::f("%02X / %u",value&255,value>>8).c_str(),nullptr);
    }
};
struct CzishWidget : ModuleWidget {
    CzishWidget(CzishModule* module) {
        using namespace czish_layout::voice;
#define POS(N) Vec(N##_X,N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/Czish.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(czish_layout::voice::WIDTH-15,365)));
        auto* wave=createParam<dspemu::panel::Choice>(POS(WAVE),module,CzishModule::WAVE);
        wave->box.size=Vec(WAVE_W,WAVE_H); wave->labels={"SAW","SQUARE","PULSE","SILENT","DOUBLE SINE","SAW PULSE","RESONANCE","DOUBLE PULSE"}; addParam(wave);
        auto* window=createParam<dspemu::panel::Choice>(POS(WINDOW),module,CzishModule::WINDOW);
        window->box.size=Vec(WINDOW_W,WINDOW_H); window->labels={"NONE","SAW","TRIANGLE","TRAPEZOID","PULSE","DOUBLE SAW"}; addParam(window);
        const Vec knobs[]={POS(SHAPE),POS(ATTACK),POS(RELEASE),POS(LEVEL),POS(TRACK)};
        for(int i=0;i<5;++i) addParam(createParamCentered<RoundBlackKnob>(knobs[i],module,CzishModule::SHAPE+i));
        auto* display=createWidget<CzishDisplay>(POS(WORD));display->box.size=Vec(WORD_W,WORD_H);display->module=module;addChild(display);
        addParam(createParamCentered<CKSS>(POS(RECON),module,CzishModule::RECON));
        const Vec ins[]={POS(VOCT),POS(GATE),POS(DCW_CV),POS(XOR_BUS)},outs[]={POS(AUDIO),POS(BUS_OUT)};
        for(int i=0;i<4;++i)addInput(createInputCentered<PJ301MPort>(ins[i],module,i));
        for(int i=0;i<2;++i)addOutput(createOutputCentered<DarkPJ301MPort>(outs[i],module,i));
#undef POS
    }
};
Model* modelCzish=createModel<CzishModule,CzishWidget>("CZish");
