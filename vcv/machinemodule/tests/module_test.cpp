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
#include <cassert>
#include <iostream>
Plugin* pluginInstance=nullptr;
using namespace md_rack;
static void ready(Machinemodule& m){m.worker.load("fake");auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!m.worker.ready){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static void flip(Module& receiver){auto& e=receiver.leftExpander;if(e.messageFlipRequested){std::swap(e.producerMessage,e.consumerMessage);e.messageFlipRequested=false;}}
static auto factory(Trace& trace){return [&trace](const std::string&,const std::string&){return std::make_unique<Fake>(&trace);};}
static void drain(){std::this_thread::sleep_for(std::chrono::milliseconds(5));}
int main(){
    Context context;context.engine=new engine::Engine;contextSet(&context);
    Plugin receiverPlugin;receiverPlugin.slug="MachinemoduleRack";receiverPlugin.version="2.0.0";modelMachinemodule->plugin=&receiverPlugin;
    Plugin providerPlugin;providerPlugin.slug="GorgonBridge";providerPlugin.version="2.3.0";
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
        // Switching host rate starts a new epoch, clears old output and preserves finite voltages.
        args.sampleRate=96000;args.sampleTime=1.f/96000;for(int i=0;i<256;++i){++args.frame;m.process(args);if(i%128==127)drain();}
    }
#ifdef GORGON_SOURCE
    modelGorgon->plugin=&providerPlugin;
    setenv("HYDRAPY_STUDIO_FILE","/nonexistent/machinemodule-interop-test",1);
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
#endif
    contextSet(nullptr);delete context.engine;context.engine=nullptr;
    std::cout<<"module_test: real Rack API, CV capture, link, jack precedence, rate restart passed\n";
}
