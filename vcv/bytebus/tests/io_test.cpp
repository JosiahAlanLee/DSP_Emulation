#include "../src/engine/Converter.hpp"
#include "../../vastish/src/engine/Engine.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
static bool watch=false; static size_t allocations=0;
void* operator new(size_t n) { if(watch) ++allocations; if(auto* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
namespace bus=dspemu::bus; namespace cv=dspemu::converter;
static bus::Sample data(uint8_t word) { bus::Sample s; s.connected=s.updated=true; s.word=s.rawWord=word; return s; }
static void converter() {
    cv::Parameters p; cv::Engine e;
    for(bool inputWide:{false,true}) for(bool outputWide:{false,true}) {
        p.adcWide=inputWide; p.dacWide=outputWide;
        for(int n=0;n<256;++n) {
            auto out=e.tick(p,bus::bipolar(n)*(inputWide ? 10 : 5),data(n),false,0);
            assert(out.adc==n && out.dac==n && !out.clocked && !out.clip);
            assert(out.voltage==bus::bipolar(n)*(outputWide ? 10 : 5));
        }
    }
    p={}; p.adcGain=-2; p.adcOffset=1;
    auto out=e.tick(p,1,data(0),false,0); assert(out.adc==102);
    p.dacGain=-2; p.dacOffset=1; out=e.tick(p,0,data(0),false,0); assert(out.voltage==11);
    p={}; out=e.tick(p,NAN,data(128),false,0); assert(out.adc==128 && out.clip);
    out=e.tick(p,6,data(128),false,0); assert(out.adc==255 && out.clip);
    out=e.tick(p,-6,data(128),false,0); assert(out.adc==0 && out.clip);
    out=e.tick(p,5,data(128),false,0); assert(out.adc==255 && !out.clip);
    e.reset(); out=e.tick(p,2,data(128),true,10); assert(out.adc==179 && out.clk);
    out=e.tick(p,-2,data(128),true,.5); assert(out.adc==179 && out.clk);
    out=e.tick(p,-2,data(128),true,0); assert(out.adc==179 && !out.clk);
    for(int t=0;t<1000;++t) assert(e.tick(p,-2,data(128),true,0).adc==179);
    out=e.tick(p,-2,data(128),true,10); assert(out.adc==77);
    p.adcClock=cv::AdcClock::Engine; out=e.tick(p,1,data(128),true,10); assert(out.adc==154 && !out.clocked);
    p.adcClock=cv::AdcClock::Jack; out=e.tick(p,3,data(128),false,0); assert(out.clocked && out.adc==154);
    bus::Sample s=data(165); s.clocked=true; s.updated=false; s.rawWord=90;
    p.dacClock=cv::DacClock::Follow; out=e.tick(p,0,s,true,0); assert(out.dac==165);
    p.dacClock=cv::DacClock::Engine; out=e.tick(p,0,s,true,0); assert(out.dac==90);
    p.dacClock=cv::DacClock::Jack; out=e.tick(p,0,s,true,0); assert(out.dac==128);
    out=e.tick(p,0,s,true,10); assert(out.dac==90); // raw current lines, not the upstream latch
    s.rawWord=7; p.dacGain=2; out=e.tick(p,0,s,true,10); assert(out.dac==90 && out.voltage==2*bus::toVolts(90));
    out=e.tick(p,0,{},true,10); assert(out.dac==128 && out.voltage==0);
    p={}; p.adcOffset=2.5; assert(e.tick(p,0,{},false,0).adc==192);
    watch=true;
    for(int n=0;n<100000;++n) e.tick(p,float(n%10),s,true,n&1 ? 0 : 10);
    watch=false; assert(allocations==0);
}
static vastish::Parameters init() {
    auto p=vastish::programs()[0]; p[vastish::PITCH_WAVE]=vastish::BUS_WAVE;
    p[vastish::PITCH_KT]=p[vastish::PITCH_VT]=0;
    return p;
}
static void transform() {
    using namespace vastish;
    for(float sr:{44100.f,48000.f,96000.f}) {
        auto p=init(); Voice v; v.prepare(sr); VoiceInput input; input.busAConnected=true;
        for(int turn:{-2,-1,0,1,2}) for(int n=0;n<256;++n) {
            p[PITCH_ADJ]=60+turn*12; input.busA=bus::bipolar(n); v.tick(p,input,sr);
            assert(bus::fromBipolar(v.tap)==n);
            p[PITCH_ADJ]=66+turn*12; v.tick(p,input,sr); assert(bus::fromBipolar(v.tap)==((n+128)&255));
        }
        input.busAConnected=false; v.tick(p,input,sr); assert(v.tap==0);
        p=init(); p[F1_TYPE]=1; p[F1_ADJ]=36; input.busAConnected=true; input.busA=.5;
        v.panic(); v.tick(p,input,sr); float first=v.tap;
        for(int t=0;t<4000;++t) { auto y=v.tick(p,input,sr); assert(y[0]==0 && y[1]==0); }
        assert(v.tap>first && std::abs(v.tap-.5)<.001);
        Engine normal,controls; std::array<VoiceInput,8> inputs{}; inputs[0].gate=true;
        for(int program=0;program<6;++program) {
            auto q=programs()[program];
            for(int t=0;t<2000;++t) assert(normal.tick(q,inputs,1)==controls.tick(q,inputs,1,nullptr,true));
        }
    }
}
static void logic() {
    using namespace vastish;
    auto p=init(); Voice v; VoiceInput in;
    p[FUN1_A]=SRC_LANE_A; p[FUN1_B]=SRC_LANE_B;
    for(int eq=0;eq<8;++eq) for(int a=0;a<2;++a) for(int b=0;b<2;++b) {
        in.laneA=a; in.laneB=b; p[FUN1_EQ]=eq; p[FUN1_BIT]=0; v.tickControl(p,in,48000);
        assert(v.sources[SRC_FUN1]==fun(eq,a,b));
        p[FUN1_BIT]=1; v.tickControl(p,in,48000);
        assert(v.sources[SRC_FUN1]==float(fun(eq,a,b)>0));
    }
    p[FUN1_EQ]=0; p[FUN1_BIT]=1;
    p[FUN2_A]=SRC_FUN1; p[FUN2_B]=SRC_ONE; p[FUN2_EQ]=6; p[FUN2_BIT]=1;
    for(int a=0;a<2;++a) for(int b=0;b<2;++b) {
        in.laneA=a; in.laneB=b; v.tickControl(p,in,48000);
        assert(v.sources[SRC_FUN2]==float(!(a||b)));
    }
    in.laneA=in.laneB=1; p[FUN1_BIT]=0; v.tickControl(p,in,48000);
    assert(v.sources[SRC_FUN1]==2 && v.sources[SRC_FUN2]==1); // numeric behavior is preserved
    assert(!positiveBit(NAN) && !positiveBit(INFINITY) && !positiveBit(-1));
}
int main() { converter(); transform(); logic(); std::cout<<"PASS: calibrated conversions, independent clock policies/raw reclock, allocation-free ADC/DAC, all BUS bytes/rotations, gate-free stateful DSP, control/audio identity, composable FUN BIT logic\n"; }
