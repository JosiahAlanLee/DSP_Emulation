// Otis Kit through the real Rack module (docs/KIT.md): which slot a hit plays, per-hit latching,
// folder loading, and a saved bank that plays back bit-identically.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/OtisKit.cpp"
#include "../src/engine/Wav.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
Plugin* pluginInstance=nullptr;
static Module::ProcessArgs args(float sr=48000) { Module::ProcessArgs a{}; a.sampleRate=sr; a.sampleTime=1/sr; return a; }
static std::vector<int16_t> noise(size_t n,unsigned seed) {
    std::mt19937 rng(seed); std::vector<int16_t> v(n); for(auto& x:v) x=int16_t(int(rng()%30001)-15000); return v;
}
static std::unique_ptr<OtisKit> makeKit(int filled=8) {
    auto m=std::make_unique<OtisKit>(); m->model=modelOtisKit; m->outputs[OtisKit::OUT].channels=1;
    for(int s=0;s<filled;++s) { m->slots[size_t(s)].pcm=noise(4000+500*size_t(s),unsigned(s)+1); m->slots[size_t(s)].name=string::f("s%d.wav",s); }
    m->install();
    return m;
}
static std::vector<int> running(OtisKit& m) { std::vector<int> r; for(int v=0;v<16;++v) if(m.engine.voices[size_t(v)].running) r.push_back(v); return r; }
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug="OtisLab"; plugin.version="2.10.0"; plugin.addModel(modelOtisKit);
    rack::plugin::plugins.push_back(&plugin);
    auto a=args();
    {   // Ports, and an empty bank stays silent whatever arrives.
        auto m=makeKit(0);
        assert(m->params.size()==48 && m->inputs.size()==4 && m->outputs.size()==1 && m->lights.size()==8);
        m->inputs[OtisKit::GATE_IN].channels=8;
        for(int n=0;n<200;++n) { for(int c=0;c<8;++c) m->inputs[OtisKit::GATE_IN].setVoltage(n%20<10 ? 10.f : 0.f,c); m->process(a); assert(m->outputs[OtisKit::OUT].getVoltage()==0); }
        assert(running(*m).empty());
    }
    {   // SLOT unpatched: gate channel n plays slot n; an empty slot's channel does nothing.
        auto m=makeKit(6); m->inputs[OtisKit::GATE_IN].channels=8; m->process(a);
        for(int c:{2,5,7}) m->inputs[OtisKit::GATE_IN].setVoltage(10,c);
        m->process(a); assert((running(*m)==std::vector<int>{2,5}));                       // slot 8 is empty
        assert(m->lights[OtisKit::HIT_LIGHT+2].getBrightness()>0 && m->lights[OtisKit::HIT_LIGHT+7].getBrightness()==0);
        double energy=0; for(int n=0;n<4800;++n) { m->process(a); energy+=std::pow(m->outputs[OtisKit::OUT].getVoltage(),2); }
        assert(energy>1);
    }
    {   // SLOT patched: bits 0-2 choose the slot for a mono gate, on the same sample; a clocked SLOT uses its latched word.
        auto m=makeKit(); m->inputs[OtisKit::GATE_IN].channels=1;
        auto bus=[&](uint8_t w,bool clocked=false,bool clk=false) { auto& p=m->inputs[OtisKit::SLOT_IN]; p.channels=clocked ? 9 : 8; dspemu::bus::Writer{}.write(p,w,clocked,clk); };
        bus(0x0d); m->process(a);
        m->inputs[OtisKit::GATE_IN].setVoltage(10); m->process(a); assert((running(*m)==std::vector<int>{5}));
        m->inputs[OtisKit::GATE_IN].setVoltage(0); bus(0x0a,true,true); m->process(a);           // latch 2
        bus(0x06,true,false); m->process(a);                                                        // raw 6, latched 2
        m->inputs[OtisKit::GATE_IN].setVoltage(10); m->process(a); assert(m->engine.voices[2].running);
    }
    {   // PITCH and LEVEL come from the hit's channel and are latched with it; TUNE, START and REV apply.
        auto m=makeKit(); m->inputs[OtisKit::GATE_IN].channels=m->inputs[OtisKit::PITCH_IN].channels=m->inputs[OtisKit::LEVEL_IN].channels=4;
        m->params[OtisKit::TUNE+3].setValue(12); m->params[OtisKit::REV+3].setValue(1); m->params[OtisKit::START+3].setValue(.5f);
        m->inputs[OtisKit::PITCH_IN].setVoltage(-.5f,3); m->inputs[OtisKit::LEVEL_IN].setVoltage(5,3); m->process(a);
        m->inputs[OtisKit::GATE_IN].setVoltage(10,3); m->process(a);
        assert(std::abs(m->engine.controls.pitch[3]-(1.f-.5f))<1e-6f && std::abs(m->engine.controls.level[3]-.5f)<1e-6f);
        assert(m->engine.voices[3].reverse);
        auto w=m->engine.windowFor(3); assert(w.begin==size_t(m->layout.begin[3])+size_t(.5f*(m->layout.end[3]-m->layout.begin[3]-1)));
        m->inputs[OtisKit::PITCH_IN].setVoltage(3,3); m->inputs[OtisKit::LEVEL_IN].setVoltage(10,3);
        for(int n=0;n<100;++n) m->process(a);
        assert(std::abs(m->engine.controls.pitch[3]-.5f)<1e-6f && std::abs(m->engine.controls.level[3]-.5f)<1e-6f); // latched
    }
    {   // A saved bank restores and plays back bit-identically; chip clock and drop-sample are saved.
        auto m=makeKit(); m->engineMode=1; m->dropSample=true; m->params[OtisKit::DECAY+1].setValue(.3f);
        m->slots[1].rate=22050; m->install();
        json_t* saved=m->toJson();
        auto r=std::make_unique<OtisKit>(); r->model=modelOtisKit; r->outputs[OtisKit::OUT].channels=1; r->fromJson(saved); json_decref(saved);
        assert(r->engineMode==1 && r->dropSample && r->slots[1].rate==22050 && r->slots[4].name=="s4.wav" && r->slots[6].pcm==m->slots[6].pcm);
        for(auto* k:{m.get(),r.get()}) { k->inputs[OtisKit::GATE_IN].channels=8; k->process(a); for(int c:{0,1,6}) k->inputs[OtisKit::GATE_IN].setVoltage(10,c); }
        for(int n=0;n<24000;++n) { m->process(a); r->process(a); assert(m->outputs[OtisKit::OUT].getVoltage()==r->outputs[OtisKit::OUT].getVoltage()); }
        // An empty slot saves as null; a bad saved rate leaves the slot empty.
        m->clearSlot(2); saved=m->dataToJson(); assert(json_is_null(json_array_get(json_object_get(saved,"slots"),2)));
        json_object_set_new(json_array_get(json_object_get(saved,"slots"),3),"rate",json_real(-1));
        json_object_set_new(json_array_get(json_object_get(saved,"slots"),5),"pcm16le",json_string("not base64!"));   // damaged
        r->dataFromJson(saved); json_decref(saved);
        assert(r->slots[2].pcm.empty() && r->slots[3].pcm.empty() && r->slots[5].pcm.empty() && !r->layout.playable(3) && r->layout.playable(4));
        assert(r->layout.playable(6) && !r->lastError.empty());
    }
    {   // A folder's WAVs fill slots 1-n in name order, as mono at their own rates.
        auto dir=std::filesystem::temp_directory_path()/"otis-kit-folder-test"; std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
        auto pcm=noise(3000,9);
        otis::writeWav((dir/"b-snare.wav").string(),pcm.data(),pcm.size(),32000);
        otis::writeWav((dir/"a-kick.wav").string(),pcm.data(),1000,44100);
        otis::writeWav((dir/"c-hat.wav").string(),pcm.data(),500,48000,pcm.data());          // stereo: averaged
        std::ofstream((dir/"notes.txt").string())<<"not audio";
        auto m=makeKit(0); m->loadFolder(dir.string());
        assert(m->slots[0].name=="a-kick.wav" && m->slots[1].name=="b-snare.wav" && m->slots[2].name=="c-hat.wav" && m->slots[3].pcm.empty());
        assert(m->slots[1].rate==32000 && m->slots[2].pcm.size()==500 && m->lastError.empty());
        assert(std::abs(m->layout.octaves[1]-std::log2(32000/44100.))<1e-12 && m->layout.playable(2));
        std::filesystem::remove_all(dir);
    }
    {   // Initialize empties the bank and restores the controls.
        auto m=makeKit(); m->engineMode=2; m->params[OtisKit::TUNE].setValue(5);
        m->onReset(Module::ResetEvent{});
        assert(m->engineMode==0 && m->params[OtisKit::TUNE].getValue()==0 && m->layout.frames==0 && m->slots[0].pcm.empty());
    }
    rack::plugin::plugins.clear();
    std::cout<<"PASS: Otis Kit ports, silent empty bank, gate channel to slot, SLOT bits on the same sample and latched, per-hit PITCH/LEVEL latching with TUNE/START/REV, bit-identical bank save/restore with clock and degradation, folder loading, Initialize\n";
}
