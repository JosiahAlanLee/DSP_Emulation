// Build against both headers to compare complete default-path host output.
#ifndef OTIS_ENGINE_HEADER
#define OTIS_ENGINE_HEADER "../src/engine/OtisEngine.hpp"
#endif
#include OTIS_ENGINE_HEADER
#include <memory>
#include <iostream>
#include <cstring>
int main() {
    uint64_t hash=1469598103934665603ULL;
    for(double host:{44100.,48000.,96000.}) for(int mode=0;mode<3;++mode) for(int filter=0;filter<4;++filter) {
        auto e=std::make_unique<otis::Engine>(); e->setHostRate(host);
        std::vector<int16_t> sample(4096); uint32_t rng=13;
        for(auto& x:sample) { rng=rng*1664525u+1013904223u; x=int16_t((rng>>16)&65535); }
        e->install(sample,otis::captureRate(5));
        auto& c=e->controls; c.engineMode=mode; c.filterMode=filter; c.loop=1+filter%2; c.gated=true;
        c.clockOctaves=mode==1?-.75f:0; c.interpBits=filter==3?0:9;
        for(int i=0;i<8;++i) { c.pitch[i]=(i-4)/12.f; c.k1v[i]=.2f+i*.1f; c.k2v[i]=.5; c.level[i]=1-i*.1f; e->trigger(i); }
        for(int i=0;i<12000;++i) {
            if(i==3000) e->trigger(0);
            if(i==5000) for(int v=0;v<4;++v) e->release(v);
            if(i==7500) e->startRecord();
            if(i==9500) e->stopRecord();
            float y=e->process(float(std::sin(i*.04)*.2)); uint32_t bits; std::memcpy(&bits,&y,4);
            hash^=bits; hash*=1099511628211ULL;
        }
        std::vector<int16_t> pcm; double rate; uint32_t revision; e->snapshot(pcm,rate,revision);
        for(auto x:pcm) { hash^=uint16_t(x); hash*=1099511628211ULL; }
    }
    std::cout<<std::hex<<hash<<"\n";
}
