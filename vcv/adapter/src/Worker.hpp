// SPDX-License-Identifier: AGPL-3.0-only
// The worker thread for any emulated instrument: it loads the OS through the instrument's factory,
// renders the audio thread's blocks of the current generation, and hands rendered blocks back. Both
// queues are bounded; a stale generation's blocks are dropped, never played late.
#pragma once
#include "Renderer.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace emu_rack {
template<class L> class Worker {
public:
    using Factory=std::function<std::unique_ptr<Backend<L>>(const std::string&,const std::string&)>;
    using Output=OutputBlock<L::channels>;
    using Input=InputBlockOf<L::inputs>;
private:
    Queue<Input,16> input;
    Queue<Output,16> output;
    Factory factory;
    std::mutex messageMutex;
    std::string loadPath,samplePath,statusText;
    bool loadPending=false;
    std::vector<Machine> machineList;
    std::atomic<bool> running{true};
    std::atomic<uint64_t> requestedGeneration{0};
    std::thread thread;
    void status(std::string value) {std::lock_guard<std::mutex> guard(messageMutex);statusText=std::move(value);}
    void run() {
        std::unique_ptr<Backend<L>> backend;
        std::unique_ptr<Renderer<L>> renderer;
        Input work;
        Output rendered;
        while(running.load(std::memory_order_acquire)){
            std::string path,samples;
            {std::lock_guard<std::mutex> guard(messageMutex);if(loadPending){path=loadPath;samples=samplePath;loadPending=false;}}
            if(!path.empty()){
                ready.store(false,std::memory_order_release);status("Loading OS and initializing emulators…");
                renderer.reset();backend.reset();
                try {backend=factory(path,samples);{std::lock_guard<std::mutex> guard(messageMutex);machineList=backend->machines();}renderer=std::make_unique<Renderer<L>>(*backend);ready.store(true,std::memory_order_release);++loaded;status(backend->description());}
                catch(const std::exception& e){status(std::string("OS load failed: ")+e.what());}
            }
            const Input* front=input.front();
            if(!front){std::this_thread::sleep_for(std::chrono::microseconds(250));continue;}
            work=*front;input.pop();
            if(!renderer||work.generation!=requestedGeneration.load(std::memory_order_acquire))continue;
            if(work.overflow){++overflows;backend->panic();renderer=std::make_unique<Renderer<L>>(*backend);continue;}
            try {
                renderer->process(work,rendered);
                converterDelay.store(renderer->converterLatency(),std::memory_order_relaxed);
                coalesced.store(renderer->coalesced,std::memory_order_relaxed);
                unavailable.store(renderer->unavailable,std::memory_order_relaxed);
                if(work.generation==requestedGeneration.load(std::memory_order_acquire)&&!output.push(rendered))++overflows;
            }catch(const std::exception& e){ready.store(false,std::memory_order_release);renderer.reset();backend.reset();status(std::string("Engine stopped: ")+e.what());}
        }
    }
public:
    std::atomic<bool> ready{false};
    std::atomic<uint64_t> loaded{0},overflows{0},coalesced{0},unavailable{0};
    std::atomic<int> converterDelay{0};
    // `idle` is the status before an OS is loaded: what to load, and from where.
    Worker(Factory make,std::string idle):factory(std::move(make)),statusText(std::move(idle)){thread=std::thread([this]{run();});}
    ~Worker(){running.store(false,std::memory_order_release);thread.join();}
    void load(const std::string& path,const std::string& samples=""){if(path.empty())return;std::lock_guard<std::mutex> guard(messageMutex);loadPath=path;samplePath=samples;loadPending=true;}
    std::vector<Machine> machines(){std::lock_guard<std::mutex> guard(messageMutex);return machineList;}
    std::string status(){std::lock_guard<std::mutex> guard(messageMutex);return statusText;}
    void generation(uint64_t value){requestedGeneration.store(value,std::memory_order_release);}
    bool submit(const Input& block){if(input.push(block))return true;++overflows;return false;}
    const Output* front() const{return output.front();}
    void pop(){output.pop();}
};
} // namespace emu_rack
