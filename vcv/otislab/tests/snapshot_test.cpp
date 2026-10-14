#include "../src/engine/OtisEngine.hpp"
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>

int main() {
    using namespace otis;
    // Pin a finished sample exactly as a slow snapshot would. The processing
    // thread must keep playing, defer publication and retain a queued record start.
    {
        auto e=std::make_unique<Engine>();
        std::vector<int16_t> initial(30000,8000); e->install(initial,22050);
        e->controls.loop=1; e->trigger(0);
        for(int i=0;i<500;i++) e->process(0);
        std::atomic<bool> held{false},release{false};
        std::thread reader([&] {
            std::lock_guard<std::mutex> lock(e->sampleMutex);
            held.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
        while(!held.load(std::memory_order_acquire)) std::this_thread::yield();
        e->startRecord(); for(int i=0;i<4800;i++) e->process(.125f);
        size_t finishedFrames=e->recordFrames; e->stopRecord();
        assert(!e->recording && e->capturePending && e->frames==30000);
        // Playback does not wait, stop or become silent while the copy is pinned.
        for(int i=0;i<500;i++) { float x=e->process(0); assert(x>.2f && e->playing()); }
        e->startRecord(); assert(!e->recording && e->restartCapture);
        e->process(0); assert(e->capturePending);
        release.store(true,std::memory_order_release); reader.join();
        e->process(.25f);
        assert(!e->capturePending && e->recording && !e->restartCapture);
        assert(e->frames==finishedFrames && e->sample.pcm[e->frames-1]==4096);
        e->stopRecord(); // discard the new take with fewer than two samples
    }
    // A falling gate cancels a start queued behind a snapshot without discarding
    // the take already waiting to become the finished sample.
    {
        auto e=std::make_unique<Engine>(); std::atomic<bool> held{false},release{false};
        std::thread reader([&] {
            std::lock_guard<std::mutex> lock(e->sampleMutex); held.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire)) std::this_thread::yield();
        });
        while(!held.load(std::memory_order_acquire)) std::this_thread::yield();
        e->startRecord(); for(int i=0;i<4800;i++) e->process(.125f); e->stopRecord();
        e->startRecord(); assert(e->restartCapture); e->stopRecord(); assert(!e->restartCapture && e->capturePending);
        release.store(true,std::memory_order_release); reader.join(); e->process(0);
        assert(!e->capturePending && !e->recording && e->frames>1000);
    }
    // Concurrent snapshots must contain one entire published take: the revision,
    // length, sample rate and final PCM value all identify the same generation.
    auto e=std::make_unique<Engine>(); std::vector<int16_t> initial(20000,1000); e->install(initial,22050);
    std::atomic<bool> done{false}; std::atomic<unsigned> copies{0};
    std::thread reader([&] {
        std::vector<int16_t> pcm; double rate=0; uint32_t seq=0;
        do {
            assert(e->snapshot(pcm,rate,seq)); assert(!(seq&1));
            if(seq==2) { assert(pcm.size()==20000 && rate==22050 && pcm.back()==1000); }
            else {
                unsigned take=seq/2-2; double expectedRate=captureRate(take%7);
                assert(std::abs(rate-expectedRate)<1e-6);
                assert(std::abs(double(pcm.size())-4096*expectedRate/48000)<2);
                assert(std::abs(int(pcm.back())-4096*int(take%2+1))<=1);
            }
            copies.fetch_add(1,std::memory_order_relaxed); std::this_thread::yield();
        } while(!done.load(std::memory_order_acquire));
    });
    for(unsigned take=0;take<200;take++) {
        e->controls.captureIndex=take%7; e->startRecord();
        for(int i=0;i<4096;i++) e->process(.125f*(take%2+1));
        e->stopRecord();
        while(e->capturePending) { e->process(0); std::this_thread::yield(); }
    }
    done.store(true,std::memory_order_release); reader.join(); assert(copies.load()>0);
    std::cout<<"PASS: pinned snapshots preserve playback, deferred capture/rearm/cancel, "<<copies<<" coherent concurrent snapshots across 200 takes\n";
}
