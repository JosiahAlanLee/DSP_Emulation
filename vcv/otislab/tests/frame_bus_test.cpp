// Original frame selection, native stereo fades and exported byte tables, without Rack.
#include "../src/engine/OtisEngine.hpp"
#include "../src/engine/FrameTable.hpp"
#include "../../shared/ByteBus.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
static thread_local bool watch=false;
static thread_local size_t allocations=0;
void* operator new(size_t n) { if(watch) ++allocations; if(void* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
using namespace otis;
static void selection() {
    auto e=std::make_unique<Engine>(); auto& c=e->controls;
    c.windowMode=2; c.sizeOctaves.fill(1); c.frameBus=true;
    for(size_t count:{1u,2u,3u,8u,255u,256u,257u,1000u,524160u}) {
        e->frames=count*2; size_t previous=0;
        for(int b=0;b<256;++b) {
            c.frameWord=b; auto w=e->windowFor(0);
            size_t index=size_t(std::lround(double(b)*(count-1)/255));
            assert(w.active && w.end-w.begin==2 && w.begin==index*2 && w.end<=e->frames);
            assert(w.begin>=previous); previous=w.begin;
            if(count==256) assert(w.begin==size_t(b)*2);
            if(b==0) assert(w.begin==0);
            if(b==255) assert(w.end==e->frames);
        }
    }
    e->frames=8*256+17; c.sizeOctaves.fill(8); c.frameWord=255;
    assert(e->windowFor(0).end==2048); // incomplete tail omitted
    c.startv[1]=.25; c.endv[1]=.75;
    auto voice1=e->windowFor(0), voice2=e->windowFor(1);
    assert(voice1.begin!=voice2.begin && voice2.end<=size_t(.75*(e->frames-1))+1);
    c.frameWord=0xa5; assert(e->windowFor(0).begin==5*256);
    c.frameWord=0x5a; assert(e->windowFor(0).begin==2*256);
    c.position[0]=0; c.windowMode=1; assert(e->windowFor(0).begin==0); // SCAN ignores bus
    c.windowMode=0; assert(!e->windowFor(0).active);
    c.windowMode=2; c.frameBus=false; c.position[0]=1; assert(e->windowFor(0).begin==7*256);
}
static void crossfade() {
    for(double rate:{1000.,chipRates[0],chipRates[1],chipRates[2],192000.}) for(bool reverse:{false,true}) for(int loop:{0,1,2}) {
        auto e=std::make_unique<Engine>();
        std::vector<int16_t> left(3*256),right(left.size());
        for(size_t i=0;i<left.size();++i) { left[i]=i<256 ? 12000 : i<512 ? -12000 : 6000; right[i]=-left[i]; }
        auto image=stereoImage(left.data(),right.data(),left.size()); e->install(image,captureRate(5),left.size(),true);
        auto& c=e->controls; c.loop=loop; c.reverse=reverse; c.windowMode=2; c.frameBus=c.frameCrossfade=true;
        e->currentRate=rate; c.frameWord=0; e->trigger(0); auto& v=e->voices[0];
        v.accumulator=v.start+67*fracOne+512; v.frequency=4; v.loop=loop;
        uint32_t relative=v.accumulator-v.start;
        c.frameWord=128; e->bounds(v,0);
        assert(v.accumulator-v.start==relative && v.frameFadeRemaining==std::max(1,int(std::lround(rate*.002))));
        Voice reference=v; const int total=v.frameFadeTotal;
        watch=true;
        for(int tick=0;tick<total;++tick) {
            // Further requests wait for completion; the newest is retained in controls.
            c.frameWord=255; e->bounds(v,0); assert(v.start==(baseAddress+256)*fracOne);
            double blend=double(tick)/total; int32_t l=int32_t(std::lround(12000-24000*blend));
            int32_t r=0; bool ended=false;
            assert(v.tick(e->sample,e->frames,ended,nullptr,1,&r)==reference.filter(l));
            assert(r==reference.filter(-l,reference.poleR));
        }
        watch=false; assert(allocations==0 && v.frameFadeRemaining==0);
        e->bounds(v,0); assert(v.start==(baseAddress+512)*fracOne && v.frameFadeRemaining==total);
        c.frameCrossfade=false; e->bounds(v,0); assert(v.frameFadeRemaining==0);
        c.frameCrossfade=true; c.frameWord=0; e->bounds(v,0); assert(v.frameFadeRemaining==total);
        c.sizeOctaves[0]=7; e->bounds(v,0); assert(v.frameFadeRemaining==0); // length change cancels
    }
}
static void tables() {
    using namespace frame_table;
    std::vector<int16_t> left(260),right(260);
    for(int i=0;i<256;++i) { left[i+2]=int16_t((i-128)*256); right[i+2]=-16384; }
    auto t=resample(left,right,2,258,Channel::Left);
    for(int i=0;i<256;++i) assert(t[i]==i);
    assert(fingerprint(t)=="fnv1a64:4242dc5249c33625");
    for(auto b:resample(left,right,2,258,Channel::Right)) assert(b==64);
    auto mix=resample(left,right,2,258,Channel::Mix);
    for(int i=0;i<256;++i) assert(mix[i]==int(std::floor((i+64)/2.+.5)));
    // Periodic neighbor interpolation, no automatic level or DC changes.
    left={-32768,32767}; right.clear(); t=resample(left,right,0,2,Channel::Mix);
    assert(t[0]==0 && t[64]==128 && t[128]==255 && t[192]==128 && t[255]==2);
    left.assign(65536,1000); t=resample(left,right,0,left.size(),Channel::Left);
    for(auto b:t) assert(b==132);
    for(int test=0;test<5;++test) {
        bool rejected=false;
        try {
            resample(left,right,test==0 ? size_t(-1) : 0,
                     test==1 ? 1 : test==2 ? left.size()+1 : left.size(),test==3 ? Channel::Right : Channel::Left);
            if(test==4) { left.push_back(0); resample(left,right,0,left.size(),Channel::Left); }
        } catch(const std::runtime_error&) { rejected=true; }
        assert(rejected);
    }
}
int main() {
    selection(); crossfade(); tables();
    std::cout<<"PASS: exhaustive byte/frame selection, region/tail rules, offset retention, stereo crossfade/queued requests, zero tick allocations, periodic frame export and channel quantization\n";
}
