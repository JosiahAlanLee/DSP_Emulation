// SPDX-License-Identifier: AGPL-3.0-only
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Machinemodule.cpp"
#include "Fake.hpp"
#ifdef GORGON_SOURCE
#include GORGON_SOURCE
#endif
#ifdef BYTEBUS_COUNTER
#include BYTEBUS_COUNTER
#include BYTEBUS_LOOKUP
#endif
#include <random>
#include <cassert>
#include <iostream>
Plugin* pluginInstance=nullptr;
using namespace md_rack;
static void ready(Machinemodule& m){m.worker.load("fake");auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!m.worker.ready){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static void flip(Module& receiver){auto& e=receiver.leftExpander;if(e.messageFlipRequested){std::swap(e.producerMessage,e.consumerMessage);e.messageFlipRequested=false;}}
static auto factory(Trace& trace){return [&trace](const std::string&,const std::string&){return std::make_unique<Fake>(&trace);};}
static void drain(){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
// Bus TRIG mode (docs/BUS_TRIG.md). Each run plays `frames` samples through one module, then
// three blocks holding the last inputs, since hits render once the engine's native clock reaches them.
template<class Drive> static void play(Machinemodule& m,Module::ProcessArgs& args,int frames,Drive drive){
    for(int f=0;f<frames+384;++f){drive(std::min(f,frames-1));m.process(args);++args.frame;if(args.frame%128==0)drain();}
    drain();
}
static std::vector<int> hitTracks(Trace& trace){std::lock_guard<std::mutex> guard(trace.mutex);std::vector<int> t;for(auto& h:trace.hits)t.push_back(h.hit.track);return t;}
static void setBus(Input& in,int channels,unsigned bits,bool clk){in.channels=channels;for(int k=0;k<16;++k)in.setVoltage(k<8&&(bits>>k&1)?10.f:(k==8&&clk?10.f:0.f),k);}
static void busTrig(){
    Module::ProcessArgs args{};args.sampleRate=48000;args.sampleTime=1.f/48000;
    auto make=[](Trace& trace){auto m=std::make_unique<Machinemodule>(factory(trace));m->model=modelMachinemodule;ready(*m);for(auto& o:m->outputs)o.channels=1;return m;};
    auto repeat=[](std::vector<int> group,int times){std::vector<int> all;for(int i=0;i<times;++i)all.insert(all.end(),group.begin(),group.end());return all;};
    // Nine channels, bits 0, 2, 5, 7 held high, a one-sample CLK every 64 samples: tracks 1, 3, 6, 8
    // hit on every pulse after the first (which arms), and channel 9 never hits track 9.
    {
        Trace trace;auto m=make(trace);m->trigMode=Machinemodule::TRIG_BUS;
        play(*m,args,64*20,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0xa5,f%64==10);});
        assert(hitTracks(trace)==repeat({0,2,5,7},19));
        // Tracks mode on the same cable: channel 9 is track 9 again and the held bits never retrigger.
        m->trigMode=Machinemodule::TRIG_TRACKS;
        play(*m,args,64*5,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0xa5,f%64==10);});
        auto all=hitTracks(trace);assert(all.size()==19*4+5);for(size_t i=19*4;i<all.size();++i)assert(all[i]==8);
    }
    // Eight channels (no CLK): per-bit rising edges only, as Tracks mode on eight channels.
    {
        Trace trace;auto m=make(trace);m->trigMode=Machinemodule::TRIG_BUS;
        unsigned words[]={0,0x03,0x07,0x06,0x07,0x80};
        play(*m,args,128*6,[&](int f){setBus(m->inputs[Machinemodule::TRIG],8,words[f/128],false);});
        assert((hitTracks(trace)==std::vector<int>{0,1,2,0,7}));
    }
    // A hot-plug or a mode switch with CLK already high: no hit until the second edge. A bus
    // that arrives with CLK low arms on its first real edge instead.
    for(bool modeSwitch:{false,true})for(bool clkHigh:{true,false}){
        Trace trace;auto m=make(trace);
        if(modeSwitch)play(*m,args,256,[&](int){setBus(m->inputs[Machinemodule::TRIG],9,0x81,clkHigh);});
        else play(*m,args,256,[&](int){m->inputs[Machinemodule::TRIG].channels=0;});
        m->trigMode=Machinemodule::TRIG_BUS;
        play(*m,args,256,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0x81,clkHigh&&f<100);});
        assert(hitTracks(trace).empty());
        play(*m,args,256,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0x81,f==50||f==150);});
        auto hits=hitTracks(trace);
        if(clkHigh)assert((hits==std::vector<int>{0,7,0,7}));  // the high CLK armed; both later edges hit
        else assert((hits==std::vector<int>{0,7}));            // the first real edge armed
    }
    // VELOCITY: per channel from a poly cable, broadcast from a mono one, 127 unpatched.
    {
        Trace trace;auto m=make(trace);m->trigMode=Machinemodule::TRIG_BUS;
        auto& vel=m->inputs[Machinemodule::VELOCITY];vel.channels=8;for(int k=0;k<8;++k)vel.setVoltage(1.f+k,k);
        play(*m,args,256,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0xff,f%128==10);});
        vel.channels=1;vel.setVoltage(5.f);
        play(*m,args,128,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0xff,f==10);});
        vel.channels=0;
        play(*m,args,128,[&](int f){setBus(m->inputs[Machinemodule::TRIG],9,0x01,f==10);});
        std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==17);
        for(int k=0;k<8;++k)assert(trace.hits[k].hit.track==k&&trace.hits[k].hit.velocity==std::lround((1.f+k)/10*127));
        for(int k=8;k<16;++k)assert(trace.hits[k].hit.velocity==64);
        assert(trace.hits[16].hit.velocity==127);
    }
    // The mode is saved with the patch; a patch without it reads TRIG as tracks. Modules live on the
    // heap here: each is ~240 KB, and main's frame already uses most of Windows' 2 MB stack.
    {
        Trace trace;auto m=std::make_unique<Machinemodule>(factory(trace));m->trigMode=Machinemodule::TRIG_BUS;
        json_t* saved=m->dataToJson();assert(std::string(json_string_value(json_object_get(saved,"trigMode")))=="bus");
        auto r=std::make_unique<Machinemodule>(factory(trace));r->dataFromJson(saved);assert(r->trigMode==Machinemodule::TRIG_BUS);
        json_object_del(saved,"trigMode");r->dataFromJson(saved);json_decref(saved);assert(r->trigMode==Machinemodule::TRIG_TRACKS);
    }
#ifdef BYTEBUS_COUNTER
    // Counter -> Lookup (Rhythm) -> Machinemodule through one-sample cables, in random process
    // order, plays the hit list the chain gives evaluated directly: one cable hop changes when,
    // never what. The Counter's seed publication only arms. Before that, for one sample, Lookup
    // passes on Rack's one-channel 0 V placeholder as an unclocked word 00, so the unclocked path
    // plays Lookup's word for address 00 once (docs/BUS_TRIG.md, "Starting a chain").
    auto library=std::make_shared<dspemu::lookup::Library>(dspemu::lookup::Library::load(BYTEBUS_LIBRARY));
    std::vector<int> expected;
    {
        namespace lu=dspemu::lookup;lu::Engine direct;lu::Parameters p;p.family=lu::Rhythm;p.seed=0x33;
        for(int count=0;count<=300;++count){
            dspemu::bus::Sample s;s.connected=s.clocked=s.rising=s.clk=s.updated=true;s.rawWord=s.word=uint8_t(count*3%256);
            if(count==0)s.clocked=s.rising=s.clk=false;
            uint8_t word=direct.tick(*library,p,s,48000).word;for(int k=0;k<8;++k)if(word>>k&1)expected.push_back(k);
        }
    }
    for(unsigned seed:{1u,2u,3u}){
        Trace trace;auto m=make(trace);m->trigMode=Machinemodule::TRIG_BUS;
        ByteCounter counter;ByteLookup lookup(library);
        // As in Rack: connecting a cable gives its output one channel at 0 V until the module writes.
        for(auto* module:std::initializer_list<Module*>{&counter,&lookup})for(auto& o:module->outputs)o.channels=1;
        counter.params[ByteCounter::CLOCK_MODE].setValue(1);counter.params[ByteCounter::INC].setValue(3);
        counter.inputs[ByteCounter::CLOCK_IN].channels=1;
        lookup.params[ByteLookup::FAMILY].setValue(dspemu::lookup::Rhythm);lookup.params[ByteLookup::SEED].setValue(0x33);
        auto copy=[](Output& from,Input& to){to.channels=from.getChannels();for(int k=0;k<16;++k)to.setVoltage(k<to.channels?from.getVoltage(k):0.f,k);};
        std::mt19937 order(seed);
        // 300 clock pulses, then enough samples to finish the engine's last 128-sample block.
        for(int f=0;f<64*300+512;++f){
            counter.inputs[ByteCounter::CLOCK_IN].setVoltage(f<64*300&&f%64==20?10.f:0.f);
            copy(counter.outputs[ByteCounter::BUS_OUT],lookup.inputs[ByteLookup::BUS_IN]);
            copy(lookup.outputs[ByteLookup::BUS_OUT],m->inputs[Machinemodule::TRIG]);
            Module* modules[]={&counter,&lookup,m.get()};std::shuffle(std::begin(modules),std::end(modules),order);
            for(auto* module:modules)module->process(args);
            ++args.frame;if(args.frame%128==0)drain();
        }
        drain();
        auto hits=hitTracks(trace);
        assert(hits==expected&&hits.size()>40);
        std::lock_guard<std::mutex> guard(trace.mutex);for(auto& h:trace.hits)assert(h.hit.velocity==127);
    }
#endif
}
int main(){
    Context context;context.engine=new engine::Engine;contextSet(&context);
    Plugin receiverPlugin;receiverPlugin.slug="MachinemoduleRack";receiverPlugin.version="2.0.0";modelMachinemodule->plugin=&receiverPlugin;
    Plugin providerPlugin;providerPlugin.slug="GorgonBridge";providerPlugin.version="2.3.1";
    Model providerModel;providerModel.slug="Gorgon";providerModel.plugin=&providerPlugin;
    {
        Trace trace;Machinemodule m(factory(trace));m.model=modelMachinemodule;ready(m);
        for(auto& o:m.outputs)o.channels=1;
        Module provider;provider.model=&providerModel;m.leftExpander.module=&provider;
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        gorgon_sound::Packet packet;packet.moving=0;packet.transport=0;
        for(int f=0;f<1600;++f){
            args.frame=f;packet.frame=f;
            packet.moving=f>=4;packet.transport=f>=4?1:0;
            packet.lanes[0].gate=(f>=4&&f<8)||(f>=132&&f<136)?10.f:0;
            packet.lanes[0].velocity=1;
            packet.lanes[0].sound.machine=16;
            packet.lanes[0].sound.mask=f<100?1:0;
            packet.lanes[0].sound.values[0]=100;
            *static_cast<gorgon_sound::Packet*>(m.leftExpander.producerMessage)=packet;m.leftExpander.messageFlipRequested=true;
            m.process(args);flip(m);
            if(f%128==127)drain();
        }
        drain();
        {std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==2);assert(trace.hits[0].resolved[0]==100&&trace.hits[1].resolved[0]==11);}
        assert(m.outputs[Machinemodule::TRACKS].getChannels()==16&&m.underruns==0);
        // Actual external-jack precedence: linked high gates/locks must be ignored.
        m.inputs[Machinemodule::TRIG].channels=4;m.inputs[Machinemodule::P1_CV].channels=1;m.inputs[Machinemodule::P1_CV].setVoltage(1);
        for(int f=1600;f<2112;++f){args.frame=f;for(int i=0;i<4;++i)m.inputs[Machinemodule::TRIG].setVoltage((f==1610||f==1740)?10:0,i);m.process(args);if(f%128==127)drain();}
        drain();{std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==10);for(size_t i=2;i<trace.hits.size();++i)assert(trace.hits[i].resolved[0]==24);}
        // A new timeline while an external gate is high must not replay it.
        for(int i=0;i<4;++i)m.inputs[Machinemodule::TRIG].setVoltage(10,i);
        m.restart(44100);
        for(int i=0;i<256;++i){++args.frame;m.process(args);if(i%128==127)drain();}
        drain();{std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==10);}
        // Switching host rate starts a new epoch, clears old output and preserves finite voltages.
        args.sampleRate=96000;args.sampleTime=1.f/96000;for(int i=0;i<256;++i){++args.frame;m.process(args);if(i%128==127)drain();}
    }
    busTrig();std::cout<<"Bus TRIG mode: clocked bits, per-bit fallback, arming, velocity, saved mode"
#ifdef BYTEBUS_COUNTER
        <<", Counter -> Lookup -> Machinemodule in random order"
#endif
        <<" passed\n";
#ifdef GORGON_SOURCE
    modelGorgon->plugin=&providerPlugin;
#ifdef _WIN32
    _putenv_s("HYDRAPY_STUDIO_FILE","C:/nonexistent/machinemodule-interop-test");
#else
    setenv("HYDRAPY_STUDIO_FILE","/nonexistent/machinemodule-interop-test",1);
#endif
    for(bool reverse:{false,true}){
        Trace trace;Machinemodule receiver(factory(trace));receiver.model=modelMachinemodule;ready(receiver);
        Gorgon sender;sender.model=modelGorgon;sender.rightExpander.module=&receiver;receiver.leftExpander.module=&sender;
        for(auto& out:sender.outputs)out.channels=1;for(auto& out:receiver.outputs)out.channels=1;
        sender.haveSnapshot=true;auto& s=sender.snapshot;s.score.bpm=120;s.score.sounds[0].machine=16;
        s.score.patterns[0].hit[0][0]=true;s.score.patterns[0].velocity[0][0]=100;
        s.score.patterns[0].hit[0][1]=true;s.score.patterns[0].velocity[0][1]=100;
        s.score.patterns[0].locks[0][0].mask=1;s.score.patterns[0].locks[0][0].values[0]=99;
        s.playhead.valid=s.playhead.enabled=true;s.playhead.playing=false;s.playhead.transportRevision=0;s.clock.observe(0,0,0);
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        for(int f=0;f<16000;++f){
            args.frame=f;
            if(f==64){s.playhead.playing=true;s.playhead.anchorTime=64./44100;s.playhead.transportRevision=1;}
            if(reverse){receiver.process(args);sender.process(args);}else{sender.process(args);receiver.process(args);}flip(receiver);
            if(f%128==127)std::this_thread::sleep_for(std::chrono::microseconds(800));
        }
        drain();{std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==2);assert(trace.hits[0].resolved[0]==99&&trace.hits[1].resolved[0]==11);}
        assert(receiver.underruns==0);std::cout<<"Real Gorgon -> Machinemodule, "<<(reverse?"receiver first":"sender first")<<": packet/trigger/default restoration passed\n";
    }
    // Gorgon 2.3.1 transport changes (Gorgon spec/cv-semantics.md: retrigger, landing gate).
    // A seek that interrupts an open gate drops it for 1 ms; a seek heard after its row's
    // gate window closed still opens the landing row's gate. Each must reach the receiver
    // as exactly one hit, after the jump's panic, carrying the landing row's lock.
    for(bool reverse:{false,true}){
        Trace trace;Machinemodule receiver(factory(trace));receiver.model=modelMachinemodule;ready(receiver);
        Gorgon sender;sender.model=modelGorgon;sender.rightExpander.module=&receiver;receiver.leftExpander.module=&sender;
        for(auto& out:sender.outputs)out.channels=1;for(auto& out:receiver.outputs)out.channels=1;
        sender.haveSnapshot=true;auto& s=sender.snapshot;s.score.bpm=120;s.score.sounds[0].machine=16;
        for(int row:{0,4}){s.score.patterns[0].hit[0][row]=true;s.score.patterns[0].velocity[0][row]=100;s.score.patterns[0].locks[0][row].mask=1;}
        s.score.patterns[0].locks[0][0].values[0]=99;s.score.patterns[0].locks[0][4].values[0]=42;
        s.playhead.valid=s.playhead.enabled=true;s.playhead.playing=false;s.playhead.transportRevision=0;s.clock.observe(0,0,0);
        // 120 BPM at 44.1 kHz: a row is 5512.5 frames and its 50% gate 2756. Changes land on
        // 32-frame boundaries, where Gorgon polls its bridge. `late` is how many frames
        // before Rack heard it the change happened in Studio.
        auto seek=[&](int frame,double beat,int late){s.playhead.anchorBeat=beat;s.playhead.anchorTime=double(frame-late)/44100;++s.playhead.transportRevision;};
        Module::ProcessArgs args{};args.sampleRate=44100;args.sampleTime=1.f/44100;
        for(int f=0;f<16000;++f){
            args.frame=f;
            if(f==64){s.playhead.playing=true;s.playhead.anchorTime=64./44100;s.playhead.transportRevision=1;}
            if(f==1024)seek(f,1.,0);      // row 0's gate is open: interrupt it and land on row 4
            if(f==8160){drain();std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==2);}
            if(f==8192)seek(f,1.,4000);   // heard 0.73 rows late, past row 4's gate window
            if(reverse){receiver.process(args);sender.process(args);}else{sender.process(args);receiver.process(args);}flip(receiver);
            if(f%128==127)std::this_thread::sleep_for(std::chrono::microseconds(800));
        }
        drain();{std::lock_guard<std::mutex> guard(trace.mutex);
            if(trace.hits.size()!=3)std::cerr<<"transport changes: "<<trace.hits.size()<<" hits, expected 3\n";
            assert(trace.hits.size()==3);
            assert(trace.hits[0].resolved[0]==99&&trace.hits[1].resolved[0]==42&&trace.hits[2].resolved[0]==42);}
        assert(receiver.underruns==0);std::cout<<"Real Gorgon -> Machinemodule, "<<(reverse?"receiver first":"sender first")<<": interrupted and late seeks each play one landing hit\n";
    }
#endif
    contextSet(nullptr);delete context.engine;context.engine=nullptr;
    std::cout<<"module_test: real Rack API, CV capture, link, jack precedence, rate restart passed\n";
}
