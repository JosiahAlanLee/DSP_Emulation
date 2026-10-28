// Pair actual Rack modules through one-sample cables in randomized process order.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Adapter.cpp"
#include "../src/Converter.cpp"
#include "../src/Register.cpp"
#include "../../czish/src/DacStage.cpp"
#include "../../czish/src/Czish.cpp"
#include "../../vastish/src/Vastish.cpp"
#include "../../otislab/src/OtisSampler.cpp"
#include "../../seance/src/engine/Bridge.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <random>
#include <set>

Plugin* pluginInstance=nullptr;
static Module::ProcessArgs args(float sr) { Module::ProcessArgs a{}; a.sampleRate=sr; a.sampleTime=1/sr; return a; }
static void outputs(Module& m) { for (auto& p:m.outputs) p.channels=1; }
static void set(Input& p,uint8_t n,bool clocked=true,bool clk=false) {
    p.channels=clocked ? 9 : 8;
    dspemu::bus::Writer{}.write(p,n,clocked,clk);
}
static uint8_t byte(Output& p) {
    uint8_t n=0; for (int k=0;k<8;++k) if (p.getVoltage(k)>=1) n|=uint8_t(1<<k); return n;
}
static void copy(Output& from,Input& to) {
    to.channels=from.getChannels();
    for (int k=0;k<16;++k) to.setVoltage(k<to.channels ? from.getVoltage(k) : 0,k);
}
template <size_t N> static void step(Module* (&modules)[N],Module::ProcessArgs& a,std::mt19937& random) {
    std::shuffle(std::begin(modules),std::end(modules),random);
    for (auto* module:modules) module->process(a);
    ++a.frame;
}
static void presetsAndRestore() {
    ByteAdapter m; m.model=modelAdapter; outputs(m); auto a=args(48000);
    assert(m.params.size()==16 && m.inputs.size()==3 && m.outputs.size()==6);
    m.requestedPreset=int(dspemu::adapter::Preset::CasioAudio);
    set(m.inputs[m.BUS_IN],0x50,true,true); m.process(a);
    assert(byte(m.outputs[m.BUS_OUT])==128 && m.outputs[m.BUS_OUT].getChannels()==9);
    for (int k=0;k<8;++k) assert(m.outputs[m.TRIG_OUT].getVoltage(k)==0);
    set(m.inputs[m.BUS_IN],0x50,true,false); m.process(a);
    set(m.inputs[m.BUS_IN],0x50,true,true); m.process(a);
    assert(byte(m.outputs[m.BUS_OUT])==160 && m.outputs[m.BIPOLAR_OUT].getVoltage()==1.25f);
    assert(m.outputs[m.GATES_OUT].getChannels()==8 && m.outputs[m.TRIG_OUT].getChannels()==8);
    m.params[m.MAP0].setValue(7); m.params[m.MAP1].setValue(-1);
    m.params[m.XOR].setValue(2); m.params[m.CZ_TRACK].setValue(.25f);
    json_t* saved=m.toJson();
    ByteAdapter restored; restored.model=modelAdapter; outputs(restored); restored.fromJson(saved);
    assert(restored.params[m.INPUT_FORMAT].getValue()==2 && restored.params[m.IDLE].getValue()==128);
    assert(restored.params[m.MAP0].getValue()==7 && restored.params[m.MAP1].getValue()==-1);
    assert(restored.params[m.XOR].getValue()==2 && restored.params[m.CZ_TRACK].getValue()==.25f);
    set(restored.inputs[m.BUS_IN],0xff,true,true); restored.process(a);
    assert(byte(restored.outputs[m.BUS_OUT])==128 && restored.engine.initialized);
    for (int k=0;k<8;++k) assert(restored.outputs[m.TRIG_OUT].getVoltage(k)==0);
    // Restore into a running instance also clears reader/timing state.
    m.fromJson(saved); m.process(a); assert(byte(m.outputs[m.BUS_OUT])==128);
    json_decref(saved);
    for (int id=0;id<m.NUM_PARAMS;++id) m.params[id].setValue(NAN);
    for (int i=0;i<8;++i) { m.process(a); assert(std::isfinite(m.outputs[m.BIPOLAR_OUT].getVoltage())); }
    m.loadRequested=true; m.process(a);
    assert(m.engine.word==0); // Invalid IDLE is sanitized to the default.
}
static void dacPairs(float sr) {
    using P=dspemu::adapter::Preset;
    const P presets[]={P::YamahaAudio,P::CasioAudio,P::LinearAudio};
    for (int profile=0;profile<3;++profile) for (float input:{0.f,1.25f,-2.5f}) {
        DacStageModule source; source.model=modelDacStage; outputs(source);
        ByteAdapter adapter; adapter.model=modelAdapter; outputs(adapter); adapter.applyPreset(presets[profile]);
        ByteConverter receiver; receiver.model=modelConverter; outputs(receiver);
        ByteConverter raw; raw.model=modelConverter; outputs(raw);
        source.params[source.PROFILE].setValue(float(profile));
        source.params[source.RECON].setValue(0);
        source.inputs[source.SIGNAL].channels=1; source.inputs[source.SIGNAL].setVoltage(input);
        auto a=args(sr); std::mt19937 random(101+profile);
        Module* modules[]={&source,&adapter,&receiver,&raw};
        for (int n=0;n<1000;++n) {
            copy(source.outputs[source.BUS_OUT],adapter.inputs[adapter.BUS_IN]);
            copy(adapter.outputs[adapter.BUS_OUT],receiver.inputs[receiver.BUS_IN]);
            copy(source.outputs[source.BUS_OUT],raw.inputs[raw.BUS_IN]);
            step(modules,a,random);
            if (n>64) assert(std::abs(receiver.outputs[receiver.SIGNAL_OUT].getVoltage()-input)<1e-5f);
        }
        if (input==0) assert(raw.outputs[raw.SIGNAL_OUT].getVoltage()==(profile==0 ? -3.125f : -5.f));
    }
}
static void czishToVastish(float sr) {
    CzishModule source; source.model=modelCzish; outputs(source);
    ByteAdapter adapter; adapter.model=modelAdapter; outputs(adapter);
    adapter.applyPreset(dspemu::adapter::Preset::CasioAudio);
    Vastish receiver; receiver.model=modelVastish; outputs(receiver);
    for (int k=0;k<vastish::PARAM_COUNT;++k) receiver.params[k].setValue(vastish::programs()[0][k]);
    receiver.params[vastish::PITCH_WAVE].setValue(vastish::BUS_WAVE);
    receiver.params[vastish::PITCH_KT].setValue(0);
    receiver.params[vastish::BUS_OUT_MODE].setValue(1);
    receiver.params[vastish::BUS_OUT_SOURCE].setValue(vastish::SRC_BUS_A);
    auto a=args(sr); std::mt19937 random(333);
    Module* modules[]={&source,&adapter,&receiver};
    // Unplayed CZish publishes encoded zero; the numeric path must remain zero.
    for (int n=0;n<1000;++n) {
        copy(source.outputs[source.BUS_OUT],adapter.inputs[adapter.BUS_IN]);
        copy(adapter.outputs[adapter.BUS_OUT],receiver.inputs[Vastish::BUS_A_IN]);
        step(modules,a,random);
        if (n>64) assert(byte(receiver.outputs[Vastish::BUS_OUT])==128);
    }
    source.inputs[source.VOCT].channels=source.inputs[source.GATE].channels=1;
    source.inputs[source.GATE].setVoltage(10);
    bool sounded=false,converted=false;
    for (int n=0;n<int(sr*.15);++n) {
        uint8_t sent=byte(adapter.outputs[adapter.BUS_OUT]);
        copy(source.outputs[source.BUS_OUT],adapter.inputs[adapter.BUS_IN]);
        copy(adapter.outputs[adapter.BUS_OUT],receiver.inputs[Vastish::BUS_A_IN]);
        step(modules,a,random);
        assert(byte(receiver.outputs[Vastish::BUS_OUT])==sent);
        sounded|=std::abs(source.outputs[source.AUDIO].getVoltage())>.01f;
        converted|=sent!=128;
    }
    assert(sounded && converted && receiver.engine.faults==0);
}
static void gorgonTriggers(float sr) {
    ByteAdapter adapter; adapter.model=modelAdapter; outputs(adapter);
    adapter.applyPreset(dspemu::adapter::Preset::GorgonHits);
    auto a=args(sr); dspemu::bus::Gate tracks[16]; int hits[16]{},expected[16]{};
    const uint8_t words[]={0x41,0x41,0x43,0x40,0xff};
    for (int n=0;n<16*50+4;++n) {
        // Tracks input sees the previous sample of the eight-channel TRIG output.
        for (int k=0;k<16;++k)
            if (tracks[k].process(k<8 ? adapter.outputs[adapter.TRIG_OUT].getVoltage(k) : 0)) ++hits[k];
        bool clk=n>=16 && n%16==0;
        uint8_t word=words[(n/16)%5];
        if (clk) for (int k=0;k<6;++k) expected[k]+=bool(word & (1<<k));
        set(adapter.inputs[adapter.BUS_IN],word,true,clk); adapter.process(a); ++a.frame;
        assert(!(byte(adapter.outputs[adapter.BUS_OUT]) & 0xc0));
    }
    for (int k=0;k<16;++k) assert(hits[k]==expected[k]);
    adapter.applyPreset(dspemu::adapter::Preset::GorgonMutes);
    for (int n=0;n<256;++n) {
        set(adapter.inputs[adapter.BUS_IN],uint8_t(n),false); adapter.process(a); adapter.process(a);
        assert(byte(adapter.outputs[adapter.BUS_OUT])==((n&127)^63));
    }
}
static void otisAndSpectral(float sr) {
    ByteAdapter adapter; adapter.model=modelAdapter; outputs(adapter);
    adapter.applyPreset(dspemu::adapter::Preset::Slow);
    auto otis=std::make_unique<OtisSampler>(); otis->model=modelOtisSampler; outputs(*otis);
    std::vector<int16_t> pcm(8*256);
    for (size_t k=0;k<pcm.size();++k) pcm[k]=int16_t((k/256)*2000);
    otis->engine.install(pcm,otis::captureRate(5)); otis->engine.setHostRate(sr); otis->smoothFrameBus=false;
    otis->params[otis->WINDOW_MODE].setValue(2); otis->params[otis->SIZE].setValue(8); otis->params[otis->LOOP].setValue(1);
    dspemu::bus::Reader maskReader; Input maskPort;
    seance::SpectralBridge spectral; seance::BridgeParams bp; bp.slewMs=0;
    auto frame=std::make_unique<seance::FrameV1>();
    frame->size=512; frame->bins=257; frame->hop=128; frame->sampleRate=sr;
    std::set<size_t> selected;
    auto a=args(sr); std::mt19937 random(409);
    Module* modules[]={&adapter,otis.get()};
    int publications=0;
    for (int n=0;n<int(sr);++n) {
        uint8_t sent=byte(adapter.outputs[adapter.BUS_OUT]);
        copy(adapter.outputs[adapter.BUS_OUT],otis->inputs[otis->FRAME_BUS_IN]);
        copy(adapter.outputs[adapter.BUS_OUT],maskPort);
        auto mask=maskReader.process(maskPort);
        // Audio-rate encoded bytes are deliberately sampled as raw addresses/masks.
        set(adapter.inputs[adapter.BUS_IN],uint8_t(128+100*std::sin(2*3.141592653589793*1.37*n/sr)),true,(n&3)==0);
        otis->params[otis->PLAY].setValue(n==16 ? 1 : 0);
        step(modules,a,random);
        if (adapter.outputs[adapter.CLOCK_OUT].getVoltage()>1) ++publications;
        if (n>64) {
            size_t which=otis->engine.windowFor(0).begin/256;
            assert(which==size_t(std::lround(sent*7./255))); selected.insert(which);
        }
        if (n%128==0) {
            std::fill(frame->mag,frame->mag+frame->bins,1.f);
            spectral.process(*frame,mask.word,bp);
            for (int k=0;k<frame->bins;++k)
                assert(frame->mag[k]==((mask.word & (1u<<spectral.bands[k])) ? 1.f : 0.f));
        }
    }
    assert(publications>=8 && publications<=9 && selected.size()>=3);
}
static void examples(float sr) {
    auto read=[](const char* name) {
        json_error_t error;
        auto* patch=json_load_file(name,0,&error); assert(patch); return patch;
    };
    {
        auto* patch=read("../examples/adapter-cz-audio.vcv");
        DacStageModule dac; dac.model=modelDacStage; outputs(dac);
        ByteAdapter adapter; adapter.model=modelAdapter; outputs(adapter);
        ByteConverter converter; converter.model=modelConverter; outputs(converter);
        Module* byId[]={nullptr,nullptr,&dac,&adapter,&converter,nullptr};
        size_t index; json_t* j;
        json_array_foreach(json_object_get(patch,"modules"),index,j) {
            int id=int(json_integer_value(json_object_get(j,"id")));
            if (id>=2 && id<=4) byId[id]->fromJson(j);
        }
        assert(adapter.params[adapter.INPUT_FORMAT].getValue()==2 && adapter.params[adapter.IDLE].getValue()==128);
        auto a=args(sr); std::mt19937 random(998); Module* modules[]={&dac,&adapter,&converter};
        for (int n=0;n<1500;++n) {
            float signal=n<500 ? 0.f : n<1000 ? 1.25f : -2.5f;
            json_array_foreach(json_object_get(patch,"cables"),index,j) {
                int from=int(json_integer_value(json_object_get(j,"outputModuleId")));
                int to=int(json_integer_value(json_object_get(j,"inputModuleId")));
                int out=int(json_integer_value(json_object_get(j,"outputId")));
                int in=int(json_integer_value(json_object_get(j,"inputId")));
                if (to==5) continue; // Host-selected Core audio device, outside the software test.
                assert(to>=2 && to<=4 && in<int(byId[to]->inputs.size()));
                if (from==1) { byId[to]->inputs[in].channels=1; byId[to]->inputs[in].setVoltage(signal); }
                else { assert(from>=2 && from<=4 && out<int(byId[from]->outputs.size())); copy(byId[from]->outputs[out],byId[to]->inputs[in]); }
            }
            step(modules,a,random);
            if (n%500>64) assert(std::abs(converter.outputs[converter.SIGNAL_OUT].getVoltage()-signal)<1e-5f);
        }
        json_decref(patch);
    }
    {
        auto* patch=read("../examples/adapter-repeated-triggers.vcv");
        ClockedRegister source; source.model=modelClockedRegister; outputs(source);
        ByteAdapter adapter; adapter.model=modelAdapter; outputs(adapter);
        size_t index; json_t* j;
        json_array_foreach(json_object_get(patch,"modules"),index,j) {
            int id=int(json_integer_value(json_object_get(j,"id")));
            if (id==1) source.fromJson(j); else if (id==2) adapter.fromJson(j);
        }
        auto a=args(sr); std::mt19937 random(999); Module* modules[]={&source,&adapter};
        int hits=0; dspemu::bus::Gate detector;
        for (int n=0;n<int(sr);++n) {
            copy(source.outputs[source.BUS_OUT],adapter.inputs[adapter.BUS_IN]);
            step(modules,a,random);
            hits+=detector.process(adapter.outputs[adapter.TRIG_OUT].getVoltage(0));
            assert(!(byte(adapter.outputs[adapter.BUS_OUT]) & 0xc0));
            for (int k=1;k<8;++k) assert(adapter.outputs[adapter.TRIG_OUT].getVoltage(k)==0);
        }
        assert(hits>=7 && hits<=8 && source.state.word==0x41);
        json_decref(patch);
    }
}
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin bytes; bytes.slug="ByteBusLab"; bytes.version="2.17.0";
    bytes.addModel(modelAdapter); bytes.addModel(modelConverter); bytes.addModel(modelClockedRegister);
    Plugin cz; cz.slug="CZishLab"; cz.version="2.1.3"; cz.addModel(modelDacStage); cz.addModel(modelCzish);
    Plugin vast; vast.slug="VastishLab"; vast.version="2.7.4"; vast.addModel(modelVastish);
    Plugin otisPlugin; otisPlugin.slug="OtisLab"; otisPlugin.version="2.9.0"; otisPlugin.addModel(modelOtisSampler);
    rack::plugin::plugins={&bytes,&cz,&vast,&otisPlugin};
    presetsAndRestore();
    for (float sr:{44100.f,48000.f,96000.f}) {
        dacPairs(sr); czishToVastish(sr); gorgonTriggers(sr); otisAndSpectral(sr); examples(sr);
    }
    rack::plugin::plugins.clear();
    std::cout << "PASS: actual CZish/DAC Stage -> Adapter -> Converter/Vastish, Gorgon hit/trigger and mute maps, slow OTIS frames, Seance masks, both saved examples, JSON/lifecycle and delayed randomized cables at 44.1/48/96 kHz\n";
}
