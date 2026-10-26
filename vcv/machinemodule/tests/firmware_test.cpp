// SPDX-License-Identifier: AGPL-3.0-only
// Optional, user-supplied OS: adapter output must match a separately driven core.
#include "Machinedrum.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
    if(argc<2){std::cout<<"firmware_test: supply an OS .syx to run real-core comparison\n";return 77;}
    using namespace md_rack;
    auto fw=md::fw::loadFirmware(argv[1]);auto c=md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(argv[1])));
    md::engine::Engine direct(fw,std::move(c.sections.at(0).data));
    md::engine::Engine::Output raw;for(int i=0;i<40;++i)assert(direct.render(raw));mm::prewarmMachines(direct,false,false);
    for(int t=0;t<16;++t){direct.host().setLevel(t,100);direct.host().setRouting(t,6);for(int p=8;p<24;++p)direct.host().setParam(t,p,Machinedrum::kitDefaults[p]);}
    Machinedrum adapter(argv[1]);Hit hit;hit.machine=16;hit.mask=3;hit.values[0]=70;hit.values[1]=100;hit.velocity=100;
    auto values=resolve(hit,direct.os().machine(16)->defaults);
    direct.host().setTempo(120);direct.host().setMachine(0,16);for(int i=0;i<8;++i)direct.host().setParam(0,i,values[i]);direct.host().trigger(0,100);
    assert(adapter.fire(hit));double peak=0;
    for(int b=0;b<300;++b){float actual[32][channels];adapter.render(actual);assert(direct.render(raw));for(int f=0;f<32;++f){assert(actual[f][0]==raw.mix.main[f][0]/8388608.f);assert(actual[f][2]==raw.tracks[0][f]/8388608.f);peak=std::max(peak,double(std::abs(actual[f][0])));}}
    // A closed filter or zero track level leaves this hit near -74 dBFS.
    assert(peak>.01);std::cout<<"firmware_test: adapter equals direct core for 9600 frames; peak "<<peak<<"\n";
}
