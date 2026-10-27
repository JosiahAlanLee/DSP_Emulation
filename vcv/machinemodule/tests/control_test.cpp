// SPDX-License-Identifier: AGPL-3.0-only
#include "Control.hpp"
#include "SampleBank.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
using namespace md_rack;
int main(){
    float knobs[8]={},cv[8]={};gorgon_sound::Sound sound;sound.machine=48;sound.mask=3;sound.values[0]=0;sound.values[1]=120;
    knobs[0]=2;cv[1]=1;
    auto hit=capture(2,sound,16,.5f,knobs,cv,138);
    knobs[0]=100;cv[1]=-10;sound.values[0]=127; // the event owns the old state
    auto values=resolve(hit,{11,22,33,44,55,66,77,88});
    assert(hit.machine==48&&hit.track==2&&hit.velocity==64&&hit.bpm==138);
    assert(values[0]==2&&values[1]==127&&values[2]==33);
    gorgon_sound::Sound fresh;auto next=capture(2,fresh,16,0,knobs,cv,120);
    assert(next.mask==0&&next.machine==16&&next.velocity==0);
    next.offsets[0]=std::numeric_limits<float>::infinity();assert(resolve(next,{11,22,33,44,55,66,77,88})[0]==0);
    InputBlock block;Event event;for(int i=0;i<maxEvents;++i)assert(block.append(event));assert(!block.append(event)&&block.overflow&&block.count==maxEvents);
    LinkState link;gorgon_sound::Packet p;p.frame=10;p.moving=0;
    assert(!link.update(&p,11).hits[0]);
    p.frame=11;p.moving=1;p.transport=3;p.lanes[0].gate=10;p.lanes[0].velocity=.75f;
    auto r=link.update(&p,12);assert(r.panic&&r.hits[0]);
    p.lanes[0].sound.values[0]=100;assert(!link.update(&p,12).hits[0]); // duplicate frame
    p.frame=12;assert(!link.update(&p,13).hits[0]); // controls alone don't trigger
    p.frame=13;p.lanes[0].gate=0;assert(!link.update(&p,14).hits[0]);
    p.frame=14;p.lanes[0].gate=10;assert(link.update(&p,15).hits[0]);
    assert(link.update(nullptr,16).panic);assert(!link.update(&p,16).hits[0]); // reconnect high is seeded
    assert(!link.update(&p,18).hits[0]&&!link.connected); // stale
    // Sound link version 2: lanes 5 and 6 trigger tracks 5 and 6.
    LinkState six;gorgon_sound::Packet q;q.frame=20;q.moving=1;q.transport=1;six.update(&q,20);
    q.frame=21;q.lanes[5].gate=10;q.lanes[5].velocity=1;q.lanes[5].sound.machine=48;
    auto s=six.update(&q,21);assert(s.hits[5]&&!s.hits[4]&&!s.hits[0]);
    // A version-1 packet in a version-2 buffer is inactive, not misread.
    q.version=1;q.frame=22;assert(!six.update(&q,22).hits[5]&&!six.connected);
    // Stress full tuple ownership across the actual SPSC acquire/release boundary.
    Queue<InputBlock,4> queue;constexpr int count=100000;
    std::thread producer([&]{for(int i=0;i<count;++i){InputBlock x;x.start=i;x.count=1;x.events[0].hit.values[0]=float(i);while(!queue.push(x))std::this_thread::yield();}});
    for(int i=0;i<count;++i){const InputBlock* x;while(!(x=queue.front()))std::this_thread::yield();assert(x->start==i&&x->count==1&&x->events[0].hit.values[0]==float(i));queue.pop();}producer.join();
    auto file=std::filesystem::temp_directory_path()/"machinemodule-control-samples-test.bin";
    auto write=[&](const std::vector<uint8_t>& bytes){std::ofstream f(file,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
    std::vector<uint8_t> bytes={'M','D','S','1',0,0,0x14,0,1,0,0,0,0x56,0x34,0x12,0,0,0,0,0,0,0,0,0};
    write(bytes);auto samples=readSampleBank(file.string());assert(samples.size()==1&&samples[0].address==0x140000&&samples[0].words[0]==0x123456);
    for(int bad=0;bad<3;++bad){auto b=bytes;if(bad==0)b[0]='X';if(bad==1)b.resize(15);if(bad==2)b[6]=0;write(b);bool threw=false;try{readSampleBank(file.string());}catch(const std::exception&){threw=true;}assert(threw);}
    std::filesystem::remove(file);
    std::cout<<"control_test: CV snapshots, default restoration, six-lane link edges, bounds, MDS1 and 100000 threaded tuples passed\n";
}
