// Loads the actual compiled VCV plugin and exercises its Rack ABI without a GUI.
#include <rack.hpp>
#include "../src/engine/Wav.hpp"
#include <dlfcn.h>
#include <cassert>
#include <iostream>
#include <memory>
int main() {
    using namespace rack;
    void* handle=dlopen("./plugin.so",RTLD_NOW|RTLD_LOCAL);
    if(!handle) { std::cerr<<dlerror()<<"\n"; return 1; }
    auto initialize=reinterpret_cast<void(*)(Plugin*)>(dlsym(handle,"init")); assert(initialize);
    Plugin plugin; plugin.slug="OtisLab"; plugin.version="2.0.0"; initialize(&plugin);
    assert(plugin.models.size()==1);
    auto model=plugin.models.front();
    auto a=std::unique_ptr<engine::Module>(model->createModule());
    auto b=std::unique_ptr<engine::Module>(model->createModule());
    a->model=b->model=model;
    auto wav=otis::loadWav("examples/bright-demo.wav");
    json_t* data=json_object(); json_object_set_new(data,"pcm16le",json_string(otis::encodePcm(wav.pcm.data(),wav.pcm.size()).c_str()));
    json_object_set_new(data,"rate",json_real(wav.rate)); json_object_set_new(data,"filterMode",json_integer(2));
    a->dataFromJson(data); json_decref(data);
    auto saved=a->dataToJson(); b->dataFromJson(saved);
    auto savedAgain=b->dataToJson(); assert(json_equal(saved,savedAgain)); json_decref(savedAgain); json_decref(saved);
    engine::Module::SampleRateChangeEvent rate; rate.sampleRate=48000; rate.sampleTime=1.f/48000;
    a->onSampleRateChange(rate); b->onSampleRateChange(rate);
    engine::Module::ProcessArgs args; args.sampleRate=48000; args.sampleTime=1.f/48000;
    // Establish low trigger state before simulating a button press.
    a->process(args); b->process(args);
    a->params[10].setValue(1); b->params[10].setValue(1); // forward loop
    a->params[8].setValue(1); b->params[8].setValue(1); // play button
    double energy=0;
    for(int i=0;i<48000;i++) {
        args.frame=i; a->process(args); b->process(args);
        float x=a->outputs[0].getVoltage(); assert(std::isfinite(x));
        assert(x==b->outputs[0].getVoltage()); assert(x==a->outputs[1].getVoltage());
        energy+=x*x; if(i==0) { a->params[8].setValue(0); b->params[8].setValue(0); }
    }
    assert(energy>1);
    // CV recording starts on gate high, finishes on low, and survives patch save.
    a->inputs[3].channels=1; a->inputs[0].channels=1; a->inputs[3].setVoltage(10); a->inputs[0].setVoltage(1);
    for(int i=0;i<4800;i++) a->process(args);
    a->inputs[3].setVoltage(0); a->process(args);
    auto recorded=a->dataToJson(); auto pcm=otis::decodePcm(json_string_value(json_object_get(recorded,"pcm16le")));
    assert(std::abs(double(pcm.size())-otis::captureRate(5)*.1)<2);
    assert(std::string(json_string_value(json_object_get(recorded,"name")))=="LIVE RECORDING"); json_decref(recorded);
    auto gated=json_object(); json_object_set_new(gated,"gated",json_true()); a->dataFromJson(gated); json_decref(gated);
    a->inputs[2].channels=1; a->inputs[1].channels=1;
    a->inputs[2].setVoltage(0); a->inputs[1].setVoltage(1); a->process(args);
    a->inputs[2].setVoltage(10); a->process(args); assert(a->lights[0].getBrightness()>0);
    a->inputs[2].setVoltage(0); a->process(args); assert(a->lights[0].getBrightness()==0);
    a.reset(); b.reset();
    std::cout<<"PASS: plugin.so dynamic load, Rack module creation, patch sample round-trip, deterministic dual-mono playback, CV record gate\n";
    // Plugin destructor owns the models. Keep the library loaded through its destruction.
}
