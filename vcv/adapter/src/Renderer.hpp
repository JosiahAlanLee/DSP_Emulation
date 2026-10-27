// SPDX-License-Identifier: AGPL-3.0-only
// The worker-side renderer for any emulated instrument: events onto the emulator's native blocks at
// 44.1 kHz, never early, then band-limited conversion to Rack's rate. Its resampler is the core's
// copy of Monomodule's (AGPL-3.0), which is why this directory is AGPL (vcv/adapter/README.md).
#pragma once
#include "Scheduling.hpp"
#include "ResamplerCompat.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace emu_rack {
// This interface keeps the emulator entirely off Rack's audio callback and lets
// the scheduling/CV/lock tests use a deterministic backend without any OS file.
struct Machine {int id=0;std::string name;std::array<std::string,params> labels;};
template<class L> class Backend {
public:
    virtual ~Backend()=default;
    virtual std::string description() const {return "Ready · SYN controls captured on each hit";}
    virtual std::vector<Machine> machines() const {return {};}
    virtual bool fire(const Hit&)=0; // false means an unavailable machine; do not guess another one
    virtual void panic()=0;
    virtual void render(float (&out)[L::nativeFrames][L::channels])=0;
};
template<class L> class Renderer {
    static constexpr int tracks=L::tracks, channels=L::channels, nativeFrames=L::nativeFrames;
    struct Pending {int64_t frame=0;Event event;};
    Backend<L>& backend;
    mnm::dsp::RateMap map;
    mnm::dsp::SincKernel kernel;
    std::array<mnm::dsp::History,channels> history;
    std::vector<float> scratch;
    std::array<Pending,512> pending{};
    size_t pendingRead=0,pendingWrite=0;
    uint64_t generation=0;
    int64_t origin=0,next=0,native=0;
    double rate=0;
    int lag=0;
    void reset(const InputBlock& block) {
        backend.panic();generation=block.generation;origin=next=block.start;native=0;rate=block.rate;
        map.set(44100,rate);kernel.design(44100,rate);
        lag=rate==44100?0:int(std::ceil((kernel.halfTaps()+nativeFrames)*rate/44100));
        scratch.resize(size_t(kernel.numTaps()));
        for(auto& h:history)h.prepare(8192);
        pendingRead=pendingWrite=0;
    }
public:
    uint64_t coalesced=0, unavailable=0;
    explicit Renderer(Backend<L>& engine):backend(engine){}
    int converterLatency() const {return lag;}
    void process(const InputBlock& block, OutputBlock<channels>& output) {
        if(!std::isfinite(block.rate)||block.rate<8000||block.rate>192000||block.count>maxEvents||block.overflow)
            throw std::runtime_error("Invalid or overflowing input block");
        if(block.generation!=generation||block.start!=next||block.rate!=rate)reset(block);
        output=OutputBlock<channels>();output.start=block.start;output.generation=block.generation;
        for(int i=0;i<block.count;++i){
            if(block.events[i].offset>=frames)throw std::runtime_error("Event outside input block");
            if(pendingWrite-pendingRead==pending.size())throw std::runtime_error("Pending event queue overflow");
            auto at=map.ceilAt(block.start-origin+block.events[i].offset);
            // Never fire early. The firmware's voice/control interface is its native block.
            auto boundary=((at+nativeFrames-1)/nativeFrames)*nativeFrames;
            pending[pendingWrite++%pending.size()]={boundary,block.events[i]};
        }
        auto available=map.floorAt(block.start-origin+frames);
        while(native+nativeFrames<=available){
            std::array<bool,tracks> fired{};
            while(pendingRead<pendingWrite&&pending[pendingRead%pending.size()].frame<=native){
                const auto& ev=pending[pendingRead++%pending.size()].event;
                if(ev.panic){backend.panic();fired.fill(false);}
                else if(ev.hit.velocity&&ev.hit.track<tracks){
                    if(fired[ev.hit.track])++coalesced;
                    fired[ev.hit.track]=true;
                    if(!backend.fire(ev.hit))++unavailable;
                }
            }
            float rendered[nativeFrames][channels];backend.render(rendered);
            for(int c=0;c<channels;++c){float x[nativeFrames];for(int f=0;f<nativeFrames;++f)x[f]=rendered[f][c];history[c].push(x,nativeFrames);}
            native+=nativeFrames;
        }
        for(int f=0;f<frames;++f){
            int64_t m=block.start-origin+f-lag;
            if(m<0)continue;
            auto pos=map.at(m);
            if(rate==44100){
                const float one=1;
                if(pos.index>=native)throw std::runtime_error("Native render underrun");
                for(int c=0;c<channels;++c)output.samples[f][c]=history[c].dot(pos.index,&one,1);
            }else{
                if(pos.index+kernel.halfTaps()>=native)throw std::runtime_error("Converter render underrun");
                const auto* taps=kernel.taps(pos,scratch.data());
                for(int c=0;c<channels;++c)output.samples[f][c]=history[c].dot(pos.index-kernel.halfTaps()+1,taps,kernel.numTaps());
            }
        }
        next=block.start+frames;
    }
};
} // namespace emu_rack
