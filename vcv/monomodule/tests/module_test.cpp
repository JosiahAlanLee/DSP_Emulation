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
int main(){
    Context context;context.engine=new engine::Engine;contextSet(&context);
    Plugin receiverPlugin;receiverPlugin.slug="MonomoduleRack";receiverPlugin.version="2.0.0";modelMonomodule->plugin=&receiverPlugin;
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
