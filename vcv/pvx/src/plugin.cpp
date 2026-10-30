// SPDX-License-Identifier: BSD-3-Clause
// Rack integration by Codex; portable DSP by DeepSeek, reviewed by Codex.
#include <rack.hpp>
#include "engine/PvxVoice.hpp"
#include "Layout.hpp"
using namespace rack;
Plugin* pluginInstance;
struct PVX : Module {
    enum Params {WAVE1,PW1,LEVEL1,WAVE2,PW2,LEVEL2,DETUNE,SYNC,NOISE,
        CUTOFF,RESONANCE,CUTOFF_DRIVE,FILTER_ENV,FILTER_TAP,DRIVE,ENV_MODE,LEVEL,OVERSAMPLE,
        ATTACK1,DECAY1,SUSTAIN1,RELEASE1,ATTACK2,DECAY2,SUSTAIN2,RELEASE2,NUM_PARAMS};
    enum Inputs {PITCH,GATE,EXTERNAL,CUTOFF_CV,RESONANCE_CV,NUM_INPUTS};
    enum Outputs {AUDIO,LOWPASS,BANDPASS,ENV1,ENV2,NUM_OUTPUTS};
    dspemu::pvx::VoiceEngine engine;
    dspemu::pvx::FilterCalibration calibration;
    PVX() {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,0);
        configSwitch(WAVE1,0,1,0,"VCO 1 wave",{"Saw","Pulse"});
        configParam(PW1,.05,.95,.5,"VCO 1 pulse width","%",0,100);
        configParam(LEVEL1,0,1,1,"VCO 1 level","%",0,100);
        configSwitch(WAVE2,0,1,0,"VCO 2 wave",{"Saw","Pulse"});
        configParam(PW2,.05,.95,.5,"VCO 2 pulse width","%",0,100);
        configParam(LEVEL2,0,1,.65,"VCO 2 level","%",0,100);
        configParam(DETUNE,-12,12,0,"VCO 2 detune"," semitones");
        configSwitch(SYNC,0,1,0,"Hard sync",{"Off","On"});
        configParam(NOISE,0,1,0,"Noise level","%",0,100);
        configParam(CUTOFF,std::log2(20.f),std::log2(20000.f),std::log2(1000.f),"Cutoff"," Hz",2);
        configParam(RESONANCE,0,1,.5,"Resonance","%",0,100);
        configParam(CUTOFF_DRIVE,0,1,.75,"Cutoff-dependent drive","%",0,100);
        configParam(FILTER_ENV,-4,4,1,"Filter envelope depth"," octaves");
        configSwitch(FILTER_TAP,0,1,0,"Filter mode",{"Low-pass","Band-pass"});
        configParam(DRIVE,0,1,.25,"Overdrive","%",0,100);
        configSwitch(ENV_MODE,0,1,1,"Envelope mode",{"AR","ADSR"});
        configParam(LEVEL,0,1,.7,"Output level","%",0,100);
        configSwitch(OVERSAMPLE,0,2,2,"Oversampling",{"1x","2x","4x"});
        for(int i:{ATTACK1,ATTACK2})configParam(i,-10,3,std::log2(.003f),"Envelope attack"," s",2);
        for(int i:{DECAY1,DECAY2})configParam(i,-10,3,std::log2(.18f),"Envelope decay"," s",2);
        for(int i:{SUSTAIN1,SUSTAIN2})configParam(i,0,1,.6,"Envelope sustain","%",0,100);
        for(int i:{RELEASE1,RELEASE2})configParam(i,-10,3,std::log2(.12f),"Envelope release"," s",2);
        configInput(PITCH,"Pitch (1 V/oct, C4 at 0 V; up to 8 voices)");
        configInput(GATE,"Gate (up to 8 voices)");configInput(EXTERNAL,"External audio (5 V full scale)");
        configInput(CUTOFF_CV,"Global cutoff (1 V/oct)");configInput(RESONANCE_CV,"Global resonance (10 V full scale)");
        configOutput(AUDIO,"Filtered, driven voice with VCA");configOutput(LOWPASS,"Raw low-pass filter");
        configOutput(BANDPASS,"Raw band-pass filter");configOutput(ENV1,"Filter envelope");configOutput(ENV2,"VCA envelope");
    }
    void onReset(const ResetEvent& e) override {Module::onReset(e);engine.reset();}
    void process(const ProcessArgs& args) override {
        using namespace dspemu::pvx;
        Parameters p;
        p.osc1Wave=int(params[WAVE1].getValue());p.osc2Wave=int(params[WAVE2].getValue());
        p.osc1Pw=params[PW1].getValue();p.osc2Pw=params[PW2].getValue();
        p.osc1Level=params[LEVEL1].getValue();p.osc2Level=params[LEVEL2].getValue();
        p.detune=params[DETUNE].getValue();p.sync=params[SYNC].getValue()>.5f;p.noiseLevel=params[NOISE].getValue();
        p.cutoff=std::exp2(params[CUTOFF].getValue());p.resonance=params[RESONANCE].getValue();
        p.cutoffDrive=params[CUTOFF_DRIVE].getValue();p.filterEnv=params[FILTER_ENV].getValue()*.5f;
        p.filterTap=int(params[FILTER_TAP].getValue());p.drive=params[DRIVE].getValue();
        p.envMode=int(params[ENV_MODE].getValue());p.level=params[LEVEL].getValue();
        p.oversample=1<<std::clamp(int(params[OVERSAMPLE].getValue()),0,2);
        p.env1Attack=std::exp2(params[ATTACK1].getValue());p.env1Decay=std::exp2(params[DECAY1].getValue());
        p.env1Sustain=params[SUSTAIN1].getValue();p.env1Release=std::exp2(params[RELEASE1].getValue());
        p.env2Attack=std::exp2(params[ATTACK2].getValue());p.env2Decay=std::exp2(params[DECAY2].getValue());
        p.env2Sustain=params[SUSTAIN2].getValue();p.env2Release=std::exp2(params[RELEASE2].getValue());
        p.extLevel=inputs[EXTERNAL].isConnected()?1.f:0.f;
        int channels=std::clamp(std::max({inputs[PITCH].getChannels(),inputs[GATE].getChannels(),inputs[EXTERNAL].getChannels(),1}),1,8);
        std::array<dspemu::pvx::Input,8> in{};std::array<float,8> out{},lp{},bp{};
        for(int c=0;c<channels;++c)in[c]={inputs[PITCH].getPolyVoltage(c),inputs[GATE].getPolyVoltage(c),inputs[EXTERNAL].getPolyVoltage(c)*.2f};
        engine.process(in,channels,p,calibration,args.sampleRate,inputs[CUTOFF_CV].getVoltage(),inputs[RESONANCE_CV].getVoltage()*.1f,out,lp,bp);
        for(auto& port:outputs)port.setChannels(channels);
        for(int c=0;c<channels;++c){
            outputs[AUDIO].setVoltage(5*out[c],c);outputs[LOWPASS].setVoltage(5*lp[c],c);outputs[BANDPASS].setVoltage(5*bp[c],c);
            outputs[ENV1].setVoltage(10*engine.state[c].env1.level,c);outputs[ENV2].setVoltage(10*engine.state[c].env2.level,c);
        }
    }
};
struct PVXWidget : ModuleWidget {
    PVXWidget(PVX* m){
        setModule(m);setPanel(createPanel(asset::plugin(pluginInstance,"res/PVX.svg")));
        for(int i=0;i<PVX::NUM_PARAMS;++i){
            Vec pos(pvx_layout::x[i],pvx_layout::y[i]);
            if(i==PVX::WAVE1||i==PVX::WAVE2||i==PVX::SYNC||i==PVX::FILTER_TAP||i==PVX::ENV_MODE)
                addParam(createParamCentered<CKSS>(pos,m,i));
            else if(i==PVX::OVERSAMPLE)addParam(createParamCentered<CKSSThree>(pos,m,i));
            else addParam(createParamCentered<RoundSmallBlackKnob>(pos,m,i));
        }
        for(int i=0;i<PVX::NUM_INPUTS;++i)addInput(createInputCentered<PJ301MPort>(Vec(36+i*72,315),m,i));
        for(int i=0;i<PVX::NUM_OUTPUTS;++i)addOutput(createOutputCentered<PJ301MPort>(Vec(36+i*72,360),m,i));
        addChild(createWidget<ScrewSilver>(Vec(0,0)));addChild(createWidget<ScrewSilver>(Vec(box.size.x-15,365)));
    }
};
Model* modelPVX=createModel<PVX,PVXWidget>("PVX");
void init(Plugin* p){pluginInstance=p;p->addModel(modelPVX);}
