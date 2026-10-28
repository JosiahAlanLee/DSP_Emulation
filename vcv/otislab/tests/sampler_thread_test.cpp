// UI SampleShare imports must not inspect audio-owned engine state without engineMutex.
#include "../src/OtisSampler.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance=nullptr;
int main() {
    OtisSampler m;
    dspemu::SampleShareV1 share{dspemu::SAMPLE_SHARE_MAGIC,dspemu::SAMPLE_SHARE_VERSION,nullptr,
        [](void*) -> uint32_t { return 2; },
        [](void*,int16_t* dst,size_t cap,double* rate,uint32_t* rev) -> size_t {
            if(dst) std::fill_n(dst,std::min(cap,size_t(64)),int16_t(123));
            *rate=48000; *rev=2; return 64;
        },
        [](void*,char* dst,size_t cap) { if(cap) std::snprintf(dst,cap,"test capture"); }};
    std::atomic<bool> stop{false}; std::atomic<size_t> processed{0};
    std::thread audio([&] {
        Module::ProcessArgs args{}; args.sampleRate=48000; args.sampleTime=1.f/48000;
        size_t n=0;
        while(!stop.load()) {
            m.params[OtisSampler::RECORD].setValue((n++%64)<32 ? 1.f : 0.f);
            m.process(args); ++processed;
        }
    });
    while(processed.load()<100) std::this_thread::yield();
    for(int i=0;i<100;++i) assert(m.importFrom(&share));
    stop=true; audio.join();
    std::cout<<"PASS: concurrent SampleShare imports and audio recording ("<<processed<<" frames)\n";
}
