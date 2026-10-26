// Actual Rack adapters with one-sample cable transport and randomized process order.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Converter.cpp"
#include "../src/Register.cpp"
#include "../src/Lookup.cpp"
#include "../../vastish/src/Vastish.cpp"
#include "../../otislab/src/OtisSampler.cpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <random>
#include <set>
Plugin* pluginInstance=nullptr;
static Module::ProcessArgs args(float sr=48000) { Module::ProcessArgs a{}; a.sampleRate=sr; a.sampleTime=1/sr; return a; }
static void outputs(Module& m) { for(auto& p:m.outputs) p.channels=1; }
static void set(Input& p,uint8_t n,bool clocked=false,bool clk=false) { p.channels=clocked ? 9 : 8; dspemu::bus::Writer{}.write(p,n,clocked,clk); }
static uint8_t word(Output& p) { uint8_t n=0; for(int k=0;k<8;++k) if(p.getVoltage(k)>=1) n|=1<<k; return n; }
static void copy(Output& src,Input& dst) { dst.channels=src.getChannels(); for(int k=0;k<16;++k) dst.setVoltage(k<dst.channels ? src.getVoltage(k) : 0,k); }
static void init(Vastish& v) { for(int i=0;i<PARAM_COUNT;++i) v.params[i].setValue(programs()[0][i]); v.params[PITCH_WAVE].setValue(BUS_WAVE); v.params[PITCH_KT].setValue(0); outputs(v); }
static void converter() {
    ByteConverter c; c.model=modelConverter; outputs(c); auto a=args();
    assert(c.params.size()==8 && c.inputs.size()==3 && c.outputs.size()==3);
    c.inputs[ByteConverter::SIGNAL_IN].channels=1;
    for(int n=0;n<256;++n) {
        c.inputs[ByteConverter::SIGNAL_IN].setVoltage(dspemu::bus::toVolts(n));
        set(c.inputs[ByteConverter::BUS_IN],n); c.process(a);
        assert(word(c.outputs[ByteConverter::BUS_OUT])==n && c.outputs[ByteConverter::BUS_OUT].getChannels()==8);
        assert(c.outputs[ByteConverter::SIGNAL_OUT].getVoltage()==dspemu::bus::toVolts(n));
    }
    c.inputs[ByteConverter::CLOCK_IN].channels=1; c.inputs[ByteConverter::CLOCK_IN].setVoltage(10); c.process(a);
    assert(c.outputs[ByteConverter::BUS_OUT].getChannels()==9 && c.outputs[ByteConverter::BUS_OUT].getVoltage(8)==10);
    c.inputs[ByteConverter::SIGNAL_IN].setVoltage(-5); c.inputs[ByteConverter::CLOCK_IN].setVoltage(0); c.process(a);
    assert(word(c.outputs[ByteConverter::BUS_OUT])==255);
    c.inputs[ByteConverter::CLOCK_IN].setVoltage(10); c.process(a); assert(word(c.outputs[ByteConverter::BUS_OUT])==0);
    c.params[ByteConverter::DAC_CLOCK].setValue(2);
    c.inputs[ByteConverter::CLOCK_IN].setVoltage(0); set(c.inputs[ByteConverter::BUS_IN],0xff,true,false); c.process(a);
    c.inputs[ByteConverter::CLOCK_IN].setVoltage(10); c.process(a);
    assert(c.outputs[ByteConverter::SIGNAL_OUT].getVoltage()==dspemu::bus::toVolts(255));
    c.params[ByteConverter::DAC_GAIN].setValue(-2); c.params[ByteConverter::DAC_OFFSET].setValue(1); c.process(a);
    assert(c.outputs[ByteConverter::SIGNAL_OUT].getVoltage()==1-2*dspemu::bus::toVolts(255));
    c.params[ByteConverter::ADC_RANGE].setValue(1);
    auto* saved=c.toJson(); ByteConverter restored; restored.model=modelConverter; restored.fromJson(saved); json_decref(saved);
    for(size_t i=0;i<c.params.size();++i) assert(c.params[i].getValue()==restored.params[i].getValue());
    c.inputs[ByteConverter::CLOCK_IN].channels=0; c.inputs[ByteConverter::SIGNAL_IN].setVoltage(NAN); c.process(a);
    assert(word(c.outputs[ByteConverter::BUS_OUT])==128 && c.lights[ByteConverter::CLIP_LIGHT].getBrightness()==1);
}
static void testVastish() {
    Vastish v; v.model=modelVastish; init(v); auto a=args();
    assert(v.params.size()==86 && v.inputs.size()==11 && v.outputs.size()==12); // Vastish 2.6 appends AUDIO IN (10)
    static_assert(Vastish::BUS_A_IN==8 && Vastish::BUS_B_IN==9 && Vastish::AUDIO_IN==10);
    for(int n=0;n<256;++n) {
        set(v.inputs[Vastish::BUS_A_IN],n); v.process(a);
        assert(word(v.outputs[Vastish::BUS_OUT])==n && v.outputs[Vastish::FUN4_OUT].getChannels()==8);
        assert(v.outputs[Vastish::LEFT_OUT].getVoltage()==0 && v.outputs[Vastish::RIGHT_OUT].getVoltage()==0);
    }
    set(v.inputs[Vastish::BUS_A_IN],20,true,true); v.process(a);
    set(v.inputs[Vastish::BUS_A_IN],30,true,false); v.params[PITCH_ADJ].setValue(66);
    for(int t=0;t<100;++t) { v.process(a); assert(word(v.outputs[Vastish::BUS_OUT])==20); }
    set(v.inputs[Vastish::BUS_A_IN],30,true,true); v.process(a); assert(word(v.outputs[Vastish::BUS_OUT])==158);
    init(v); v.params[BUS_OUT_MODE].setValue(2); v.params[BUS_OUT_SOURCE].setValue(SRC_FUN1);
    v.params[FUN1_A].setValue(SRC_LANE_A); v.params[FUN1_B].setValue(SRC_LANE_B); v.params[FUN1_EQ].setValue(6);
    for(int x=0;x<256;++x) for(int y=0;y<256;++y) {
        set(v.inputs[Vastish::BUS_A_IN],x); set(v.inputs[Vastish::BUS_B_IN],y); v.process(a);
        assert(word(v.outputs[Vastish::BUS_OUT])==(x^y));
    }
    // The sleeping lane's DSP tap must actually advance when explicitly selected.
    v.params[BUS_OUT_SOURCE].setValue(BUS_SOURCE_DSP); v.params[PITCH_S1].setValue(SRC_LANE_A); v.params[PITCH_D1].setValue(6);
    set(v.inputs[Vastish::BUS_A_IN],32); v.process(a); assert(word(v.outputs[Vastish::BUS_OUT])==32);
    v.params[BUS_OUT_MODE].setValue(1); v.params[BUS_OUT_SOURCE].setValue(SRC_BUS_B);
    set(v.inputs[Vastish::BUS_A_IN],0); set(v.inputs[Vastish::BUS_B_IN],45,true,true); v.process(a);
    assert(v.outputs[Vastish::BUS_OUT].getChannels()==8 && word(v.outputs[Vastish::BUS_OUT])==45);
    v.inputs[Vastish::BUS_A_IN].channels=0; set(v.inputs[Vastish::BUS_B_IN],55,true,false); v.process(a);
    set(v.inputs[Vastish::BUS_B_IN],55,true,true); v.process(a);
    assert(v.outputs[Vastish::BUS_OUT].getChannels()==9 && word(v.outputs[Vastish::BUS_OUT])==55);
    v.params[BUS_OUT_MODE].setValue(0); v.process(a);
    assert(v.outputs[Vastish::BUS_OUT].getChannels()==8 && word(v.outputs[Vastish::BUS_OUT])==128);
    v.params[BUS_OUT_MODE].setValue(2); v.params[BUS_OUT_SOURCE].setValue(SRC_FUN1); v.params[FUN1_BIT].setValue(1);
    auto* patch=v.toJson(); Vastish restored; restored.model=modelVastish; restored.fromJson(patch); json_decref(patch);
    assert(restored.params[BUS_OUT_MODE].getValue()==2 && restored.params[FUN1_BIT].getValue()==1);
    v.requestedProgram=5; v.process(a);
    assert(v.params[BUS_OUT_MODE].getValue()==2 && v.params[BUS_OUT_SOURCE].getValue()==SRC_FUN1 && v.params[FUN1_BIT].getValue()==0);
    // A genuinely old Rack patch has no appended parameters or cables.
    auto* old=v.toJson(); auto* parameters=json_object_get(old,"params");
    while(json_array_size(parameters)>80) json_array_remove(parameters,80);
    Vastish fresh; fresh.model=modelVastish; fresh.fromJson(old); json_decref(old);
    for(int i=0;i<80;++i) assert(fresh.params[i].getValue()==v.params[i].getValue());
    assert(fresh.params[BUS_OUT_MODE].getValue()==1 && fresh.params[BUS_OUT_SOURCE].getValue()==BUS_SOURCE_DSP && fresh.params[FUN1_BIT].getValue()==0);
}
static void chain(std::shared_ptr<const dspemu::lookup::Library> library,float sr) {
    ByteConverter c; ClockedRegister r; Vastish v; ByteLookup l(library); auto o=std::make_unique<OtisSampler>();
    outputs(c); outputs(r); init(v); outputs(l); outputs(*o);
    r.params[ClockedRegister::CLOCK_MODE].setValue(2); r.state.clear(128);
    l.params[ByteLookup::FAMILY].setValue(dspemu::lookup::Curve); l.params[ByteLookup::SHAPE].setValue(32);
    std::vector<int16_t> pcm(8*256); for(size_t i=0;i<pcm.size();++i) pcm[i]=int16_t((i/256)*2000);
    o->engine.install(pcm,otis::captureRate(5)); o->engine.setHostRate(sr); o->smoothFrameBus=false;
    o->params[OtisSampler::WINDOW_MODE].setValue(2); o->params[OtisSampler::SIZE].setValue(8); o->params[OtisSampler::LOOP].setValue(1);
    c.inputs[ByteConverter::SIGNAL_IN].channels=c.inputs[ByteConverter::CLOCK_IN].channels=1;
    auto a=args(sr); std::mt19937 random(1989); int checked=0;
    for(int t=0;t<32*258;++t) {
        copy(c.outputs[ByteConverter::BUS_OUT],r.inputs[ClockedRegister::BUS_IN]);
        copy(r.outputs[ClockedRegister::BUS_OUT],v.inputs[Vastish::BUS_A_IN]);
        copy(v.outputs[Vastish::BUS_OUT],l.inputs[ByteLookup::BUS_IN]);
        copy(l.outputs[ByteLookup::BUS_OUT],c.inputs[ByteConverter::BUS_IN]);
        copy(l.outputs[ByteLookup::BUS_OUT],o->inputs[OtisSampler::FRAME_BUS_IN]);
        int n=std::max(0,std::min(255,t/32-1));
        c.inputs[ByteConverter::SIGNAL_IN].setVoltage(dspemu::bus::toVolts(n));
        c.inputs[ByteConverter::CLOCK_IN].setVoltage(t>=32 && t%32==0 ? 10 : 0);
        o->params[OtisSampler::PLAY].setValue(t==16 ? 1 : 0);
        Module* modules[]={&c,&r,&v,&l,o.get()}; std::shuffle(std::begin(modules),std::end(modules),random);
        for(auto* module:modules) module->process(a);
        if(t>=36 && t/32<=256 && t%32==4) {
            assert(c.outputs[ByteConverter::SIGNAL_OUT].getVoltage()==dspemu::bus::toVolts(255-n));
            assert(o->engine.windowFor(0).begin/256==size_t(std::lround((255-n)*7./255)));
            ++checked;
        }
        ++a.frame;
    }
    assert(checked==256 && v.engine.faults==0);
}
static void example(std::shared_ptr<const dspemu::lookup::Library> library) {
    auto* patch=json_load_file("../examples/byte-bus-io.vcv",JSON_REJECT_DUPLICATES,nullptr); assert(patch);
    Vastish source,processor; ByteConverter c; ClockedRegister r; ByteLookup l(library); auto o=std::make_unique<OtisSampler>();
    source.model=processor.model=modelVastish; c.model=modelConverter; r.model=modelClockedRegister;
    l.model=modelLookup; o->model=modelOtisSampler;
    Module* modules[]={nullptr,&source,&c,&r,&processor,&l,o.get()};
    auto* saved=json_object_get(patch,"modules"); assert(json_array_size(saved)==7);
    for(int i=1;i<=6;++i) { modules[i]->fromJson(json_array_get(saved,i-1)); outputs(*modules[i]); }
    auto* cables=json_object_get(patch,"cables");
    auto a=args(); std::set<size_t> frames; double energy=0;
    for(int t=0;t<120000;++t) {
        for(size_t k=0;k<json_array_size(cables);++k) {
            auto* cable=json_array_get(cables,k);
            int from=json_integer_value(json_object_get(cable,"outputModuleId")),to=json_integer_value(json_object_get(cable,"inputModuleId"));
            int out=json_integer_value(json_object_get(cable,"outputId")),in=json_integer_value(json_object_get(cable,"inputId"));
            if(to<7) copy(modules[from]->outputs[out],modules[to]->inputs[in]);
        }
        o->params[OtisSampler::PLAY].setValue(t==16 ? 1 : 0);
        for(int i=1;i<=6;++i) modules[i]->process(a);
        frames.insert(o->engine.windowFor(0).begin/256);
        auto left=o->outputs[0].getVoltage(),right=o->outputs[1].getVoltage();
        assert(std::isfinite(left) && std::isfinite(right)); energy+=left*left+right*right; ++a.frame;
    }
    assert(o->engine.sample.stereo && frames.size()>=3 && energy>1);
    json_decref(patch);
}
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin bytePlugin; bytePlugin.slug="ByteBusLab"; bytePlugin.version="2.2.0";
    bytePlugin.addModel(modelConverter); bytePlugin.addModel(modelClockedRegister); bytePlugin.addModel(modelLookup);
    Plugin vastPlugin; vastPlugin.slug="VastishLab"; vastPlugin.version="2.5.0"; vastPlugin.addModel(modelVastish);
    Plugin otisPlugin; otisPlugin.slug="OtisLab"; otisPlugin.version="2.8.0"; otisPlugin.addModel(modelOtisSampler);
    rack::plugin::plugins={&bytePlugin,&vastPlugin,&otisPlugin};
    converter(); testVastish(); auto library=std::make_shared<dspemu::lookup::Library>(dspemu::lookup::Library::load("../res/lookup-v1.bin"));
    for(float sr:{44100.f,48000.f,96000.f}) chain(library,sr);
    example(library);
    rack::plugin::plugins.clear();
    std::cout<<"PASS: real Converter/Vastish ports, exhaustive two-byte XOR, gate-free DSP lanes, clock policies, persistence/program routing and delayed randomized Converter/Register/Vastish/Lookup/OTIS chain at 44.1/48/96 kHz\n";
}
