#include "../src/engine/LoopMutator.hpp"
#include <cassert>
#include <iostream>
#include <memory>
using namespace otis;
int main() {
    auto e=std::make_unique<Engine>(); LoopMutator mutator(*e);
    auto install=[&](int generation) {
        std::vector<int16_t> pcm(2048+(generation%3)*512,int16_t(1000+generation));
        for(size_t i=128;i<1024;++i) pcm[i]=int16_t(10000*std::sin(i*.1)+generation);
        e->install(pcm,4000+generation); e->controls.loop=1; e->trigger(0);
    };
    install(0);
    assert(mutator.request(128,1024,.4,1));
    for(int i=0;i<2000 && mutator.status()!=LoopMutator::Ready;++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(mutator.status()==LoopMutator::Ready);
    std::atomic<bool> done{false}; std::atomic<unsigned> copies{0};
    std::thread reader([&] {
        while(!done.load(std::memory_order_acquire)) {
            std::vector<int16_t> pcm; double rate; uint32_t revision; e->snapshot(pcm,rate,revision);
            int generation=int(rate)-4000;
            assert(generation>=0 && generation<100);
            assert(pcm.size()==size_t(2048+(generation%3)*512));
            assert(!(revision&1));
            for(size_t i=0;i<128;++i) assert(pcm[i]==1000+generation);
            for(size_t i=1024;i<pcm.size();++i) assert(pcm[i]==1000+generation);
            ++copies;
        }
    });
    for(int i=0;i<100000 && mutator.busy();++i) { mutator.service(); e->process(0); }
    assert(!mutator.busy() && mutator.lastOutcome()==LoopMutator::Applied);
    for(int generation=0;generation<100;++generation) {
        install(generation);
        for(int i=0;i<1200;++i) {
            if(i%200==0) mutator.request(128,1024,.4,1);
            mutator.service(); assert(std::isfinite(e->process(0)));
        }
    }
    done.store(true,std::memory_order_release); reader.join(); assert(copies>0);
    std::cout<<"PASS: "<<copies<<" coherent snapshots during worker rework, processing and 100 sample replacements\n";
}
