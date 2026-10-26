// SPDX-License-Identifier: AGPL-3.0-only
#include "Worker.hpp"
#include "Fake.hpp"
#include <cassert>
#include <iostream>
using namespace md_rack;
template<class F>void until(F predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){assert(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
int main(){
    Trace trace;Worker worker([&](const std::string& path,const std::string&)->std::unique_ptr<Backend>{if(path=="bad")throw std::runtime_error("invalid test OS");return std::make_unique<Fake>(&trace);});
    worker.load("bad");until([&]{return worker.status().find("invalid test OS")!=std::string::npos;});assert(!worker.ready);
    worker.load("fake");until([&]{return worker.ready.load();});worker.generation(1);
    InputBlock input;input.generation=1;input.rate=44100;Event e;e.hit.mask=1;e.hit.values[0]=127;input.append(e);assert(worker.submit(input));
    until([&]{return worker.front()!=nullptr;});auto* output=worker.front();assert(output->start==0&&output->generation==1&&output->samples[0][2]==1);worker.pop();
    // Old-generation jobs cannot contribute events to a new timeline.
    worker.generation(2);input.start=128;input.events[0].hit.values[0]=10;assert(worker.submit(input));
    input.generation=2;input.start=0;input.count=0;assert(worker.submit(input));
    until([&]{return worker.front()!=nullptr;});output=worker.front();assert(output->generation==2&&output->samples[0][2]==0);worker.pop();
    {std::lock_guard<std::mutex> guard(trace.mutex);assert(trace.hits.size()==1);}
    std::cout<<"worker_test: load failure/recovery, bounded handoff and generation isolation passed\n";
}
