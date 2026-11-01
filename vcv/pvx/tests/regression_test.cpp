// SPDX-License-Identifier: BSD-3-Clause
// External regressions found during integration: useful high-cutoff output and tiny tails.
#include "../src/engine/PvxVoice.hpp"
#include <cstdio>
#include <cmath>
using namespace dspemu::pvx;
int main(){
    int failures=0;
    for(int os:{1,2,4}){
        VoiceEngine e;Parameters p;p.oversample=os;p.cutoff=20000;p.resonance=0;
        p.osc1Level=.2;p.osc2Level=0;p.noiseLevel=0;p.cutoffDrive=0;p.env2Sustain=1;
        std::array<Input,8> in{};std::array<float,8> out{},lp{},bp{};
        in[0]={float(std::log2(5432./261.6255653005986)),10,0};
        double real=0,imag=0,nyquist=0;float statePeak=0;
        for(int i=0;i<96000;++i){
            e.process(in,1,p,FilterCalibration{},48000,0,0,out,lp,bp);
            if(i>=48000){double phase=2*dspemu::rate::pi*5432*i/48000.;
                real+=out[0]*std::cos(phase);imag+=out[0]*std::sin(phase);
                nyquist+=out[0]*(i%2?-1:1);
                statePeak=std::max(statePeak,std::max(std::abs(e.state[0].lp),std::abs(e.state[0].bp)));
            }
        }
        double fundamental=2*std::hypot(real,imag)/48000,nyq=2*std::abs(nyquist)/48000;
        std::printf("high cutoff %dx: fundamental %.8f, Nyquist %.8f, internal peak %.5f\n",os,fundamental,nyq,statePeak);
        if(!(fundamental>.005&&statePeak<.5&&nyq<fundamental*.001))++failures;
    }
    VoiceEngine e;Parameters p;p.level=1e-17f;p.osc1Level=.2;p.osc2Level=0;p.env2Release=.005;
    std::array<Input,8> in{};std::array<float,8> out{},lp{},bp{};in[0].gate=10;
    for(int i=0;i<4800;++i)e.process(in,1,p,FilterCalibration{},48000,0,0,out,lp,bp);
    in[0].gate=0;int subnormalOutputs=0;
    for(int i=0;i<4800;++i){
        e.process(in,1,p,FilterCalibration{},48000,0,0,out,lp,bp);
        if(std::fpclassify(out[0])==FP_SUBNORMAL)++subnormalOutputs;
    }
    std::printf("tiny release: %d subnormal output samples\n",subnormalOutputs);
    if(subnormalOutputs)++failures;
    std::printf("integration regressions: %d failures\n",failures);return failures?1:0;
}
