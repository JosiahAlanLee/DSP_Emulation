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

    // Stereo (OTIS 2.7.0+): a stereo OTIS sample arrives in Vastish with both channels. With the
    // version unset above, the link was mono, as with an older OTIS: it gets the mix.
    std::vector<int16_t> L(3000),R(3000);
    for(size_t i=0;i<L.size();++i) { L[i]=int16_t(9000*std::sin(i*.04)); R[i]=int16_t(-7000*std::sin(i*.013)); }
    json_t* st=json_object();
    json_object_set_new(st,"pcm16le",json_string(otis::encodePcm(L.data(),L.size()).c_str()));
    json_object_set_new(st,"pcm16le_r",json_string(otis::encodePcm(R.data(),R.size()).c_str()));
    json_object_set_new(st,"rate",json_real(44100)); json_object_set_new(st,"name",json_string("wide.wav"));
    otis->dataFromJson(st); json_decref(st);
    v.leftExpander.module=otis.get();
    assert(!Vastish::findOtisStereo(otis.get()));
    v.followShare(Vastish::findOtisShare(otis.get()),Vastish::findOtisStereo(otis.get()));
    assert(!v.uiSample->data.stereo() && v.uiSample->data.pcm.size()==L.size());
    for(size_t i=0;i<L.size();++i) assert(v.uiSample->data.pcm[i]==int16_t((int32_t(L[i])+R[i])>>1));
    otisPlugin.version="2.7.0";
    auto* ext=Vastish::findOtisStereo(otis.get()); assert(ext);
    Vastish w; w.setFollowOtis(true); w.leftExpander.module=otis.get();
    w.followShare(Vastish::findOtisShare(otis.get()),ext);
    assert(w.uiSample->data.stereo() && w.uiSample->data.pcm==L && w.uiSample->data.right==R);
    assert(w.sampleLabel()=="OTIS: wide.wav (stereo)" && w.sampleShortLabel()=="\xe2\x97\x80 ST wide");
    // It plays in stereo and saves both channels; a restore brings them back.
    for(auto& o:w.outputs) o.channels=1; w.inputs[Vastish::PITCH_IN].channels=w.inputs[Vastish::GATE_IN].channels=1;
    w.params[PITCH_WAVE].setValue(SAMPLE_WAVE); w.process(args); w.inputs[Vastish::GATE_IN].setVoltage(10);
    double diff=0,sum=0;
    for(int i=0;i<24000;++i) { w.process(args); float l=w.outputs[0].getVoltage(),r=w.outputs[1].getVoltage(); diff+=(l-r)*(l-r); sum+=l*l+r*r; }
    assert(sum>1e-3 && diff>.1*sum);
    json_t* saved=w.dataToJson();
    assert(json_string_value(json_object_get(json_object_get(saved,"sample"),"pcm16le_r")));
    Vastish back; back.dataFromJson(saved); json_decref(saved);
    assert(back.uiSample && back.uiSample->data.pcm==L && back.uiSample->data.right==R);
    // A mono OTIS sample through the stereo link stays mono.
    json_t* mono=json_object();
    json_object_set_new(mono,"pcm16le",json_string(otis::encodePcm(L.data(),L.size()).c_str()));
    json_object_set_new(mono,"rate",json_real(44100)); json_object_set_new(mono,"name",json_string("narrow.wav"));
    otis->dataFromJson(mono); json_decref(mono);
    w.followShare(Vastish::findOtisShare(otis.get()),ext);
    assert(!w.uiSample->data.stereo() && w.uiSample->data.pcm==L && w.sampleLabel()=="OTIS: narrow.wav");
    w.collectSamples(); w.process(args); w.collectSamples(); back.collectSamples();
    otis.reset();
    std::cout<<"PASS: Vastish follows a real OTIS module from the compiled OtisLab plugin, plays it, and tracks new samples; stereo from OTIS 2.7.0 (mix from older), stereo playback, save and restore\n";
}
