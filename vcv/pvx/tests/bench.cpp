// SPDX-License-Identifier: BSD-3-Clause
// Wall and process CPU measurements of the accepted engine; results consumed.
#include "../src/engine/PvxVoice.hpp"
#include <chrono>
#include <ctime>
#include <cstdio>
using namespace dspemu::pvx;
volatile double benchmarkChecksum=0;
int main(){
    std::puts("rate,os,voices,ns_per_voice_sample,process_core_percent,wall_core_percent,checksum");
    for(int voices:{1,8})for(double sr:{44100.,48000.,96000.})for(int os:{1,2,4}){
        if(voices==1&&(sr!=48000||os!=4))continue;
        VoiceEngine e;Parameters p;p.osc1Wave=p.osc2Wave=1;p.osc2Level=1;p.noiseLevel=.3;
        p.sync=true;p.cutoff=1200;p.resonance=.8;p.cutoffDrive=1;p.drive=.7;p.filterEnv=.5;p.oversample=os;
        std::array<Input,8> in{};std::array<float,8> out{},lp{},bp{};
        for(int c=0;c<voices;++c)in[c]={float(c*.5),10,0};
        for(int i=0;i<2048;++i)e.process(in,voices,p,FilterCalibration{},sr,0,0,out,lp,bp);
        constexpr double seconds=2;int samples=int(sr*seconds);double sum=0;
        auto wallStart=std::chrono::steady_clock::now();auto cpuStart=std::clock();
        for(int i=0;i<samples;++i){
            e.process(in,voices,p,FilterCalibration{},sr,0,0,out,lp,bp);
            for(int c=0;c<voices;++c)sum+=out[c];
        }
        double cpu=double(std::clock()-cpuStart)/CLOCKS_PER_SEC;
        double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-wallStart).count();
        benchmarkChecksum=sum;
        std::printf("%.0f,%d,%d,%.2f,%.3f,%.3f,%.9f\n",sr,os,voices,cpu*1e9/(samples*voices),cpu/seconds*100,wall/seconds*100,sum);
    }
}
