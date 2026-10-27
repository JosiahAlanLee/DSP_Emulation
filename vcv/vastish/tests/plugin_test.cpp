// Harness uses host internals to provide a Rack context; the plugin only uses rack.hpp.
#include <engine/Engine.hpp>
#include <plugin/Plugin.hpp>
#include <plugin/Model.hpp>
#include <context.hpp>
#include <plugin.hpp>
#include "../src/engine/Programs.hpp"
#include <dlfcn.h>
#include <cassert>
#include <iostream>
#include <memory>
int main() {
    using namespace rack;
    using rack::plugin::Plugin;
    Context context; context.engine=new engine::Engine; contextSet(&context);
    void* handle=dlopen("./plugin.so",RTLD_NOW|RTLD_LOCAL); if(!handle) { std::cerr<<dlerror()<<"\n"; return 1; }
    auto init=reinterpret_cast<void(*)(Plugin*)>(dlsym(handle,"init")); assert(init);
    Plugin plugin; plugin.slug="VastishLab"; plugin.version="2.0.0"; init(&plugin); assert(plugin.models.size()==1); rack::plugin::plugins.push_back(&plugin);
    auto model=plugin.models.front(); auto m=std::unique_ptr<engine::Module>(model->createModule()); m->model=model;
    for(auto& out:m->outputs) out.channels=1;
    assert(m->params.size()==86 && m->inputs.size()==11 && m->outputs.size()==12); // input 10: AUDIO IN
    engine::Module::SampleRateChangeEvent rate; rate.sampleRate=48000; rate.sampleTime=1.f/48000; m->onSampleRateChange(rate);
    engine::Module::ProcessArgs args; args.sampleRate=rate.sampleRate; args.sampleTime=rate.sampleTime;
    for(int i=0;i<100;++i) m->process(args);
    m->inputs[0].channels=8; m->inputs[1].channels=8; m->inputs[2].channels=1; m->inputs[3].channels=1;
    m->inputs[2].setVoltage(7.5); m->inputs[3].setVoltage(3);
    for(int c=0;c<8;++c) { m->inputs[0].setVoltage(float(c)*.25f,c); m->inputs[1].setVoltage(10,c); }
    double energy=0;
    for(int i=0;i<24000;++i) { args.frame=i; m->process(args); float v=m->outputs[0].getVoltage(); assert(std::isfinite(v)); energy+=v*v; assert(v==m->outputs[1].getVoltage()); }
    assert(energy>1); assert(m->outputs[2].getChannels()==8);
    for(int c=0;c<8;++c) assert(m->outputs[2].getVoltage(c)>1);
    // Disappearing input channels release their envelopes, preserving tails.
    m->inputs[0].channels=1; m->inputs[1].channels=1; m->inputs[1].setVoltage(0);
    for(int i=0;i<48000;++i) m->process(args);
    assert(std::abs(m->outputs[0].getVoltage())<1e-5f);
    // Rack's complete module serialization preserves edits and selector values.
    m->params[vastish::PAN_ADJ].setValue(.7f); m->params[vastish::FUN1_EQ].setValue(6);
    json_t* patch=m->toJson(); assert(patch);
    auto restored=std::unique_ptr<engine::Module>(model->createModule()); restored->model=model; restored->fromJson(patch); json_decref(patch);
    assert(restored->params[vastish::PAN_ADJ].getValue()==.7f); assert(restored->params[vastish::FUN1_EQ].getValue()==6);
    // A 2.5 patch has no audioInRange: it opens at ±5 V with its parameters and sound unchanged.
    {
        json_t* now=m->toJson(); json_t* old=json_deep_copy(now); json_decref(now);
        json_t* data=json_object_get(old,"data"); assert(data && json_object_get(data,"audioInRange"));
        json_object_del(data,"audioInRange");
        auto a=std::unique_ptr<engine::Module>(model->createModule()); a->model=model; a->fromJson(old); json_decref(old);
        for(size_t i=0;i<m->params.size();++i) assert(a->params[i].getValue()==m->params[i].getValue());
        json_t* saved=a->toJson(); assert(json_integer_value(json_object_get(json_object_get(saved,"data"),"audioInRange"))==5); json_decref(saved);
        a->onSampleRateChange(rate);
        auto b=std::unique_ptr<engine::Module>(model->createModule()); b->model=model; b->onSampleRateChange(rate);
        for(size_t i=0;i<m->params.size();++i) b->params[i].setValue(m->params[i].getValue());
        for(auto* x:{a.get(),b.get()}) { for(auto& out:x->outputs) out.channels=1; x->inputs[1].channels=1; x->inputs[1].setVoltage(10); }
        for(int i=0;i<4800;++i) { a->process(args); b->process(args); assert(a->outputs[0].getVoltage()==b->outputs[0].getVoltage()); }
    }
    // Rack's Initialize returns AUDIO IN to its ±5 V default.
    {
        auto x=std::unique_ptr<engine::Module>(model->createModule()); x->model=model;
        json_t* data=json_object(); json_object_set_new(data,"audioInRange",json_integer(10)); x->dataFromJson(data); json_decref(data);
        auto range=[&] { json_t* d=x->dataToJson(); auto v=json_integer_value(json_object_get(d,"audioInRange")); json_decref(d); return v; };
        assert(range()==10); x->onReset(engine::Module::ResetEvent{}); assert(range()==5);
        // ...and turns chain mode off.
        data=json_object(); json_object_set_new(data,"chainFromLeft",json_true()); x->dataFromJson(data); json_decref(data);
        auto chained=[&] { json_t* d=x->dataToJson(); bool v=json_is_true(json_object_get(d,"chainFromLeft")); json_decref(d); return v; };
        assert(chained()); x->onReset(engine::Module::ResetEvent{}); assert(!chained());
    }
    // Malformed external CV is contained at the module boundary.
    m->inputs[0].channels=1; m->inputs[1].channels=1; m->inputs[1].setVoltage(10); m->inputs[0].setVoltage(NAN); m->inputs[3].setVoltage(INFINITY);
    for(int i=0;i<1000;++i) { m->process(args); assert(std::isfinite(m->outputs[0].getVoltage())); }
    rack::plugin::plugins.clear();
    std::cout<<"PASS: compiled Rack plugin load, eight-channel pitch/gate, mono velocity/MOD broadcast, disconnect release, patch serialization, 2.5 patch without AUDIO IN range, Initialize range and chain, nonfinite CV containment\n";
}
