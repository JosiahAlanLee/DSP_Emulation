// Load the actual plugin binary with a minimal Rack host context.
#include <engine/Engine.hpp>
#include <plugin.hpp>
#include <context.hpp>
#include "../src/engine/Wav.hpp"
#include "../../shared/ByteBus.hpp"
#include <dlfcn.h>
#include <cassert>
#include <chrono>
#include <thread>
#include <memory>
#include <iostream>
int main() {
    using namespace rack; using rack::plugin::Plugin;
    Context context; context.engine=new engine::Engine; contextSet(&context);
    void* handle=dlopen("./plugin.so",RTLD_NOW|RTLD_LOCAL); if(!handle) { std::cerr<<dlerror()<<"\n"; return 1; }
    auto init=reinterpret_cast<void(*)(Plugin*)>(dlsym(handle,"init")); assert(init);
    Plugin plugin; plugin.slug="OtisLab"; plugin.version="2.3.0"; init(&plugin); rack::plugin::plugins.push_back(&plugin);
    auto model=plugin.models.front(); auto m=std::unique_ptr<engine::Module>(model->createModule()); m->model=model;
    assert(m->params.size()==20 && m->inputs.size()==18 && m->outputs.size()==4); // FRAME BUS, then PATCH, appended after AUDIO R
    for(auto& o:m->outputs) o.channels=1;
    std::vector<int16_t> pcm(1024);
    for(size_t i=0;i<pcm.size();++i) pcm[i]=int16_t((i/256+1)*4096);
    auto data=json_object(); json_object_set_new(data,"pcm16le",json_string(otis::encodePcm(pcm.data(),pcm.size()).c_str()));
    json_object_set_new(data,"rate",json_real(otis::captureRate(5))); json_object_set_new(data,"name",json_string("FRAMES"));
    m->dataFromJson(data); json_decref(data);
    engine::Module::ProcessArgs args; args.sampleRate=48000; args.sampleTime=1.f/48000;
    engine::Module::SampleRateChangeEvent rate; rate.sampleRate=48000; rate.sampleTime=args.sampleTime; m->onSampleRateChange(rate);
    m->params[10].setValue(1); m->params[15].setValue(8); m->params[16].setValue(2);
    m->inputs[1].channels=2; m->inputs[2].channels=2; m->inputs[11].channels=2;
    m->inputs[11].setVoltage(0,0); m->inputs[11].setVoltage(10,1);
    m->process(args); m->inputs[2].setVoltage(10,0); m->inputs[2].setVoltage(10,1);
    for(int i=0;i<4800;++i) m->process(args);
    // Frames 0 and 3 contain constants 4096 and 16384, summed at DSP * 5 V.
    assert(std::abs(m->outputs[0].getVoltage()-3.125)<.003);
    m->inputs[11].setVoltage(5,0); // round(0.5 * 3) selects frame 2
    for(int i=0;i<4800;++i) m->process(args);
    assert(std::abs(m->outputs[0].getVoltage()-4.375)<.003);
    // The packaged binary's appended bus overrides both voice CVs and holds between edges.
    m->inputs[16].channels=9;
    dspemu::bus::Writer{}.write(m->inputs[16],0,true,true);
    for(int i=0;i<4800;++i) m->process(args);
    assert(std::abs(m->outputs[0].getVoltage()-1.25)<.003);
    dspemu::bus::Writer{}.write(m->inputs[16],255,true,false);
    for(int i=0;i<4800;++i) m->process(args);
    assert(std::abs(m->outputs[0].getVoltage()-1.25)<.003);
    dspemu::bus::Writer{}.write(m->inputs[16],255,true,true);
    for(int i=0;i<4800;++i) m->process(args);
    assert(std::abs(m->outputs[0].getVoltage()-5)<.003);
    m->inputs[16].channels=0;
    for(int i=0;i<4800;++i) m->process(args);
    assert(std::abs(m->outputs[0].getVoltage()-4.375)<.003);
    // Rework a nonconstant capture and wait for worker + publication without blocking processing.
    for(size_t i=0;i<pcm.size();++i) pcm[i]=int16_t(16000*std::sin(i*.09));
    data=json_object(); json_object_set_new(data,"pcm16le",json_string(otis::encodePcm(pcm.data(),pcm.size()).c_str()));
    json_object_set_new(data,"rate",json_real(otis::captureRate(5))); m->dataFromJson(data); json_decref(data);
    m->params[18].setValue(1); m->inputs[13].channels=1; m->inputs[13].setVoltage(10);
    m->process(args); assert(m->lights[3].getBrightness()>0);
    for(int pass=0;pass<2000 && m->lights[3].getBrightness()>0;++pass) {
        for(int i=0;i<128;++i) { m->process(args); assert(std::isfinite(m->outputs[0].getVoltage())); }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(m->lights[3].getBrightness()==0);
    auto saved=m->dataToJson(); auto altered=otis::decodePcm(json_string_value(json_object_get(saved,"pcm16le"))); json_decref(saved);
    assert(altered!=pcm);
    // Voice 1 selects frame 2, so only [512,768) may change.
    for(size_t i=0;i<pcm.size();++i) if(i<512 || i>=768) assert(altered[i]==pcm[i]);
    auto patch=m->toJson(); auto restored=std::unique_ptr<engine::Module>(model->createModule()); restored->model=model;
    restored->fromJson(patch); json_decref(patch);
    for(size_t i=0;i<m->params.size();++i) assert(restored->params[i].getValue()==m->params[i].getValue());
    auto restoredData=restored->dataToJson();
    auto again=otis::decodePcm(json_string_value(json_object_get(restoredData,"pcm16le"))); json_decref(restoredData);
    assert(again==altered);
    rack::plugin::plugins.clear();
    std::cout<<"PASS: plugin poly frame CV, half-open frame levels, triggered worker rework, selected-region publication, complete Rack parameter/PCM save and restore\n";
}
