// Otis Capture with Otis Kit through the real Rack modules (docs/CAPTURE.md), with Rack's expander
// wiring and end-of-frame message swaps emulated. The drum-machine checks: slot isolation, an old take
// sounding on while its replacement lands, recorded timing, save/reload and memory limits.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/OtisKit.cpp"
#include "../src/OtisCapture.cpp"
#include <cassert>
#include <iostream>
#include <random>
Plugin* pluginInstance=nullptr;
static Module::ProcessArgs args(float sr=48000) { Module::ProcessArgs a{}; a.sampleRate=sr; a.sampleTime=1/sr; return a; }
static std::vector<int16_t> noise(size_t n,unsigned seed) {
    std::mt19937 rng(seed); std::vector<int16_t> v(n); for(auto& x:v) x=int16_t(int(rng()%30001)-15000); return v;
}
static std::unique_ptr<OtisKit> makeKit(size_t frames=20000) {
    auto k=std::make_unique<OtisKit>(); k->model=modelOtisKit; k->outputs[OtisKit::OUT].channels=1;
    for(int s=0;s<8;++s) { k->slots[size_t(s)].pcm=noise(frames,unsigned(s)+1); k->slots[size_t(s)].name=string::f("s%d",s); }
    k->install(false);
    k->inputs[OtisKit::GATE_IN].channels=8;
    return k;
}
static std::unique_ptr<OtisCapture> makeCapture() {
    auto c=std::make_unique<OtisCapture>(); c->model=modelOtisCapture; for(auto& o:c->outputs) o.channels=1; return c;
}
static void wire(OtisKit& k,OtisCapture& c) { k.rightExpander.module=&c; c.leftExpander.module=&k; }
// One engine frame: both modules, then Rack's end-of-frame message swaps.
static void frame(OtisKit& k,OtisCapture* c,const Module::ProcessArgs& a,bool captureFirst) {
    if(c && captureFirst) c->process(a);
    k.process(a);
    if(c && !captureFirst) c->process(a);
    for(Module* m:{static_cast<Module*>(&k),static_cast<Module*>(c)}) if(m) for(auto* e:{&m->leftExpander,&m->rightExpander})
        if(e->messageFlipRequested) { std::swap(e->producerMessage,e->consumerMessage); e->messageFlipRequested=false; }
}
static void hit(OtisKit& k,int slot,bool high) { k.inputs[OtisKit::GATE_IN].setVoltage(high ? 10.f : 0.f,slot); }
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug="OtisLab"; plugin.version="2.11.2"; plugin.addModel(modelOtisKit); plugin.addModel(modelOtisCapture);
    rack::plugin::plugins.push_back(&plugin);
    auto a=args();
    {   // Without a kit on the left, Capture says so and never records.
        auto c=makeCapture(); assert(c->params.size()==6 && c->inputs.size()==3 && c->outputs.size()==2);
        c->params[OtisCapture::ARM_BUTTON].setValue(1); c->process(a); c->params[OtisCapture::ARM_BUTTON].setValue(0);
        for(int n=0;n<100;++n) c->process(a);
        assert(!c->armed && !c->recording && !c->displayLinked.load());
    }
    // A take of one beat at 4 PPQN starts on the first CLOCK pulse after ARM and ends on the fourth
    // after that: exactly the samples between, recorded as they arrived, into a free part of memory.
    // Meanwhile a hit on the same slot, started before the take, plays its old take to the end exactly
    // as a kit without Capture does; the other slots are untouched; the next hit plays the new take.
    for(bool captureFirst:{true,false}) {
        auto k=makeKit(), ref=makeKit(); auto c=makeCapture(); wire(*k,*c);
        c->params[OtisCapture::SLOT].setValue(3); c->params[OtisCapture::LENGTH].setValue(2);   // 1 beat
        c->inputs[OtisCapture::CLOCK_IN].channels=c->inputs[OtisCapture::AUDIO_IN].channels=1;
        auto signal=[](int t) { return float(5*std::sin(t*.013)+.3*std::sin(t*.41)); };
        int t=0, start=-1, end=-1, dones=0; bool doneHigh=false; std::vector<float> sent;
        auto step=[&]() {
            c->inputs[OtisCapture::CLOCK_IN].setVoltage(t%100<3 ? 10.f : 0.f);
            c->inputs[OtisCapture::AUDIO_IN].setVoltage(signal(t)); sent.push_back(signal(t));
            frame(*k,c.get(),a,captureFirst); frame(*ref,nullptr,a,true);
            bool rec=c->outputs[OtisCapture::REC_OUT].getVoltage()>5;
            if(rec && start<0) start=t;
            if(!rec && start>=0 && end<0) end=t;
            bool d=c->outputs[OtisCapture::DONE_OUT].getVoltage()>5; dones+=d && !doneHigh; doneHigh=d;   // DONE is a 1 ms pulse
            assert(k->outputs[OtisKit::OUT].getVoltage()==ref->outputs[OtisKit::OUT].getVoltage());
            ++t;
        };
        for(auto* m:{k.get(),ref.get()}) { hit(*m,3,true); hit(*m,6,true); }
        for(int n=0;n<37;++n) step();
        c->params[OtisCapture::ARM_BUTTON].setValue(1); step(); c->params[OtisCapture::ARM_BUTTON].setValue(0);
        assert(c->armed);
        while(end<0) step();
        for(int n=0;n<5;++n) step();
        assert(start==100 && end==500 && dones==1);
        auto map=k->exchange.read(); auto take=map.take[3];
        assert(take.size()==400 && map.rate[3]==48000);
        for(int i=0;i<400;++i) assert(k->engine.sample.pcm[size_t(take.begin+i)]==otis::quantize(sent[size_t(start+i)]/5));
        // The old hit on slot 3 and the one on 6 played on identically (asserted every sample above).
        // A new hit on slot 3 plays the take: the same as a kit loaded with it.
        auto loaded=makeKit(); loaded->slots[3].pcm.assign(k->engine.sample.pcm.begin()+take.begin,k->engine.sample.pcm.begin()+take.end);
        loaded->slots[3].rate=48000; loaded->install(false);
        // Let the old hits finish, then start both chip clocks from the same phase.
        for(auto* m:{k.get(),loaded.get()}) { hit(*m,3,false); hit(*m,6,false); for(int n=0;n<40000;++n) m->process(a); }
        assert(!k->engine.playing() && !loaded->engine.playing());
        Module::SampleRateChangeEvent rate; rate.sampleRate=48000; rate.sampleTime=1/48000.f;
        k->onSampleRateChange(rate); loaded->onSampleRateChange(rate);
        double energy=0;
        for(int n=0;n<3000;++n) { hit(*k,3,true); hit(*loaded,3,true); k->process(a); loaded->process(a);
            assert(k->outputs[OtisKit::OUT].getVoltage()==loaded->outputs[OtisKit::OUT].getVoltage());
            energy+=std::pow(k->outputs[OtisKit::OUT].getVoltage(),2); }
        assert(energy>1 && k->voiceTake[3]==take);
        // Saved: the take is slot 3, named and at its rate; a restored kit plays it identically.
        json_t* saved=k->toJson(); auto restored=makeKit(); restored->fromJson(saved); json_decref(saved);
        assert(restored->slots[3].name=="Capture take 1" && restored->slots[3].rate==48000 && restored->slots[3].pcm==loaded->slots[3].pcm);
        assert(restored->slots[2].pcm==k->slots[2].pcm);
    }
    {   // Source KIT resamples the kit's own output; IN + KIT sums them; GAIN scales the take.
        auto k=makeKit(); auto c=makeCapture(); wire(*k,*c);
        c->params[OtisCapture::SOURCE].setValue(1); c->params[OtisCapture::GAIN].setValue(.5f); c->params[OtisCapture::SLOT].setValue(7);
        hit(*k,0,true); std::vector<float> out;
        c->params[OtisCapture::ARM_BUTTON].setValue(1);                                   // no CLOCK: at once, TEMPO 120
        c->params[OtisCapture::LENGTH].setValue(0);                                       // 1/4 beat = 6000 samples
        for(int n=0;n<7000;++n) { frame(*k,c.get(),a,true); out.push_back(k->outputs[OtisKit::OUT].getVoltage()); c->params[OtisCapture::ARM_BUTTON].setValue(0); }
        auto take=k->exchange.read().take[7];
        assert(take.size()==6000);
        // The take starts one sample after ARM (the request crosses one expander hop) and holds the kit's
        // own output from that sample on, at half gain.
        for(int i=0;i<6000;++i) assert(std::abs(k->engine.sample.pcm[size_t(take.begin+i)]-otis::quantize(out[size_t(1+i)]/5*.5f))<=1);
    }
    {   // No room: the take doesn't start, the slot keeps its take, and Capture says NO ROOM.
        auto k=makeKit(); k->slots[0].pcm=noise(otis::memoryWords,9); k->install(false);
        auto c=makeCapture(); wire(*k,*c); auto before=k->exchange.read();
        c->params[OtisCapture::ARM_BUTTON].setValue(1);
        for(int n=0;n<30000;++n) { frame(*k,c.get(),a,true); c->params[OtisCapture::ARM_BUTTON].setValue(0); }
        assert(k->exchange.read().take==before.take && c->displayState.load()==1 && k->takes==0);
    }
    {   // A take longer than the free memory is cut at its end and still lands, marked.
        auto k=makeKit(); k->slots[0].pcm=noise(otis::memoryWords-7*20000-1000,9); k->install(false);   // 1000 frames free
        auto c=makeCapture(); wire(*k,*c); c->params[OtisCapture::SLOT].setValue(1); c->params[OtisCapture::LENGTH].setValue(2);   // 1 beat
        auto room=k->bank.largestGap().size(); assert(room>900 && room<20000);
        c->params[OtisCapture::ARM_BUTTON].setValue(1);
        for(int n=0;n<30000;++n) { frame(*k,c.get(),a,true); c->params[OtisCapture::ARM_BUTTON].setValue(0); }
        assert(k->exchange.read().take[1].size()==room && c->displayState.load()==2);
    }
    {   // Saving during a take leaves the slot's old take in the save and doesn't disturb the take; the
        // link breaking mid-take ends it there; without CLOCK one beat at TEMPO 120 is 24000 samples.
        auto k=makeKit(); auto c=makeCapture(); wire(*k,*c);
        c->params[OtisCapture::SLOT].setValue(2); c->params[OtisCapture::LENGTH].setValue(2);
        c->inputs[OtisCapture::AUDIO_IN].channels=1; c->inputs[OtisCapture::AUDIO_IN].setVoltage(2.5f);
        auto old=k->slots[2].pcm;
        c->params[OtisCapture::ARM_BUTTON].setValue(1);
        int recorded=0;
        for(int n=0;n<30000;++n) {
            frame(*k,c.get(),a,false); c->params[OtisCapture::ARM_BUTTON].setValue(0);
            recorded+=c->outputs[OtisCapture::REC_OUT].getVoltage()>5;
            if(n==10000) { json_t* mid=k->dataToJson(); assert(k->slots[2].pcm==old); json_decref(mid); assert(k->bank.active()); }
        }
        assert(recorded==24000 && k->exchange.read().take[2].size()==24000);
        c->params[OtisCapture::ARM_BUTTON].setValue(1);
        for(int n=0;n<1000;++n) { frame(*k,c.get(),a,false); c->params[OtisCapture::ARM_BUTTON].setValue(0); }
        k->rightExpander.module=nullptr; c->leftExpander.module=nullptr;              // moved away
        for(int n=0;n<10;++n) { frame(*k,c.get(),a,false); }
        assert(!c->recording && !k->bank.active() && k->exchange.read().take[2].size()==999);
        // Compacting keeps every take, copied out first, and the bank plays them as before.
        k->install(true);
        // Takes are named as they are copied out; the first was replaced before anything copied it.
        assert(k->slots[2].pcm.size()==999 && k->slots[2].name=="Capture take 1" && k->exchange.read().take[2].size()==999);
    }
    // Installing a bank cancels the whole request, not just its recorded prefix. Check both
    // compaction and WAV loading, in either module processing order, then record a fresh take.
    for(bool captureFirst:{true,false}) for(bool load:{false,true}) {
        auto k=makeKit(); auto c=makeCapture(); wire(*k,*c);
        c->params[OtisCapture::LENGTH].setValue(0); // 1/4 beat at 120 BPM = 6000 samples
        c->inputs[OtisCapture::AUDIO_IN].channels=1;
        c->inputs[OtisCapture::AUDIO_IN].setVoltage(2.5f);
        frame(*k,c.get(),a,captureFirst);
        int recorded=0, dones=0; bool doneHigh=false;
        otis::kit::BankMap installed;
        for(int t=0;t<6005;++t) {
            c->params[OtisCapture::ARM_BUTTON].setValue(t==0 ? 1.f : 0.f);
            if(t==1000) {
                assert(k->bank.active() && k->bank.recording.size()>0);
                if(load) k->loadSlot(0,"examples/bright-demo.wav"); else k->install(true);
                installed=k->exchange.read();
                assert(!k->bank.active());
            }
            frame(*k,c.get(),a,captureFirst);
            bool rec=c->outputs[OtisCapture::REC_OUT].getVoltage()>5;
            bool d=c->outputs[OtisCapture::DONE_OUT].getVoltage()>5;
            assert(rec==(t<6000)); assert(d==(t>=6000));
            recorded+=rec; dones+=d && !doneHigh; doneHigh=d;
            if(t>=1000) {
                assert(!k->bank.active());
                assert(k->exchange.read().serial==installed.serial && k->exchange.read().take==installed.take);
            }
        }
        assert(recorded==6000 && dones==1 && k->takes==0);
        c->params[OtisCapture::SLOT].setValue(7);
        for(int t=0;t<6005;++t) {
            c->params[OtisCapture::ARM_BUTTON].setValue(t==0 ? 1.f : 0.f);
            frame(*k,c.get(),a,captureFirst);
        }
        auto take=k->exchange.read().take[7];
        assert(k->takes==1 && take.size()==6000);
        for(int i=take.begin;i<take.end;++i) assert(k->engine.sample.pcm[size_t(i)]==otis::quantize(.5f));
    }
    // Clock mode is chosen at start. Tempo fallback always measures from that start, while
    // a clock kept patched can run past the fallback deadline and still end on its pulse.
    for(bool captureFirst:{true,false}) for(int scenario=0;scenario<4;++scenario) {
        auto k=makeKit(); auto c=makeCapture(); wire(*k,*c);
        c->params[OtisCapture::LENGTH].setValue(0); // 6000 samples or one clock pulse
        c->inputs[OtisCapture::AUDIO_IN].channels=1;
        c->inputs[OtisCapture::AUDIO_IN].setVoltage(2.5f);
        bool startsClocked=scenario!=2;
        c->inputs[OtisCapture::CLOCK_IN].channels=startsClocked ? 1 : 0;
        frame(*k,c.get(),a,captureFirst); // establish low clock before the start edge
        int end=scenario==1 ? 7000 : scenario==3 ? 8000 : 6000;
        int recorded=0, dones=0; bool doneHigh=false;
        for(int t=0;t<end+5;++t) {
            c->params[OtisCapture::ARM_BUTTON].setValue(t==0 ? 1.f : 0.f);
            if((scenario==0 && t==2000) || (scenario==1 && t==7000))
                c->inputs[OtisCapture::CLOCK_IN].channels=0;
            if(scenario==2 && t==2000) c->inputs[OtisCapture::CLOCK_IN].channels=1;
            bool pulse=t==0 || (scenario==2 && t>=2000 && t%100==0) || (scenario==3 && t==8000);
            c->inputs[OtisCapture::CLOCK_IN].setVoltage(pulse ? 10.f : 0.f);
            frame(*k,c.get(),a,captureFirst);
            bool rec=c->outputs[OtisCapture::REC_OUT].getVoltage()>5;
            bool d=c->outputs[OtisCapture::DONE_OUT].getVoltage()>5;
            assert(rec==(t<end)); assert(d==(t>=end));
            recorded+=rec; dones+=d && !doneHigh; doneHigh=d;
        }
        auto take=k->exchange.read().take[0];
        assert(recorded==end && dones==1 && k->takes==1 && take.size()==end);
        for(int i=take.begin;i<take.end;++i) assert(k->engine.sample.pcm[size_t(i)]==otis::quantize(.5f));
    }
    rack::plugin::plugins.clear();
    std::cout<<"PASS: Otis Capture with Otis Kit: no kit, clock-quantized takes of exact length and content in either processing order, old take sounding on and other slots untouched while the new one lands, new hits on the take, save/restore, resampling the kit at GAIN, no room, truncation, save mid-take, TEMPO length, link loss, compaction, install cancellation without tails, latched clock mode and tempo fallback\n";
}
