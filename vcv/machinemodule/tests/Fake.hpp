// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Renderer.hpp"
#include <mutex>
namespace md_rack {
struct Record {int64_t native=0;Hit hit;std::array<int,8> resolved{};};
struct Trace {std::mutex mutex;std::vector<Record> hits;int resets=0;};
class Fake:public Backend {
    Trace* trace;
    int64_t frame=0;
    float level[16]={};
public:
    explicit Fake(Trace* sink=nullptr):trace(sink){}
    bool fire(const Hit& hit) override {
        auto resolved=resolve(hit,{11,22,33,44,55,66,77,88});
        level[hit.track]=resolved[0]/127.f;
        if(trace){std::lock_guard<std::mutex> guard(trace->mutex);trace->hits.push_back({frame,hit,resolved});}
        return hit.machine!=191;
    }
    void panic() override {std::fill(level,level+16,0);if(trace){std::lock_guard<std::mutex> guard(trace->mutex);++trace->resets;}}
    void render(float (&out)[32][channels]) override {
        for(auto& sample:out){std::fill(sample,sample+channels,0);for(int t=0;t<16;++t){sample[2+t]=level[t];sample[t%2]+=level[t];}}
        frame+=32;
    }
};
}
