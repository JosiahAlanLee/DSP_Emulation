// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Renderer.hpp"
#include "MdEngine.h"
#include "Firmware.h"
#include "SampleBank.hpp"
namespace md_rack {
class Machinedrum final:public Backend {
    md::fw::Firmware firmware; // Engine retains a reference: this must outlive it.
    std::unique_ptr<md::engine::Engine> engine;
    uint8_t empty=0;
    bool hasSamples=false;
    bool offered(const md::engine::MachineInfo& machine) const {
        const auto group=machine.name.substr(0,3);
        return !machine.name.empty()&&group!="RAM"&&group!="INP"&&group!="MID"&&group!="CTR"&&(group!="ROM"||hasSamples);
    }
public:
    explicit Machinedrum(const std::string& path,const std::string& samples=""):firmware(md::fw::loadFirmware(path)) {
        auto container=md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(path)));
        if(container.sections.empty())throw std::runtime_error("OS has no ColdFire image");
        engine=std::make_unique<md::engine::Engine>(firmware,std::move(container.sections.at(0).data));
        for(const auto& machine:engine->os().machines())if(machine.name=="GND--"||machine.name=="GND --")empty=machine.id;
        if(!samples.empty()){
            for(const auto& record:readSampleBank(samples))engine->voices().writeP(record.address,record.words.data(),record.words.size());
            hasSamples=true;
        }
        // Warm startup on the worker, before any host timeline begins.
        md::engine::Engine::Output warm;
        for(int b=0;b<40;++b)if(!engine->render(warm))throw std::runtime_error(engine->fault());
        auto& host=engine->host();
        for(int t=0;t<tracks;++t){host.setLevel(t,100);host.setParam(t,17,100);host.setRouting(t,6);}
    }
    std::string description() const override {return hasSamples?"Ready · ROM sample bank loaded":"Ready · synthesis only (ROM sample bank not loaded)";}
    std::vector<Machine> machines() const override {
        std::vector<Machine> result;
        for(const auto& m:engine->os().machines())if(offered(m))result.push_back({m.id,m.name,m.params});
        return result;
    }
    bool fire(const Hit& hit) override {
        const auto* machine=engine->os().machine(uint8_t(hit.machine));
        if(!machine||!offered(*machine))return false;
        auto values=resolve(hit,machine->defaults);
        auto& host=engine->host();
        host.setTempo(hit.bpm);
        // setMachine establishes native SYN defaults. trigger copies all raw
        // controls into current firmware work values immediately, bypassing slew.
        host.setMachine(hit.track,uint8_t(hit.machine));
        for(int i=0;i<params;++i)host.setParam(hit.track,i,values[i]);
        host.trigger(hit.track,hit.velocity);
        return true;
    }
    void panic() override {
        for(int t=0;t<tracks;++t){engine->host().setMachine(t,empty);engine->host().trigger(t,1);}
    }
    void render(float (&out)[32][channels]) override {
        md::engine::Engine::Output block;
        if(!engine->render(block))throw std::runtime_error(engine->fault());
        constexpr float scale=1.f/8388608.f;
        for(int f=0;f<32;++f){
            out[f][0]=block.mix.main[f][0]*scale;out[f][1]=block.mix.main[f][1]*scale;
            for(int t=0;t<tracks;++t)out[f][2+t]=block.tracks[t][f]*scale;
        }
    }
};
} // namespace md_rack
