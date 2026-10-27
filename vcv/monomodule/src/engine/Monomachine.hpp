// SPDX-License-Identifier: AGPL-3.0-only
// The Monomachine on the shared adapter: six of shnolk's Monomodule voices (MonoVoice, one emulated
// DSP56300 per track, as Monomodule Six runs them) from one OS file. Runs on the worker thread only.
#pragma once
#include "Renderer.hpp"
#include "Machines.hpp"
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
    std::array<MnmMachine,tracks> current{};
    std::array<bool,tracks> fx{};
    std::array<uint8_t,tracks> source{};   // FxSource per track
    float left[tracks][nativeFrames]={}, right[tracks][nativeFrames]={}, silence[nativeFrames]={};
    float inA[nativeFrames]={}, inB[nativeFrames]={};   // INP A and INP B at 44.1 kHz, this block
    void route(int t) {
        auto in=source[size_t(t)]==INP_A?mnm::host::FxInput::InpA:source[size_t(t)]==INP_B?mnm::host::FxInput::InpB:
                source[size_t(t)]==INP_AB?mnm::host::FxInput::InpAB:mnm::host::FxInput::Neighbor;
        voices[size_t(t)]->host().setRouting(fx[size_t(t)]?mnm::host::dspInputBits(in):0u);
    }
    void assign(int t,MnmMachine m) {
        auto& host=voices[size_t(t)]->host();
        host.setMachine(m);   // also loads the AMP/FILT/EFX page defaults, as a machine assign does
        current[size_t(t)]=m;fx[size_t(t)]=mnm::host::isFxMachine(m);
        route(t);
        if(fx[size_t(t)])host.noteOn(60);   // an FX machine must be trigged to pass audio
    }
public:
    explicit Monomachine(const std::string& path)
        :firmware(std::make_unique<mnm::fw::Firmware>(mnm::fw::loadFirmware(path))) {
        for(int t=0;t<tracks;++t){
            voices[size_t(t)]=std::make_unique<mnm::MonoVoice>(*firmware);
            auto& host=voices[size_t(t)]->host();
            host.setOutBuses(1);   // OUT BUS AB: the main pair
            host.setLevel(100);
            prewarm(*voices[size_t(t)]);
            assign(t,MnmMachine::GND);
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
    bool fire(const Hit& hit) override {
        const auto* def=machineDef(hit.machine);
        if(!def)return false;
        const int t=hit.track;
        auto& host=voices[size_t(t)]->host();
        if(def->machine!=current[size_t(t)])assign(t,def->machine);
        auto values=resolve(hit,def->defaults);
        for(int k=0;k<params;++k)host.setParam(mnm::host::Page::SYN,k,values[size_t(k)]);
        host.setBpm(hit.bpm);
        // A note's machine and locks apply at its trig, as the sequencer's locks do, not by the host's
        // knob slew: from GND (TUNE 0) a first note would otherwise start 40 cents flat.
        host.settle();
        host.noteOn(hit.note>=0?hit.note:60);
        return true;
    }
    void release(int track) override {if(!fx[size_t(track)])voices[size_t(track)]->host().noteOff();}
    void configure(const uint8_t (&settings)[emu_rack::trackSettings]) override {
        for(int t=0;t<tracks;++t){
            uint8_t s=std::min<uint8_t>(settings[t],INP_AB);
            if(s!=source[size_t(t)]){source[size_t(t)]=s;route(t);}
        }
    }
    void input(const float* frames) override {
        for(int f=0;f<nativeFrames;++f){inA[f]=frames[f*inputs];inB[f]=frames[f*inputs+1];}
    }
    void panic() override {
        for(int t=0;t<tracks;++t){voices[size_t(t)]->host().noteOff();assign(t,MnmMachine::GND);}
    }
    // Tracks render in index order, so an FX track on NEIBOR reads the track before it as rendered this
    // block. On INP A/B the DSP takes INP A as its left input and INP B as its right; the routing bits
    // choose which it hears, as on the hardware.
    void render(float (&out)[nativeFrames][channels]) override {
        for(int t=0;t<tracks;++t){
            if(fx[size_t(t)]){
                const bool neighbour=source[size_t(t)]==NEIBOR;
                const float* inL=neighbour?(t?left[t-1]:silence):inA;
                const float* inR=neighbour?(t?right[t-1]:silence):inB;
                voices[size_t(t)]->processFx(inL,inR,left[t],right[t],nativeFrames);
            }
            else voices[size_t(t)]->process(left[t],right[t],nativeFrames);
        }
        for(int f=0;f<nativeFrames;++f){
            float l=0,r=0;
            for(int t=0;t<tracks;++t){
                out[f][trackLeft+t]=left[t][f];out[f][trackRight+t]=right[t][f];
                l+=left[t][f];r+=right[t][f];
            }
            out[f][0]=l;out[f][1]=r;
        }
    }
};
} // namespace mnm_rack
