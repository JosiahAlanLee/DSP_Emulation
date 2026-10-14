#include "../src/engine/LoopMutator.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <new>
#include <cstdlib>
static thread_local bool watch=false;
static thread_local size_t allocations=0;
void* operator new(size_t n) { if(watch) ++allocations; if(void* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
using namespace otis;
static std::unique_ptr<Engine> makeEngine(size_t n=4096) {
    auto e=std::make_unique<Engine>();
    std::vector<int16_t> pcm(n);
    for(size_t i=0;i<n;++i) pcm[i]=int16_t(12000*std::sin(2*pi*i/113)+5000*std::cos(2*pi*i/31));
    e->install(pcm,captureRate(5)); e->controls.loop=1; return e;
}
static void waitReady(LoopMutator& m) {
    for(int i=0;i<2000 && m.status()!=LoopMutator::Ready;++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(m.status()==LoopMutator::Ready);
}
int main() {
    {
        auto e=makeEngine(1024); e->controls.windowMode=1; e->controls.sizeOctaves.fill(8);
        for(float pos:{0.f,.5f,1.f}) { e->controls.position[0]=pos; auto w=e->windowFor(0); assert(w.end-w.begin==256); assert(w.begin==size_t(std::lround(pos*768))); }
        e->controls.position[0]=0; e->trigger(0); auto& v=e->voices[0];
        v.accumulator=v.start+17*fracOne+123;
        e->controls.position[0]=1; e->bounds(v,0); assert(v.accumulator==v.start+17*fracOne+123);
        e->controls.windowMode=2;
        e->controls.position[0]=.4f; auto w=e->windowFor(0); assert(w.begin==256 && w.end==512);
        e->controls.position[0]=1; w=e->windowFor(0); assert(w.begin==768 && w.end==1024);
        e->controls.startv[1]=.25; e->controls.endv[1]=.75; e->controls.position[1]=1;
        w=e->windowFor(1); assert(w.end-w.begin==256 && w.begin==511 && w.end==767);
        // Endpoints, short regions, malformed controls and region restriction.
        for(int mode:{1,2}) for(float size:{1.f,8.f,16.f,NAN,INFINITY}) for(float p:{-100.f,0.f,.5f,1.f,100.f,NAN}) {
            e->controls.windowMode=mode; e->controls.sizeOctaves[0]=size; e->controls.position[0]=p;
            w=e->windowFor(0); assert(w.end>w.begin && w.end<=e->frames && w.end-w.begin>=2);
        }
    }
    {
        auto e=makeEngine(16); auto& v=e->voices[0]; v.window=true; v.running=true; v.loop=1;
        v.start=baseAddress*fracOne; v.end=(baseAddress+4)*fracOne; v.accumulator=v.start; v.frequency=fracOne;
        bool ended=false;
        for(int i=0;i<4;++i) v.tick(e->sample,e->frames,ended);
        assert(v.accumulator==v.start && ended); // exactly four samples per forward loop
        v.accumulator=v.end-fracOne; v.reverse=true; ended=false;
        for(int i=0;i<4;++i) v.tick(e->sample,e->frames,ended);
        assert(v.accumulator==v.end-fracOne && ended);
        v.accumulator=v.start; v.reverse=false; v.loop=2;
        const int expected[]={1,2,3,2,1,0};
        for(int i:expected) { v.tick(e->sample,e->frames,ended); assert(v.accumulator==v.start+i*fracOne); }
        e->sample.pcm[0]=2000; e->sample.pcm[3]=1000; e->sample.pcm[4]=-20000;
        v=Voice(); v.running=v.window=true; v.loop=1; v.start=baseAddress*fracOne; v.end=(baseAddress+4)*fracOne;
        v.accumulator=(baseAddress+3)*fracOne+fracOne/2;
        Voice reference=v; int32_t expectedFiltered=reference.filter(1500);
        assert(v.tick(e->sample,e->frames,ended)==expectedFiltered); // interpolation wraps inside frame
        for(int loop:{0,1,2}) for(bool reverse:{false,true}) {
            v=Voice(); v.window=v.running=true; v.loop=loop; v.reverse=reverse;
            v.start=baseAddress*fracOne; v.end=(baseAddress+2)*fracOne; v.accumulator=v.start; v.frequency=131068;
            for(int i=0;i<100;++i) { v.tick(e->sample,e->frames,ended); if(v.running) assert(v.accumulator>=v.start && v.accumulator<v.end); }
        }
    }
    {
        std::vector<int16_t> source(4096);
        for(size_t i=0;i<source.size();++i) source[i]=int16_t(20000*std::sin(i*.074));
        auto a=source,b=source;
        assert(!reworkLoop(a,100,4000,.5,0,1) && a==source);
        assert(reworkLoop(a,128,2048,.7,1,1234));
        assert(reworkLoop(b,128,2048,.7,1,1234) && a==b);
        assert(std::equal(a.begin(),a.begin()+128,source.begin()));
        assert(std::equal(a.begin()+2048,a.end(),source.begin()+2048));
        assert(a[128]==a[2047]); // full-wet seam endpoints agree
        auto c=source; reworkLoop(c,128,2048,.7,1,1235); assert(c!=a);
        for(size_t n:{2u,3u,7u,31u,256u}) for(float smooth:{0.f,.5f,1.f}) {
            std::vector<int16_t> tiny(n,32767); assert(!reworkLoop(tiny,0,n,smooth,1,5));
            for(auto v:tiny) assert(v==32767);
        }
        assert(!reworkLoop(a,size_t(-1),a.size(),.5,1,1));
        auto rms=[](const std::vector<int16_t>& p,size_t b,size_t e) {
            double s=0; for(size_t i=b;i<e;++i) s+=double(p[i])*p[i]; return std::sqrt(s/double(e-b));
        };
        for(float amount:{.25f,.65f,1.f}) { // repeated generations keep their level
            auto g=source; double reference=rms(source,0,1024);
            for(int generation=1;generation<=8;++generation) {
                reworkLoop(g,0,1024,.2f+generation*.08f,amount,100+generation);
                double dB=20*std::log10(rms(g,0,1024)/reference);
                assert(dB>-1 && dB<1);
            }
        }
    }
    {
        auto e=makeEngine(); LoopMutator worker(*e); e->trigger(0);
        auto original=e->sample.pcm; uint32_t revision=e->sequence();
        assert(worker.request(512,2048,.6,1)); assert(!worker.request(0,256,.2,1));
        waitReady(worker);
        {
            std::unique_lock<std::mutex> pin(e->sampleMutex);
            watch=true;
            for(int i=0;i<2000;++i) { worker.service(); assert(std::isfinite(e->process(0))); }
            watch=false;
            assert(worker.status()==LoopMutator::Ready && e->sequence()==revision);
        }
        watch=true; worker.service();
        assert(worker.status()==LoopMutator::Fading && worker.lastOutcome()==LoopMutator::Applied);
        assert(e->sequence()==revision+2 && e->voices[0].running);
        assert(std::equal(e->sample.pcm.begin(),e->sample.pcm.begin()+512,original.begin()));
        for(int i=0;i<500;++i) { e->process(0); worker.service(); }
        watch=false; assert(!worker.busy() && !e->fadingPcm && allocations==0);
        // Stale result cannot overwrite a new load.
        assert(worker.request(0,2048,.3,1)); waitReady(worker);
        std::vector<int16_t> replacement(128,1234); e->install(replacement,captureRate(5));
        worker.service(); assert(!worker.busy() && worker.lastOutcome()==LoopMutator::SourceChanged);
        assert(e->frames==128 && e->sample.pcm[0]==1234);
        auto cancelEngine=makeEngine(); LoopMutator canceled(*cancelEngine);
        uint32_t before=cancelEngine->sequence();
        assert(canceled.request(0,1024,.5,1)); waitReady(canceled); canceled.cancel(); canceled.service();
        assert(canceled.lastOutcome()==LoopMutator::Canceled && cancelEngine->sequence()==before);
        // Recording wins over a completed background result.
        auto fresh=makeEngine(); LoopMutator another(*fresh);
        assert(another.request(0,2048,.2,1)); waitReady(another); fresh->startRecord(); another.service();
        assert(!another.busy() && another.lastOutcome()==LoopMutator::SourceChanged);
        assert(!another.request(0,1024,.2,1)); fresh->stopRecord();
    }
    std::cout<<"PASS: fixed-size scan, frame selection, phase preservation, half-open forward/reverse/ping loops, frame interpolation, deterministic bounded rework, stale/capture rejection, deferred publication, live crossfade, zero audio-thread allocations\n";
}
