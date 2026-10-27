// SPDX-License-Identifier: AGPL-3.0-only
// The shared renderer on the Monomachine's layout: hits and note-offs on 16-frame native blocks, never
// early; a note ends when its release arrives; six tracks on their own outputs.
#include "Fake.hpp"
#include <cassert>
#include <iostream>
using namespace mnm_rack;
int main(){
    for(double rate:{44100.,48000.,96000.}){
        Trace trace;Fake fake(&trace);Renderer render(fake);OutputBlock out;
        InputBlock block;block.generation=1;block.rate=rate;
        Event hit;hit.offset=5;hit.hit.track=4;hit.hit.mask=1;hit.hit.values[0]=127;hit.hit.note=69;block.append(hit);
        Event off;off.offset=90;off.release=true;off.hit.track=4;
        for(int i=0;i<20;++i){
            block.start=i*frames;
            if(i==3)block.append(off);
            render.process(block,out);block.count=0;
            for(const auto& f:out.samples)for(float v:f)assert(std::isfinite(v));
            if(i==2)assert(std::abs(out.samples[127][trackLeft+4]-1)<1e-4&&out.samples[127][trackLeft+3]==0&&out.samples[127][trackRight+4]>.99f);
        }
        mnm::dsp::RateMap map;map.set(44100,rate);
        auto onAt=((map.ceilAt(5)+15)/16)*16, offAt=((map.ceilAt(3*frames+90)+15)/16)*16;
        assert(trace.events.size()==2&&!trace.events[0].release&&trace.events[1].release);
        assert(trace.events[0].native==onAt&&trace.events[1].native==offAt);
        assert(trace.events[0].hit.note==69&&trace.events[1].hit.track==4);
        assert(std::abs(out.samples[127][trackLeft+4])<1e-4);   // released: silent
        std::cout<<rate<<" Hz: note on and off on 16-frame blocks, six-track outputs passed\n";
    }
    // A release for a track out of range is ignored; a velocity-0 hit plays nothing.
    Trace trace;Fake fake(&trace);Renderer renderer(fake);InputBlock b;b.generation=1;b.rate=44100;OutputBlock out;
    Event e;e.release=true;e.hit.track=9;b.append(e);Event quiet;quiet.hit.velocity=0;b.append(quiet);renderer.process(b,out);
    assert(trace.events.empty());
    std::cout<<"renderer_test: Monomachine layout passed\n";
}
