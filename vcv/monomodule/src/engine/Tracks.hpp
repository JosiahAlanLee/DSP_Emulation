// SPDX-License-Identifier: AGPL-3.0-only
// The Monomachine's six tracks on any voice with MonoVoice's interface (host(), process(), processFx()):
// each track's machine, whether it is an FX machine, and its FX input. Monomachine runs it on Monomodule's
// voices; module_test runs it on voices with the real host model and no DSP, so it needs no OS.
#pragma once
#include "Renderer.hpp"
#include "Machines.hpp"
#include <algorithm>

namespace mnm_rack {
template<class Voice> class Tracks {
    std::array<Voice*,tracks> voices{};
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
    // Track t plays on `voice` (which must outlive this), starting on GND.
    void attach(int t,Voice& voice) {voices[size_t(t)]=&voice;assign(t,MnmMachine::GND);}
    MnmMachine machine(int t) const {return current[size_t(t)];}
    bool fire(const Hit& hit) {
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
    // An FX machine's trig is what lets it pass audio, so neither a gate's fall nor a panic ends it.
    void release(int track) {if(!fx[size_t(track)])voices[size_t(track)]->host().noteOff();}
    void configure(const uint8_t (&settings)[emu_rack::trackSettings]) {
        for(int t=0;t<tracks;++t){
            uint8_t s=std::min<uint8_t>(settings[t],INP_AB);
            if(s!=source[size_t(t)]){source[size_t(t)]=s;route(t);}
        }
    }
    void input(const float* frames) {
        for(int f=0;f<nativeFrames;++f){inA[f]=frames[f*inputs];inB[f]=frames[f*inputs+1];}
    }
    // A stop, seek, RESET or restart ends every note and nothing else, as on the hardware: each track keeps
    // its machine, its settings and its FX input, and an FX track keeps processing that input.
    void panic() {for(int t=0;t<tracks;++t)release(t);}
    // Tracks render in index order, so an FX track on NEIBOR reads the track before it as rendered this
    // block. On INP A/B the DSP takes INP A as its left input and INP B as its right; the routing bits
    // choose which it hears, as on the hardware.
    void render(float (&out)[nativeFrames][channels]) {
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
