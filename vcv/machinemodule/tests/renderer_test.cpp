// SPDX-License-Identifier: AGPL-3.0-only
#include "Fake.hpp"
#include <cassert>
#include <iostream>
using namespace md_rack;
struct Tone:Backend{
    int64_t n=0;double frequency;
    explicit Tone(double f):frequency(f){}
    bool fire(const Hit&)override{return true;}void panic()override{n=0;}
    void render(float (&out)[32][channels])override{for(auto& frame:out){float v=float(std::sin(6.283185307179586*frequency*n++/44100));for(auto& x:frame)x=v;}}
};
int main(){
    for(double rate:{8000.,22050.,44100.,48000.,48000.5,96000.,192000.}){
        Trace trace;Fake fake(&trace);Renderer render(fake);OutputBlock out;
        InputBlock block;block.generation=1;block.rate=rate;
        Event hit;hit.offset=17;hit.hit.mask=1;hit.hit.values[0]=127;block.append(hit);
        for(int i=0;i<20;++i){block.start=i*frames;render.process(block,out);block.count=0;for(const auto& frame:out.samples)for(float v:frame)assert(std::isfinite(v));}
        mnm::dsp::RateMap map;map.set(44100,rate);auto expected=((map.ceilAt(17)+31)/32)*32;
        assert(trace.hits.size()==1&&trace.hits[0].native==expected);
        assert(expected>=map.ceilAt(17)&&expected-map.ceilAt(17)<32);
        assert(std::abs(out.samples[127][2]-1)<.0001); // correct track, continuous DC across block boundaries
        assert(out.samples[127][3]==0);
        block.start+=256;render.process(block,out);assert(trace.resets==2); // gap cuts previous voices; no late replay
        std::cout<<rate<<" Hz: trigger quantization, contiguous conversion and gap recovery passed\n";
    }
    // A high-frequency native tone must not alias into a lower-rate Rack host.
    for(double frequency:{1000.,16000.}){
        Tone tone(frequency);Renderer renderer(tone);InputBlock input;input.generation=1;input.rate=22050;OutputBlock output;
        double sum=0;size_t n=0;
        for(int b=0;b<100;++b){input.start=b*frames;renderer.process(input,output);if(b>10)for(const auto& f:output.samples){sum+=f[0]*f[0];++n;}}
        double rms=std::sqrt(sum/n);if(frequency==1000)assert(rms>.70&&rms<.72);else assert(rms<.0001);
    }
    Trace trace;Fake fake(&trace);Renderer renderer(fake);InputBlock b;b.generation=1;b.rate=44100;
    Event e;e.offset=0;e.hit.mask=1;e.hit.values[0]=100;b.append(e);b.append(e);OutputBlock out;renderer.process(b,out);assert(renderer.coalesced==1);
    b.start=128;b.count=0;e.offset=0;e.hit.mask=0;b.append(e);renderer.process(b,out);assert(out.samples[0][2]==11.f/127); // unlocked -> native default
    b.start=256;b.events[0].hit.machine=191;renderer.process(b,out);assert(renderer.unavailable==1);
    std::cout<<"renderer_test: per-hit restoration, coalescing counts, unavailable IDs and anti-alias rejection passed\n";
}
