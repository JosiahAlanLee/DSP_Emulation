// SPDX-License-Identifier: BSD-3-Clause
// Independent calibrated ADC and DAC paths. No Rack types or processing allocation.
#pragma once
#include "../../../shared/ByteBus.hpp"
#include <algorithm>

namespace dspemu::converter {
enum class AdcClock { Auto, Engine, Jack };
enum class DacClock { Follow, Engine, Jack };
struct Parameters {
    float adcGain=1,adcOffset=0,dacGain=1,dacOffset=0;
    bool adcWide=false,dacWide=false;
    AdcClock adcClock=AdcClock::Auto; DacClock dacClock=DacClock::Follow;
};
struct Output {
    uint8_t adc=128,dac=128;
    bool clocked=false,clk=false,clip=false;
    float voltage=0;
};
inline float trim(float v,float fallback,float low,float high) {
    return std::isfinite(v) ? std::max(low,std::min(high,v)) : fallback;
}
struct Engine {
    uint8_t adc=128,dac=128; bool clip=false;
    bus::Gate adcGate,dacGate;
    bool adcClocked=false,adcConnected=false,dacConnected=false;
    DacClock lastDacClock=DacClock::Follow;
    void reset() { *this=Engine{}; }
    Output tick(const Parameters& p,float signal,const bus::Sample& input,bool clockConnected,float clock) {
        bool clocked=p.adcClock==AdcClock::Jack || (p.adcClock==AdcClock::Auto && clockConnected);
        if(clocked!=adcClocked || clockConnected!=adcConnected) adcGate.reset();
        adcClocked=clocked; adcConnected=clockConnected;
        bool adcEdge=adcGate.process(clockConnected ? clock : 0.f);
        if(!clocked || adcEdge) {
            double value=double(signal)*trim(p.adcGain,1,-4,4)+trim(p.adcOffset,0,-10,10);
            double range=p.adcWide ? 10 : 5;
            clip=!std::isfinite(value) || value < -range || value > range;
            adc=bus::fromBipolar(float(value/range));
        }
        if(p.dacClock!=lastDacClock || clockConnected!=dacConnected) {
            dacGate.reset();
            if(p.dacClock!=lastDacClock && p.dacClock==DacClock::Jack) dac=128;
        }
        lastDacClock=p.dacClock; dacConnected=clockConnected;
        bool dacEdge=dacGate.process(clockConnected ? clock : 0.f);
        if(!input.connected) dac=128;
        else if(p.dacClock==DacClock::Follow) dac=input.word;
        else if(p.dacClock==DacClock::Engine || dacEdge) dac=input.rawWord;
        Output out;
        out.adc=adc; out.dac=dac; out.clip=clip; out.clocked=clocked; out.clk=clocked && adcGate.high;
        out.voltage=bus::bipolar(dac)*(p.dacWide ? 10.f : 5.f)*trim(p.dacGain,1,-4,4)+trim(p.dacOffset,0,-10,10);
        return out;
    }
};
} // namespace dspemu::converter
