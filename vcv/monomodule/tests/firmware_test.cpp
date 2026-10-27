// SPDX-License-Identifier: AGPL-3.0-only
// Optional, user-supplied OS: the adapter's Monomachine plays exactly what separately driven Monomodule
// voices play: a note on track 3 and its release, then a THRU machine on track 1 hearing INP A+B.
#include "Monomachine.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace mnm_rack;
// A voice set up as the adapter sets up each of its tracks.
static void prepare(mnm::MonoVoice& v) {
    auto& h=v.host();h.setOutBuses(1);h.setLevel(100);prewarm(v);h.setMachine(MnmMachine::GND);h.setRouting(0);v.warmUp(64);
}
int main(int argc,char** argv){
    if(argc<2){std::cout<<"firmware_test: supply the Monomachine OS 1.32B .syx to run the real-engine comparison\n";return 77;}
    auto began=std::chrono::steady_clock::now();
    Monomachine adapter(argv[1]);
    double loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    auto fw=mnm::fw::loadFirmware(argv[1]);
    mnm::MonoVoice track1(fw),track3(fw);
    prepare(track1);prepare(track3);
    // A SAW note on track 3, released after 1200 blocks.
    Hit hit;hit.track=2;hit.machine=int16_t(MnmMachine::SAW);hit.mask=3;hit.values[0]=90;hit.values[1]=30;hit.note=57;hit.bpm=120;
    auto values=resolve(hit,machineDef(hit.machine)->defaults);
    {auto& h=track3.host();h.setMachine(MnmMachine::SAW);h.setRouting(0);for(int k=0;k<params;++k)h.setParam(mnm::host::Page::SYN,k,values[size_t(k)]);h.setBpm(120);h.settle();h.noteOn(57);}
    assert(adapter.fire(hit));
    double peak=0,worst=0;
    float l[nativeFrames],r[nativeFrames];
    for(int b=0;b<2000;++b){
        if(b==1200){adapter.release(2);track3.host().noteOff();}
        float actual[nativeFrames][channels];
        auto t0=std::chrono::steady_clock::now();
        adapter.render(actual);
        worst=std::max(worst,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
        track3.process(l,r,nativeFrames);
        for(int f=0;f<nativeFrames;++f){
            assert(actual[f][trackLeft+2]==l[f]&&actual[f][trackRight+2]==r[f]);
            peak=std::max(peak,double(std::abs(l[f])));
        }
        track1.process(l,r,nativeFrames);
        for(int f=0;f<nativeFrames;++f){
            // Track 1 rests on GND. Prewarming leaves a constant one-LSB offset (-120 dBFS).
            assert(actual[f][trackLeft]==l[f]&&std::abs(actual[f][trackLeft])<1e-5f);
        }
    }
    assert(peak>.01);
    std::cout<<"firmware_test: track 3 equals a direct Monomodule voice for 32000 frames; peak "<<peak<<"; load "<<loadMs
             <<" ms; worst block "<<worst<<" ms (six engines, 16 frames = 0.36 ms of audio)\n";
    // THRU on track 1, its input INP A+B, as the adapter takes it: equal to a direct voice's FX path.
    uint8_t settings[emu_rack::trackSettings]={};settings[0]=INP_AB;
    adapter.configure(settings);
    track1.host().setRouting(0);   // what configure() does to a track not yet on an FX machine
    Hit thru;thru.track=0;thru.machine=int16_t(MnmMachine::THRU);thru.note=60;thru.bpm=120;
    assert(adapter.fire(thru));
    {auto& h=track1.host();h.setMachine(MnmMachine::THRU);h.setRouting(mnm::host::dspInputBits(mnm::host::FxInput::InpAB));h.noteOn(60);
     auto v=resolve(thru,machineDef(thru.machine)->defaults);for(int k=0;k<params;++k)h.setParam(mnm::host::Page::SYN,k,v[size_t(k)]);
     h.setBpm(120);h.settle();h.noteOn(60);}
    double fxPeak=0;
    for(int b=0;b<600;++b){
        float in[nativeFrames][inputs],a[nativeFrames],bb[nativeFrames];
        for(int f=0;f<nativeFrames;++f){
            a[f]=in[f][0]=float(.3*std::sin(.05*(b*nativeFrames+f)));
            bb[f]=in[f][1]=float(.2*std::sin(.031*(b*nativeFrames+f)));
        }
        adapter.input(&in[0][0]);
        float actual[nativeFrames][channels];adapter.render(actual);
        track1.processFx(a,bb,l,r,nativeFrames);
        for(int f=0;f<nativeFrames;++f){
            assert(actual[f][trackLeft]==l[f]&&actual[f][trackRight]==r[f]);
            fxPeak=std::max(fxPeak,double(std::abs(l[f])));
        }
    }
    assert(fxPeak>.05);
    std::cout<<"firmware_test: THRU on INP A+B equals a direct voice for 9600 frames; peak "<<fxPeak<<"\n";
}
