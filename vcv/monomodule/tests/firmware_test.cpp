// SPDX-License-Identifier: AGPL-3.0-only
// Optional, user-supplied OS: the adapter's Monomachine plays exactly what a separately driven Monomodule
// voice plays, for a note on track 3 and its release.
#include "Monomachine.hpp"
#include <cassert>
#include <cmath>
#include <chrono>
#include <iostream>
int main(int argc,char** argv){
    if(argc<2){std::cout<<"firmware_test: supply the Monomachine OS 1.32B .syx to run the real-engine comparison\n";return 77;}
    using namespace mnm_rack;
    auto began=std::chrono::steady_clock::now();
    Monomachine adapter(argv[1]);
    double loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    auto fw=mnm::fw::loadFirmware(argv[1]);
    mnm::MonoVoice direct(fw);
    {auto& h=direct.host();h.setOutBuses(1);h.setLevel(100);prewarm(direct);h.setMachine(MnmMachine::GND);h.setRouting(0);direct.warmUp(64);}
    Hit hit;hit.track=2;hit.machine=int16_t(MnmMachine::SAW);hit.mask=3;hit.values[0]=90;hit.values[1]=30;hit.note=57;hit.bpm=120;
    const auto* def=machineDef(hit.machine);
    auto values=resolve(hit,def->defaults);
    {auto& h=direct.host();h.setMachine(MnmMachine::SAW);h.setRouting(0);for(int k=0;k<params;++k)h.setParam(mnm::host::Page::SYN,k,values[size_t(k)]);h.setBpm(120);h.settle();h.noteOn(57);}
    assert(adapter.fire(hit));
    double peak=0,worst=0;
    for(int b=0;b<2000;++b){
        if(b==1200){adapter.release(2);direct.host().noteOff();}
        float actual[nativeFrames][channels];
        auto t0=std::chrono::steady_clock::now();
        adapter.render(actual);
        worst=std::max(worst,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
        float l[nativeFrames],r[nativeFrames];direct.process(l,r,nativeFrames);
        for(int f=0;f<nativeFrames;++f){
            assert(actual[f][trackLeft+2]==l[f]&&actual[f][trackRight+2]==r[f]);
            // The other tracks rest on GND. Prewarming leaves a constant one-LSB offset (-120 dBFS).
            assert(std::abs(actual[f][trackLeft])<1e-5f&&std::abs(actual[f][trackLeft+5])<1e-5f);
            peak=std::max(peak,double(std::abs(l[f])));
        }
    }
    assert(peak>.01);
    std::cout<<"firmware_test: track 3 equals a direct Monomodule voice for 32000 frames; peak "<<peak<<"; load "<<loadMs
             <<" ms; worst block "<<worst<<" ms (six engines, 16 frames = 0.36 ms of audio)\n";
}
