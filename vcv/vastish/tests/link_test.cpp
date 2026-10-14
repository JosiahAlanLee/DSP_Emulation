// Cross-plugin link: the real Vastish module code reading a real OTIS module
// created from OtisLab's compiled plugin binary, as Rack would place them.
#include "../src/Vastish.cpp"
#include "../../otislab/src/engine/Wav.hpp"
#include <dlfcn.h>
#include <cassert>
#include <iostream>
#include <memory>
Plugin* pluginInstance=nullptr;
int main(int argc,char** argv) {
    const char* otisPath=argc>1 ? argv[1] : "../../otislab/plugin.so";
    void* handle=dlopen(otisPath,RTLD_NOW|RTLD_LOCAL);
    if(!handle) { std::cerr<<dlerror()<<"\n"; return 1; }
    auto init=reinterpret_cast<void(*)(rack::plugin::Plugin*)>(dlsym(handle,"init")); assert(init);
    rack::plugin::Plugin otisPlugin; otisPlugin.slug="OtisLab"; init(&otisPlugin);
    auto* model=otisPlugin.models.front(); model->plugin=&otisPlugin;
    auto otis=std::unique_ptr<engine::Module>(model->createModule()); otis->model=model;
    auto wav=otis::loadWav("../../otislab/examples/bright-demo.wav");
    json_t* d=json_object(); json_object_set_new(d,"pcm16le",json_string(otis::encodePcm(wav.pcm.data(),wav.pcm.size()).c_str()));
    json_object_set_new(d,"rate",json_real(wav.rate)); json_object_set_new(d,"name",json_string("bright-demo.wav"));
    otis->dataFromJson(d); json_decref(d);

    Vastish v; v.setFollowOtis(true);
    assert(!Vastish::findOtisShare(nullptr));
    // Wrong neighbour: same module but claiming another plugin slug is ignored.
    otisPlugin.slug="SomethingElse"; assert(!Vastish::findOtisShare(otis.get())); otisPlugin.slug="OtisLab";
    v.leftExpander.module=otis.get();
    auto* share=Vastish::findOtisShare(v.leftExpander.module); assert(share);
    v.followShare(share);
    assert(v.linkState==Vastish::LINK_CONNECTED && v.uiSample && v.uiSample->data.pcm==wav.pcm);
    assert(v.uiSample->data.rate==wav.rate && v.sampleLabel()=="OTIS: bright-demo.wav");
    // Vastish plays it through its own voice.
    Module::ProcessArgs args; args.sampleRate=48000; args.sampleTime=1.f/48000;
    for(auto& o:v.outputs) o.channels=1; v.inputs[Vastish::PITCH_IN].channels=v.inputs[Vastish::GATE_IN].channels=1;
    v.params[PITCH_WAVE].setValue(SAMPLE_WAVE); v.process(args); v.inputs[Vastish::GATE_IN].setVoltage(10);
    double energy=0; for(int i=0;i<24000;++i) { v.process(args); energy+=std::pow(v.outputs[0].getVoltage(),2); }
    assert(energy>1e-3);
    // A new OTIS sample arrives in Vastish on the next UI frame.
    std::vector<int16_t> other(2000); for(size_t i=0;i<other.size();++i) other[i]=int16_t(i*11);
    json_t* e=json_object(); json_object_set_new(e,"pcm16le",json_string(otis::encodePcm(other.data(),other.size()).c_str()));
    json_object_set_new(e,"rate",json_real(22050)); json_object_set_new(e,"name",json_string("take-2.wav"));
    otis->dataFromJson(e); json_decref(e);
    v.followShare(Vastish::findOtisShare(v.leftExpander.module));
    assert(v.uiSample->data.pcm==other && v.uiSample->data.rate==22050 && v.sampleLabel()=="OTIS: take-2.wav");
    // Moving OTIS away: link reports not found, sample stays.
    v.leftExpander.module=nullptr; v.followShare(Vastish::findOtisShare(v.leftExpander.module));
    assert(v.linkState==Vastish::LINK_NOT_FOUND && v.sampleLabel()=="OTIS: take-2.wav");
    v.collectSamples(); v.process(args); v.collectSamples();
    otis.reset();
    std::cout<<"PASS: Vastish follows a real OTIS module from the compiled OtisLab plugin, plays it, and tracks new samples\n";
}
