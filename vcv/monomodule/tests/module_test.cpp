// SPDX-License-Identifier: AGPL-3.0-only
// The real module through Rack's API, on the deterministic six-track backend: TRIG gates are notes with
// V/OCT's pitch and end when they fall; the six tracks reach TRACKS L/R; the sound link plays notes and
// locks. With GORGON_SOURCE, a real Gorgon plays a version-3 score into all six tracks.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#include "../src/Monomodule.cpp"
#include "Fake.hpp"
#ifdef GORGON_SOURCE
#include GORGON_SOURCE
#endif
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
using namespace mnm_rack;
Plugin* pluginInstance=nullptr;
static mnm_rack::Worker::Factory factory(Trace& trace){return [&trace](const std::string&,const std::string&)->std::unique_ptr<Backend>{return std::make_unique<Fake>(&trace);};}
static void ready(Monomodule& m){m.worker.load("fake");auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!m.worker.ready){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static void drain(){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
static void flip(Module& m){if(m.leftExpander.messageFlipRequested){std::swap(m.leftExpander.producerMessage,m.leftExpander.consumerMessage);m.leftExpander.messageFlipRequested=false;}}
// Publish worker-owned host state under a mutex; the test never reads a live backend directly.
struct HostTrace {
    std::mutex mutex;
    std::array<MnmMachine,tracks> machines{};
    std::array<bool,tracks> gates{};
    std::array<uint8_t,tracks> settings{};
    int loads=0;
};
class HostBackend:public Backend {
    HostTrace& trace;
    std::array<HostVoice,tracks> voices;
    Tracks<HostVoice> state;
public:
    explicit HostBackend(HostTrace& t):trace(t){for(int i=0;i<tracks;++i)state.attach(i,voices[i]);std::lock_guard<std::mutex> lock(trace.mutex);++trace.loads;}
    bool fire(const Hit& h) override{return state.fire(h);}
    void release(int t) override{state.release(t);}
    void initialize() override{state.initialize();}
    void panic() override{state.panic();}
    void configure(const uint8_t (&s)[emu_rack::trackSettings]) override{state.configure(s);std::lock_guard<std::mutex> lock(trace.mutex);std::copy(s,s+tracks,trace.settings.begin());}
    void input(const float* in) override{state.input(in);}
    void render(float (&out)[nativeFrames][channels]) override{
        state.render(out);std::lock_guard<std::mutex> lock(trace.mutex);
        for(int t=0;t<tracks;++t){trace.machines[t]=state.machine(t);trace.gates[t]=voices[t].gate;}
    }
};
struct RoutingVoice:HostVoice {
    std::array<float,nativeFrames> receivedL{},receivedR{};
    float level=0;
    void process(float* l,float* r,int n){step();std::fill(l,l+n,level);std::fill(r,r+n,-level);}
    void processFx(const float* l,const float* r,float* outL,float* outR,int n){
        std::copy(l,l+n,receivedL.begin());std::copy(r,r+n,receivedR.begin());HostVoice::processFx(l,r,outL,outR,n);
    }
};
static void linkSourceChanges(Plugin& providerPlugin){
    Trace trace;auto m=std::make_unique<Monomodule>(factory(trace));m->model=modelMonomodule;ready(*m);
    Model model;model.slug="Gorgon";model.plugin=&providerPlugin;
    Module first,second;first.model=second.model=&model;first.id=10;second.id=11;
    m->leftExpander.module=&first;
    Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
    auto tick=[&](int f,float gate,bool publish=true){
        args.frame=f;
        if(publish){auto* p=static_cast<gorgon_sound::Packet*>(m->leftExpander.consumerMessage);
            p->frame=f;p->moving=1;p->transport=7;p->lanes[0].gate=gate;p->lanes[0].velocity=1;}
        m->process(args);
    };
    tick(0,0);tick(1,0);tick(2,10);assert(m->block.count==2); // epoch panic, then hit
    m->leftExpander.module=&second;
    tick(3,0,false); // still a fresh packet, but written by first
    assert(m->block.count==3&&m->block.events[2].panic);
    tick(4,0,false);assert(!m->link.connected&&m->block.count==3); // old packet is still within the 2-sample freshness window
    tick(5,10);assert(m->block.count==3); // seed second's held gate; never invent a hit
    tick(6,0);tick(7,10);assert(m->block.count==5&&m->block.events[3].release&&!m->block.events[4].panic);
    // Rack may reuse a module address: the engine ID also identifies the sender.
    second.id=12;tick(8,0,false);assert(m->block.count==6&&m->block.events[5].panic);
    tick(9,10);assert(m->block.count==6);
    // Loop-wrap reset alone keeps the new sender's held note alive.
    auto* packet=static_cast<gorgon_sound::Packet*>(m->leftExpander.consumerMessage);
    packet->reset=10;tick(10,10);assert(m->block.count==6);
    packet->reset=0;tick(11,10);
    // TRIG overrides the link, ends its session, and seeds a cable already held high.
    m->inputs[Monomodule::TRIG].channels=1;m->inputs[Monomodule::TRIG].setVoltage(10);
    tick(12,10);assert(m->block.count==7&&m->block.events[6].panic&&!m->link.connected);
    m->leftExpander.module=&first;tick(13,10);assert(m->block.count==7);
    m->inputs[Monomodule::TRIG].channels=0;
    tick(14,10,false);assert(m->block.count==8&&m->block.events[7].release&&!m->link.connected);
    tick(15,10);assert(m->block.count==8&&m->link.connected);
    // Initialize resets the receiver but retains the provider-change frame barrier. A held
    // link gate must fall and rise before a new note, just like a held TRIG gate.
    Module::ResetEvent reset;m->onReset(reset);tick(16,10);
    assert(m->block.count==1&&m->block.events[0].initialize);
    tick(17,0);tick(18,10);
    assert(m->block.count==3&&m->block.events[1].release&&!m->block.events[2].panic);
    // A replacement can publish immediately while the previous sender's gate was low.
    // Even a packet stamped this frame cannot turn that sender's held high into a note.
    tick(19,0);assert(m->block.count==4&&m->block.events[3].release);
    m->leftExpander.module=&second;
    tick(20,10);assert(m->block.count==5&&m->block.events[4].panic&&!m->link.connected);
    tick(21,10);assert(m->block.count==5&&m->link.connected);
    tick(22,0);tick(23,10);
    assert(m->block.count==7&&m->block.events[5].release&&!m->block.events[6].panic);
    std::cout<<"Link provider pointer/ID changes, stale packets, loop reset, TRIG and Initialize passed\n";
}
int main(){
    Context context;context.engine=new engine::Engine;contextSet(&context);
    Plugin receiverPlugin;receiverPlugin.slug="MonomoduleRack";receiverPlugin.version="2.0.4";modelMonomodule->plugin=&receiverPlugin;
    Plugin linkPlugin;linkPlugin.slug="GorgonBridge";linkPlugin.version="2.4.0";
    linkSourceChanges(linkPlugin);
    {
        // Rack Initialize must reach the live worker without reconstructing its OS-backed engine.
        HostTrace trace;
        auto m=std::make_unique<Monomodule>([&](const std::string&,const std::string&){return std::make_unique<HostBackend>(trace);});
        m->model=modelMonomodule;ready(*m);m->inputs[Monomodule::TRIG].channels=6;
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        auto run=[&](int count){for(int f=0;f<count;++f){++args.frame;m->process(args);if(f%128==127)drain();}drain();};
        run(2000);
        for(int t=0;t<tracks;++t){m->fxSource[t]=INP_B;m->inputs[Monomodule::TRIG].setVoltage(10,t);}
        m->params[Monomodule::MACHINE].setValue(4);run(2000);
        {std::lock_guard<std::mutex> lock(trace.mutex);for(int t=0;t<tracks;++t)assert(trace.machines[t]!=MnmMachine::GND&&trace.gates[t]);}
        Module::ResetEvent reset;m->onReset(reset);run(3000);
        {std::lock_guard<std::mutex> lock(trace.mutex);
            assert(trace.loads==1&&m->worker.ready&&m->worker.loaded==1);
            for(int t=0;t<tracks;++t){assert(trace.machines[t]==MnmMachine::GND&&!trace.gates[t]);assert(trace.settings[t]==(t?NEIBOR:INP_AB));}}
        // An FX machine must also lose its continuous trig on Initialize.
        for(int t=0;t<tracks;++t)m->inputs[Monomodule::TRIG].setVoltage(0,t);run(1000);
        m->params[Monomodule::MACHINE].setValue(21);
        for(int t=0;t<tracks;++t)m->inputs[Monomodule::TRIG].setVoltage(10,t);run(2000);
        {std::lock_guard<std::mutex> lock(trace.mutex);for(auto machine:trace.machines)assert(mnm::host::isFxMachine(machine));}
        m->onReset(reset);run(3000);
        {std::lock_guard<std::mutex> lock(trace.mutex);assert(trace.loads==1);for(int t=0;t<tracks;++t)assert(trace.machines[t]==MnmMachine::GND&&!trace.gates[t]);}
        std::cout<<"Initialize: every track GND, default FX inputs, no notes, OS retained, passed\n";
    }
    {
        std::array<RoutingVoice,tracks> voices;Tracks<RoutingVoice> state;
        for(int t=0;t<tracks;++t){state.attach(t,voices[t]);voices[t].level=float(t+1)/10;}
        auto fire=[&](int t){Hit h;h.track=t;h.machine=int16_t(MnmMachine::THRU);h.note=60;assert(state.fire(h));};
        float in[nativeFrames][inputs],out[nativeFrames][channels];
        for(int f=0;f<nativeFrames;++f){in[f][0]=float(f+1)/100;in[f][1]=-float(f+1)/50;}
        uint8_t settings[emu_rack::trackSettings]={};
        const mnm::host::FxInput sources[]={mnm::host::FxInput::Neighbor,mnm::host::FxInput::InpA,mnm::host::FxInput::InpB,mnm::host::FxInput::InpAB};
        fire(1);
        for(int source=NEIBOR;source<=INP_AB;++source){
            settings[1]=source;state.configure(settings);
            HostVoice expected;expected.host().setRouting(mnm::host::dspInputBits(sources[source]));
            assert(voices[1].host().routingWord()==expected.host().routingWord());
            voices[0].level+=.1f;state.input(&in[0][0]);state.render(out);
            for(int f=0;f<nativeFrames;++f){assert(voices[1].receivedL[f]==(source==NEIBOR?voices[0].level:in[f][0]));assert(voices[1].receivedR[f]==(source==NEIBOR?-voices[0].level:in[f][1]));}
        }
        // Two adjacent FX tracks prove a neighbour uses this block, including stereo and the first-track boundary.
        fire(2);settings[1]=INP_AB;settings[2]=NEIBOR;state.configure(settings);state.input(&in[0][0]);state.render(out);
        for(int f=0;f<nativeFrames;++f){assert(voices[2].receivedL[f]==in[f][0]);assert(voices[2].receivedR[f]==in[f][1]);}
        fire(0);state.render(out);
        for(int f=0;f<nativeFrames;++f)assert(voices[0].receivedL[f]==0&&voices[0].receivedR[f]==0);
        std::cout<<"FX routing: all source bits, external stereo buffers, same-block neighbour, first-track silence passed\n";
    }
    {
        // Patch values are validated before narrowing to int; every invalid/missing entry uses its default.
        auto m=std::make_unique<Monomodule>();
        auto check=[&](const char* text,const std::array<int,tracks>& expected){
            json_error_t error;auto* root=json_loads(text,0,&error);assert(root);
            m->dataFromJson(root);json_decref(root);
            for(int t=0;t<tracks;++t)assert(m->fxSource[t]==expected[t]);
        };
        check("{}",{INP_AB,NEIBOR,NEIBOR,NEIBOR,NEIBOR,NEIBOR});
        check("{\"fxInputs\":[1,2]}",{INP_A,INP_B,NEIBOR,NEIBOR,NEIBOR,NEIBOR});
        check("{\"fxInputs\":[-1,4,1.0,\"2\",null,4294967297]}",{INP_AB,NEIBOR,NEIBOR,NEIBOR,NEIBOR,NEIBOR});
        check("{\"fxInputs\":7}",{INP_AB,NEIBOR,NEIBOR,NEIBOR,NEIBOR,NEIBOR});
        for(int t=0;t<tracks;++t)m->fxSource[t]=t%4;
        auto* saved=m->dataToJson();auto copy=std::make_unique<Monomodule>();copy->dataFromJson(saved);json_decref(saved);
        for(int t=0;t<tracks;++t)assert(copy->fxSource[t]==t%4);
        std::cout<<"FX inputs: round trip and malformed patches passed\n";
    }
    {
        // TRIG: track 3 plays V/OCT's note while its gate is high, and ends when it falls.
        Trace trace;auto mm=std::make_unique<Monomodule>(factory(trace));auto& m=*mm;m.model=modelMonomodule;ready(m);
        for(auto& o:m.outputs)o.channels=1;
        m.inputs[Monomodule::TRIG].channels=6;m.inputs[Monomodule::VOCT].channels=1;m.inputs[Monomodule::VOCT].setVoltage(.75f);
        m.params[Monomodule::P1].setValue(20);
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        for(int f=0;f<6000;++f){
            args.frame=f;
            m.inputs[Monomodule::TRIG].setVoltage(f>=300&&f<2300?10.f:0.f,2);
            m.process(args);if(f%128==127)drain();
        }
        drain();
        {std::lock_guard<std::mutex> guard(trace.mutex);
            assert(trace.events.size()==2&&trace.resets>=1);   // the restart's panic is a reset; then note on, note off
            const auto& on=trace.events[0];const auto& off=trace.events[1];
            assert(!on.release&&on.hit.track==2&&on.hit.note==69&&on.hit.machine==int16_t(machineIdAt(4)));
            assert(std::lround(on.hit.offsets[0])==20&&off.release&&off.hit.track==2);
            assert(off.native-on.native>=1984&&off.native-on.native<=2016);}
        assert(m.outputs[Monomodule::TRACKS_L].getChannels()==6&&m.outputs[Monomodule::TRACKS_R].getChannels()==6);
        std::cout<<"TRIG: gate on V/OCT's note, held until it falls, on track 3 passed\n";
        // INP A reaches the engine (the fake plays it back on track 6) after the queue; FX inputs are settings.
        m.inputs[Monomodule::INP_A].channels=1;m.inputs[Monomodule::INP_A].setVoltage(2.5f);
        m.fxSource[3]=INP_B;
        for(int f=6000;f<6000+3000;++f){args.frame=f;m.process(args);if(f%128==127)drain();}
        assert(std::abs(m.outputs[Monomodule::TRACKS_L].getVoltage(5)-2.5f)<1e-4f);   // 0.5 of full scale, back at 5 V
        json_t* saved=m.dataToJson();
        assert(json_integer_value(json_array_get(json_object_get(saved,"fxInputs"),3))==INP_B);
        assert(json_integer_value(json_array_get(json_object_get(saved,"fxInputs"),0))==INP_AB);
        json_decref(saved);
        std::cout<<"INP A into the engine, and FX inputs saved with the patch, passed\n";
    }
    {
        // Unplugging TRIG while a gate is high ends that note (review #5), here with a moving Gorgon on the
        // left, so the link takes over; a channel count drop ends a note too.
        Plugin providerPlugin;providerPlugin.slug="GorgonBridge";providerPlugin.version="2.4.0";
        Model providerModel;providerModel.slug="Gorgon";providerModel.plugin=&providerPlugin;
        Trace trace;auto mm=std::make_unique<Monomodule>(factory(trace));auto& m=*mm;m.model=modelMonomodule;ready(m);
        for(auto& o:m.outputs)o.channels=1;
        Module provider;provider.model=&providerModel;m.leftExpander.module=&provider;
        m.inputs[Monomodule::TRIG].channels=6;
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        gorgon_sound::Packet packet;packet.moving=true;packet.transport=1;
        bool linked=false;
        for(int f=0;f<10000;++f){
            args.frame=f;packet.frame=f;
            *static_cast<gorgon_sound::Packet*>(m.leftExpander.producerMessage)=packet;m.leftExpander.messageFlipRequested=true;
            if(f==3000)m.inputs[Monomodule::TRIG].channels=0;   // the cable is pulled out with tracks 2 and 3 high
            else if(f<3000){m.inputs[Monomodule::TRIG].setVoltage(f>=300?10.f:0.f,1);m.inputs[Monomodule::TRIG].setVoltage(f>=400?10.f:0.f,2);}
            if(f==5000){m.inputs[Monomodule::TRIG].channels=6;for(int c=0;c<6;++c)m.inputs[Monomodule::TRIG].setVoltage(0.f,c);}
            if(f>=5300)m.inputs[Monomodule::TRIG].setVoltage(f<9000?10.f:0.f,4);
            if(f==7000)m.inputs[Monomodule::TRIG].channels=4;   // track 5's channel goes away while its gate is high
            m.process(args);flip(m);if(f%128==127)drain();
            if(f==4000)linked=m.lights[Monomodule::LINK].getBrightness()>0;
        }
        drain();
        m.leftExpander.module=nullptr;
        assert(linked);
        {std::lock_guard<std::mutex> guard(trace.mutex);
            int on[tracks]={},off[tracks]={};for(const auto& e:trace.events)(e.release?off:on)[e.hit.track]++;
            assert(on[1]==1&&off[1]==1&&on[2]==1&&off[2]==1&&on[4]==1&&off[4]==1);}
        assert(std::abs(m.outputs[Monomodule::TRACKS_L].getVoltage(1))<1e-4f&&std::abs(m.outputs[Monomodule::TRACKS_L].getVoltage(2))<1e-4f);
        assert(std::abs(m.outputs[Monomodule::TRACKS_L].getVoltage(4))<1e-4f);
        std::cout<<"TRIG unplugged or narrowed with gates high: those notes end, passed\n";
    }
    {
        // A 10-sample trigger at 48 kHz, shorter than one 16-frame engine block, still sounds: its release
        // reaches the engine one block after its note (review #16).
        Trace trace;auto mm=std::make_unique<Monomodule>(factory(trace));auto& m=*mm;m.model=modelMonomodule;ready(m);
        for(auto& o:m.outputs)o.channels=1;
        m.inputs[Monomodule::TRIG].channels=1;
        Module::ProcessArgs args{};args.sampleRate=48000;args.sampleTime=1.f/48000;
        for(int f=0;f<6000;++f){
            args.frame=f;
            m.inputs[Monomodule::TRIG].setVoltage(f>=301&&f<311?10.f:0.f,0);
            m.process(args);if(f%128==127)drain();
        }
        drain();
        {std::lock_guard<std::mutex> guard(trace.mutex);
            assert(trace.events.size()==2&&!trace.events[0].release&&trace.events[1].release);
            assert(trace.events[1].native==trace.events[0].native+nativeFrames);}
        std::cout<<"A gate shorter than one engine block plays for one block, passed\n";
    }
    {
        // The sound link: lane 6's note and machine lock, and its release.
        Plugin providerPlugin;providerPlugin.slug="GorgonBridge";providerPlugin.version="2.4.0";
        Model providerModel;providerModel.slug="Gorgon";providerModel.plugin=&providerPlugin;
        Trace trace;auto mm=std::make_unique<Monomodule>(factory(trace));auto& m=*mm;m.model=modelMonomodule;ready(m);
        for(auto& o:m.outputs)o.channels=1;
        Module provider;provider.model=&providerModel;m.leftExpander.module=&provider;
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        gorgon_sound::Packet packet;
        for(int f=0;f<4000;++f){
            args.frame=f;packet.frame=f;packet.moving=f>=4;packet.transport=f>=4?1:0;
            packet.lanes[5].gate=f>=100&&f<1100?10.f:0;packet.lanes[5].velocity=1;packet.lanes[5].pitch=-1.f;   // C3
            packet.lanes[5].sound.machine=int16_t(MnmMachine::FM_PAR);packet.lanes[5].sound.mask=1;packet.lanes[5].sound.values[0]=99;
            *static_cast<gorgon_sound::Packet*>(m.leftExpander.producerMessage)=packet;m.leftExpander.messageFlipRequested=true;
            m.process(args);flip(m);if(f%128==127)drain();
        }
        drain();
        {std::lock_guard<std::mutex> guard(trace.mutex);
            std::vector<Record> notes;for(const auto& e:trace.events)notes.push_back(e);
            assert(notes.size()==2&&notes[0].hit.track==5&&notes[0].hit.note==48&&notes[0].hit.machine==int16_t(MnmMachine::FM_PAR));
            assert(notes[0].hit.values[0]==99&&notes[1].release&&notes[1].hit.track==5);}
        m.leftExpander.module=nullptr;
        std::cout<<"Sound link: lane 6's note, machine and lock, then its release, passed\n";
    }
    {
        // A panic (a stop, seek, RESET or restart) ends every note and keeps every machine (review #9):
        // track 4 THRU on INP A, track 5 SAW, track 6 REVERB on NEIBOR. Tracks on host-model voices, no OS.
        std::array<HostVoice,tracks> voices;Tracks<HostVoice> state;
        for(int t=0;t<tracks;++t)state.attach(t,voices[size_t(t)]);
        uint8_t settings[emu_rack::trackSettings]={};settings[3]=INP_A;settings[5]=NEIBOR;state.configure(settings);
        auto hit=[&](int track,MnmMachine machine){Hit h;h.track=uint8_t(track);h.machine=int16_t(machine);h.note=60;h.bpm=120;return state.fire(h);};
        assert(hit(3,MnmMachine::THRU)&&hit(4,MnmMachine::SAW)&&hit(5,MnmMachine::REVERB));
        voices[4].host().setParam(mnm::host::Page::AMP,2,99);   // a setting a machine assign would reset to 64
        float in[nativeFrames][inputs]={},out[nativeFrames][channels];
        for(auto& f:in)f[0]=.5f;
        auto block=[&](int n){for(int b=0;b<n;++b){state.input(&in[0][0]);state.render(out);}};
        auto level=[&](int track){return out[nativeFrames-1][trackLeft+track];};
        block(6);
        assert(level(3)==.5f&&level(4)==.25f&&level(5)==.25f);
        state.panic();block(6);
        assert(level(4)==0);   // SAW's note ended
        assert(state.machine(3)==MnmMachine::THRU&&state.machine(4)==MnmMachine::SAW&&state.machine(5)==MnmMachine::REVERB);
        assert(voices[4].host().machine()==MnmMachine::SAW&&voices[4].host().param(mnm::host::Page::AMP,2)==99);
        assert(level(3)==.5f);   // THRU still passes INP A
        {const auto bits=mnm::host::dspInputBits(mnm::host::FxInput::InpA);assert((voices[3].host().routingWord()&bits)==bits);}
        // Track 5's next note plays SAW with its settings, and REVERB on track 6 hears it with no hit of its own.
        assert(hit(4,MnmMachine::SAW));block(6);
        assert(voices[4].host().param(mnm::host::Page::AMP,2)==99&&level(4)==.25f&&level(5)==.25f);
        std::cout<<"Panic: notes end, machines and FX inputs stay, FX tracks keep processing, passed\n";
    }
#ifdef GORGON_SOURCE
    {
        // A real Gorgon 2.4 plays a version-3 score into all six tracks.
        modelGorgon->plugin=nullptr;
        Plugin providerPlugin;providerPlugin.slug="GorgonBridge";providerPlugin.version="2.4.0";modelGorgon->plugin=&providerPlugin;
#ifdef _WIN32
        _putenv_s("HYDRAPY_STUDIO_FILE","C:/nonexistent/monomodule-interop-test");
#else
        setenv("HYDRAPY_STUDIO_FILE","/nonexistent/monomodule-interop-test",1);
#endif
        Trace trace;auto rm=std::make_unique<Monomodule>(factory(trace));auto& receiver=*rm;receiver.model=modelMonomodule;ready(receiver);
        auto sm=std::make_unique<Gorgon>();auto& sender=*sm;sender.model=modelGorgon;
        sender.rightExpander.module=&receiver;receiver.leftExpander.module=&sender;
        for(auto& out:sender.outputs)out.channels=1;for(auto& out:receiver.outputs)out.channels=1;
        auto score=std::make_shared<gorgon::v3::ScoreV3>();score->bpm=120;score->loop=true;
        gorgon::v3::PatternV3 pattern;pattern.geometry.w=480;pattern.geometry.rows=pattern.geometry.bar=16;pattern.geometry.bars=1;
        for(auto& lane:pattern.lanes){lane.len=16;lane.cells.assign(16,gorgon::v3::Cell());}
        for(int lane=0;lane<6;++lane){auto& c=pattern.lanes[size_t(lane)].cells[size_t(lane)];c.present=true;c.vel=127;c.midi=60+lane;}
        score->store.push_back(pattern);score->index[0]=0;score->buildTimeline();
        auto& s=sender.snapshot;s.score3=score;s.layer=std::make_shared<gorgon::v3::Layer>();
        s.playhead.valid=s.playhead.enabled=s.playhead.v3=true;s.playhead.playing=false;s.playhead.transportRevision=0;s.clock.observe(0,0,0);
        sender.haveSnapshot=true;
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        for(int f=0;f<44100;++f){
            args.frame=f;
            if(f==64){s.playhead.playing=true;s.playhead.anchorTime=64./44100;s.playhead.transportRevision=1;}
            sender.process(args);receiver.process(args);flip(receiver);
            if(f%128==127)std::this_thread::sleep_for(std::chrono::microseconds(800));
        }
        drain();
        {std::lock_guard<std::mutex> guard(trace.mutex);
            std::vector<Record> on;for(const auto& e:trace.events)if(!e.release&&e.hit.velocity)on.push_back(e);
            assert(on.size()>=6);   // one note per lane, in row order
            for(int lane=0;lane<6;++lane)assert(on[size_t(lane)].hit.track==lane&&on[size_t(lane)].hit.note==60+lane);}
        std::cout<<"Real Gorgon -> Monomodule: six lanes, six tracks, each with its note, passed\n";
        modelGorgon->plugin=nullptr;
    }
#endif
    contextSet(nullptr);delete context.engine;context.engine=nullptr;
    std::cout<<"module_test: real Rack API, TRIG notes, sound link passed\n";
}
