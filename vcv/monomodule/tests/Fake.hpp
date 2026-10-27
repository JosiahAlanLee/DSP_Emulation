// SPDX-License-Identifier: AGPL-3.0-only
// A deterministic six-track backend: no OS, no emulator. A track sounds its last hit's P1 while its note is
// held, and records what it was given.
#pragma once
#include "Renderer.hpp"
#include <mutex>
#include <vector>
namespace mnm_rack {
struct Record {int64_t native=0;Hit hit;bool release=false;};
struct Trace {std::mutex mutex;std::vector<Record> events;int resets=0;};
class Fake:public Backend {
    Trace* trace;
    int64_t frame=0;
    float level[tracks]={};
public:
    explicit Fake(Trace* sink=nullptr):trace(sink){}
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
        for(auto& sample:out){
            std::fill(sample,sample+channels,0);
            for(int t=0;t<tracks;++t){sample[trackLeft+t]=sample[trackRight+t]=level[t];sample[0]+=level[t];sample[1]+=level[t];}
        }
        frame+=nativeFrames;
    }
};
}
