// SPDX-License-Identifier: AGPL-3.0-only
// The Monomachine on the shared adapter: six of shnolk's Monomodule voices (MonoVoice, one emulated
// DSP56300 per track, as Monomodule Six runs them) from one OS file. Runs on the worker thread only.
#pragma once
#include "Renderer.hpp"
#include "Machines.hpp"
#include "Tracks.hpp"
#include "MonoVoice.h"
#include "firmware/Firmware.h"
#include <memory>

namespace mnm_rack {
// Play every machine through a note and its release on `voice`, FX machines on noise, so the DSP's JIT
// compiles each one here, at load, and not in the block where a track first plays it: a first use costs
// up to 24 ms against a 0.36 ms block. Each track's DSP compiles its own code, so every voice runs this.
inline void prewarm(mnm::MonoVoice& voice) {
    float l[nativeFrames],r[nativeFrames],noise[nativeFrames];
    uint32_t seed=1;
    auto& host=voice.host();
    for(auto m:machineOrder){
        const bool fx=mnm::host::isFxMachine(m);
        host.setMachine(m);
        host.setRouting(fx?mnm::host::dspInputBits(mnm::host::FxInput::Neighbor):0u);
        host.noteOn(60);
        for(int b=0;b<400;++b){
            if(b==300&&!fx)host.noteOff();
            if(fx){
                for(auto& x:noise){seed=seed*1664525u+1013904223u;x=float(int32_t(seed))*(.25f/2147483648.f);}
                voice.processFx(noise,noise,l,r,nativeFrames);
            }
            else voice.process(l,r,nativeFrames);
        }
    }
    host.noteOff();
    host.setMachine(MnmMachine::GND);
    host.setRouting(0);
    for(int b=0;b<200;++b)voice.process(l,r,nativeFrames);
}

class Monomachine final:public Backend {
    std::unique_ptr<mnm::fw::Firmware> firmware;   // every voice keeps a reference: it outlives them
    std::array<std::unique_ptr<mnm::MonoVoice>,tracks> voices;
    Tracks<mnm::MonoVoice> state;   // declared after the voices, which it points to
public:
    explicit Monomachine(const std::string& path)
        :firmware(std::make_unique<mnm::fw::Firmware>(mnm::fw::loadFirmware(path))) {
        for(int t=0;t<tracks;++t){
            voices[size_t(t)]=std::make_unique<mnm::MonoVoice>(*firmware);
            auto& host=voices[size_t(t)]->host();
            host.setOutBuses(1);   // OUT BUS AB: the main pair
            host.setLevel(100);
            prewarm(*voices[size_t(t)]);
            state.attach(t,*voices[size_t(t)]);
            voices[size_t(t)]->warmUp(64);   // the same warm-up on every voice keeps their frame counters in step
        }
    }
    std::string description() const override {
        auto version=firmware->version;version.erase(0,version.find_first_not_of(' '));
        return "Ready · OS "+version+" · six DSP engines";
    }
    std::vector<Machine> machines() const override {
        std::vector<Machine> result;
        for(auto m:machineOrder){
            const auto* def=mnm::host::machineDef(m);
            Machine machine;machine.id=int(m);machine.name=def->name;
            for(int k=0;k<params;++k)machine.labels[size_t(k)]=def->labels[k];
            result.push_back(machine);
        }
        return result;
    }
    bool fire(const Hit& hit) override {return state.fire(hit);}
    void release(int track) override {state.release(track);}
    void configure(const uint8_t (&settings)[emu_rack::trackSettings]) override {state.configure(settings);}
    void input(const float* frames) override {state.input(frames);}
    void initialize() override {state.initialize();}
    void panic() override {state.panic();}
    void render(float (&out)[nativeFrames][channels]) override {state.render(out);}
};
} // namespace mnm_rack
