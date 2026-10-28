// SPDX-License-Identifier: AGPL-3.0-only
// A deterministic six-track backend: no OS, no emulator. A track sounds its last hit's P1 while its note is
// held, and records what it was given. Track 6 plays INP A back, for the input path.
#pragma once
#include "Renderer.hpp"
#include "Tracks.hpp"
#include "host/HostModel.h"
#include <mutex>
#include <vector>
namespace mnm_rack {
struct Record {int64_t native=0;Hit hit;bool release=false;};
struct Trace {std::mutex mutex;std::vector<Record> events;int resets=0;};
class Fake:public Backend {
    Trace* trace;
    int64_t frame=0;
    float level[tracks]={}, inA[nativeFrames]={};
public:
    uint8_t settings[emu_rack::trackSettings]={};
    explicit Fake(Trace* sink=nullptr):trace(sink){}
    void configure(const uint8_t (&s)[emu_rack::trackSettings]) override {std::copy(s,s+emu_rack::trackSettings,settings);}
    void input(const float* frames) override {for(int f=0;f<nativeFrames;++f)inA[f]=frames[f*inputs];}
    bool fire(const Hit& hit) override {
        level[hit.track]=resolve(hit,{11,22,33,44,55,66,77,88})[0]/127.f;
        if(trace){std::lock_guard<std::mutex> guard(trace->mutex);trace->events.push_back({frame,hit,false});}
        return hit.machine!=191;
    }
    void release(int track) override {
        level[track]=0;
        if(trace){Hit h;h.track=uint8_t(track);std::lock_guard<std::mutex> guard(trace->mutex);trace->events.push_back({frame,h,true});}
    }
    void panic() override {std::fill(level,level+tracks,0);if(trace){std::lock_guard<std::mutex> guard(trace->mutex);++trace->resets;}}
    void render(float (&out)[nativeFrames][channels]) override {
        for(int f=0;f<nativeFrames;++f){
            auto& sample=out[f];
            std::fill(sample,sample+channels,0);
            for(int t=0;t<tracks;++t){sample[trackLeft+t]=sample[trackRight+t]=level[t];sample[0]+=level[t];sample[1]+=level[t];}
            sample[trackLeft+5]=inA[f];
        }
        frame+=nativeFrames;
    }
};
// A voice with the real host model and no DSP, for Tracks: it reads the note word of each block the host
// builds, as the DSP does. A synth machine sounds 0.25 while its note is on (GND is silent); an FX machine
// passes its input while it is trigged, as an FX machine on the hardware needs a trig to pass audio.
struct HostVoice {
    mnm::host::HostModel model;
    bool gate=false;
    mnm::host::HostModel& host() {return model;}
    void step() {
        const auto& w=model.nextBlock().w;
        if(w[mnm::host::NoteTrig]&1u)gate=true;
        else if(w[mnm::host::NoteTrig]&2u)gate=false;
    }
    void process(float* l,float* r,int n) {
        step();const float v=gate&&model.machine()!=MnmMachine::GND?.25f:0.f;
        std::fill(l,l+n,v);std::fill(r,r+n,v);
    }
    void processFx(const float* inL,const float* inR,float* l,float* r,int n) {
        step();
        for(int f=0;f<n;++f){l[f]=gate?inL[f]:0.f;r[f]=gate?inR[f]:0.f;}
    }
};
}
