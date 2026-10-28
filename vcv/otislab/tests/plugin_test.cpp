// Loads the actual compiled VCV plugin and exercises its Rack ABI without a GUI.
#include <rack.hpp>
#include "../src/engine/Wav.hpp"
#include "../../shared/SampleShare.hpp"
#include <dlfcn.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
int main() {
    using namespace rack;
    void* handle=dlopen("./plugin.so",RTLD_NOW|RTLD_LOCAL);
    if(!handle) { std::cerr<<dlerror()<<"\n"; return 1; }
    auto initialize=reinterpret_cast<void(*)(Plugin*)>(dlsym(handle,"init")); assert(initialize);
    Plugin plugin; plugin.slug="OtisLab"; plugin.version="2.1.1"; initialize(&plugin);
    assert(plugin.models.size()==3 && (*std::next(plugin.models.begin()))->slug=="OtisKit" && plugin.models.back()->slug=="OtisCapture"); // 2.10, 2.11
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
    // The compiled plugin exposes its finished sample to a right-hand neighbour.
    {
        void* p=a->rightExpander.producerMessage; assert(p==a->rightExpander.consumerMessage && dspemu::validShare(p));
        auto* share=static_cast<const dspemu::SampleShareV1*>(p);
        uint32_t rev=share->revision(share->context); assert(!(rev&1));
        double rate=0; uint32_t got=0;
        size_t n=share->copy(share->context,nullptr,0,&rate,&got); assert(n==wav.pcm.size() && rate==wav.rate && got==rev);
        std::vector<int16_t> pcm(n); assert(share->copy(share->context,pcm.data(),n,&rate,&got)==n && pcm==wav.pcm);
        std::vector<int16_t> part(10); assert(share->copy(share->context,part.data(),10,&rate,&got)==n); // capacity respected
        assert(std::equal(part.begin(),part.end(),wav.pcm.begin()));
        char name[8]; share->name(share->context,name,sizeof name); assert(std::strlen(name)<sizeof name); // always terminated
        // Replacing the sample moves the revision on.
        std::vector<int16_t> other(777,123);
        json_t* d=json_object(); json_object_set_new(d,"pcm16le",json_string(otis::encodePcm(other.data(),other.size()).c_str()));
        json_object_set_new(d,"rate",json_real(22050)); json_object_set_new(d,"name",json_string("other.wav"));
        a->dataFromJson(d); json_decref(d);
        uint32_t rev2=share->revision(share->context); assert(rev2!=rev && !(rev2&1));
        assert(share->copy(share->context,nullptr,0,&rate,&got)==777 && rate==22050);
        char full[64]; share->name(share->context,full,sizeof full); assert(std::string(full)=="other.wav");
        // Restore the original so the rest of the test is unchanged.
        json_t* back=json_object(); json_object_set_new(back,"pcm16le",json_string(otis::encodePcm(wav.pcm.data(),wav.pcm.size()).c_str()));
        json_object_set_new(back,"rate",json_real(wav.rate)); json_object_set_new(back,"filterMode",json_integer(2));
        a->dataFromJson(back); json_decref(back);
    }
    // CV recording starts on gate high, finishes on low, and survives patch save.
    a->inputs[3].channels=1; a->inputs[0].channels=1; a->inputs[3].setVoltage(10); a->inputs[0].setVoltage(1);
    for(int i=0;i<4800;i++) a->process(args);
    a->inputs[3].setVoltage(0); a->process(args);
    auto recorded=a->dataToJson(); auto pcm=otis::decodePcm(json_string_value(json_object_get(recorded,"pcm16le")));
    assert(std::abs(double(pcm.size())-otis::captureRate(5)*.1)<2);
    assert(std::string(json_string_value(json_object_get(recorded,"name")))=="LIVE RECORDING"); json_decref(recorded);
    // Restore a complete preset when changing settings: absent PCM means an empty sample.
    auto gated=a->dataToJson(); json_object_set_new(gated,"gated",json_true()); a->dataFromJson(gated); json_decref(gated);
    a->inputs[2].channels=1; a->inputs[1].channels=1;
    a->inputs[2].setVoltage(0); a->inputs[1].setVoltage(1); a->process(args);
    a->inputs[2].setVoltage(10); a->process(args); assert(a->lights[0].getBrightness()>0);
    a->inputs[2].setVoltage(0); a->process(args);
    assert(a->lights[0].getBrightness()>0); // default declick fades instead of cutting
    for(int i=0;i<200;i++) a->process(args);
    assert(a->lights[0].getBrightness()==0);
    for(int i=0;i<200;i++) a->process(args); // also drain the output converter
    assert(std::abs(a->outputs[0].getVoltage())<1e-6f);
    auto hardCut=a->dataToJson(); json_object_set_new(hardCut,"gated",json_true());
    json_object_set_new(hardCut,"declick",json_false()); a->dataFromJson(hardCut); json_decref(hardCut);
    a->inputs[2].setVoltage(10); a->process(args); assert(a->lights[0].getBrightness()>0);
    a->inputs[2].setVoltage(0); a->process(args); assert(a->lights[0].getBrightness()==0);
    a.reset(); b.reset();
    // Otis Kit from the compiled plugin: a saved one-slot bank restores and plays on gate channel 1.
    {
        auto kitModel=*std::next(plugin.models.begin());
        auto k=std::unique_ptr<engine::Module>(kitModel->createModule()); k->model=kitModel; k->onSampleRateChange(rate);
        json_t* slot=json_object(); json_object_set_new(slot,"name",json_string("bright-demo.wav"));
        json_object_set_new(slot,"rate",json_real(wav.rate)); json_object_set_new(slot,"pcm16le",json_string(otis::encodePcm(wav.pcm.data(),wav.pcm.size()).c_str()));
        json_t* slots=json_array(); json_array_append_new(slots,slot); json_t* kit=json_object(); json_object_set_new(kit,"slots",slots);
        k->dataFromJson(kit); json_decref(kit);
        k->outputs[0].channels=1; k->inputs[0].channels=1; k->process(args); k->inputs[0].setVoltage(10);
        double kitEnergy=0; for(int i=0;i<24000;i++) { k->process(args); float x=k->outputs[0].getVoltage(); assert(std::isfinite(x)); kitEnergy+=x*x; }
        assert(kitEnergy>1);
    }
    std::cout<<"PASS: plugin.so dynamic load, Rack module creation, patch sample round-trip, deterministic dual-mono playback, CV record gate, faded and hard gate release, Otis Kit bank restore and playback\n";
    // Plugin destructor owns the models. Keep the library loaded through its destruction.
}
