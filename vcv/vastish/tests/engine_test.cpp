#include "../src/engine/Engine.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
static bool watchAlloc=false; static size_t allocations=0;
void* operator new(size_t n) { if(watchAlloc) ++allocations; if(void* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
int main() {
    using namespace vastish;
    assert(fun(2,.3f,.5f)==.15f); assert(fun(6,-.5f,.5f)==1);
    Envelope e; e.gate(true,10,20,.4f,30,1000); float v=0;
    for(int i=0;i<10;++i) { v=e.tick(); } assert(std::abs(v-1)<1e-6);
    for(int i=0;i<20;++i) { v=e.tick(); } assert(std::abs(v-.4f)<1e-6);
    e.gate(false,10,20,.4f,30,1000); for(int i=0;i<30;++i) v=e.tick(); assert(v==0);
    // Compare six routing graphs with known constant gain blocks.
    for(int alg=1;alg<=6;++alg) {
        Voice a; a.prepare(48000); auto p=programs()[0]; p[ALG]=alg; p[PITCH_WAVE]=3;
        p[F1_TYPE]=p[F2_TYPE]=p[F3_TYPE]=11; p[F1_ADJ]=6; p[F2_ADJ]=-6; p[F3_ADJ]=-3;
        p[AMP_A]=p[AMP_D]=0; p[AMP_S]=1; p[AMP_ADJ]=0;
        VoiceInput in; in.gate=true;
        float g1=dbgain(6),g2=dbgain(-6),g3=dbgain(-3); const float* r=routes[alg-1];
        float f1=g1, f2=(r[0]+r[1]*f1)*g2, f3=(r[2]+r[3]*f1+r[4]*f2+r[5]*f1*f2)*g3;
        float expected=(r[6]*f1+r[7]*f2+r[8]*f3)*cosine(.125f);
        auto y=a.tick(p,in,48000); assert(std::abs(y[0]-expected)<2e-6);
    }
    // Acyclic FUN dependencies and deliberate zero value on recursive edges.
    Voice a; a.prepare(48000); auto p=programs()[0]; a.sources[3]=.5f; a.sources[1]=.25f;
    p[FUN1_A]=6; p[FUN1_B]=3; p[FUN1_EQ]=2; p[FUN2_A]=1; p[FUN2_B]=3; p[FUN2_EQ]=0;
    unsigned busy=0,done=0; assert(a.evaluateFun(0,p,busy,done)==.375f);
    p[FUN2_A]=5; busy=done=0; assert(std::isfinite(a.evaluateFun(0,p,busy,done)));
    std::array<VoiceInput,8> inputs{};
    for(float rate:{44100.f,48000.f,96000.f}) for(int program=0;program<6;++program) {
        Engine eng; eng.prepare(rate); auto params=programs()[program];
        for(int c=0;c<8;++c) { inputs[c].note=36+c*5; inputs[c].velocity=.8f; inputs[c].modwheel=.6f; inputs[c].gate=true; }
        double energy=0; auto start=std::chrono::steady_clock::now(); watchAlloc=true;
        for(int i=0;i<int(rate);++i) { if(i==int(rate*.65)) for(auto& input:inputs) input.gate=false; auto y=eng.tick(params,inputs); assert(std::isfinite(y[0])&&std::isfinite(y[1])); energy+=y[0]*y[0]+y[1]*y[1]; }
        watchAlloc=false; assert(energy>1e-4); assert(eng.faults==0); assert(allocations==0);
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"program="<<program<<" sr="<<rate<<" eight_voice_cpu_seconds_per_audio_second="<<seconds<<"\n";
    }
    // Every function and endpoint must remain finite at all supported test rates.
    std::mt19937 rng(17);
    for(float rate:{44100.f,48000.f,96000.f}) for(int type=0;type<=14;++type) {
        FunctionBlock f; f.prepare(rate);
        for(int i=0;i<10000;++i) {
            float x=float(int(rng()%2001)-1000)/1000;
            float y=f.tick(type,x,i%2?135:-20,i%3?40:-.98f,rate); assert(std::isfinite(y));
        }
    }
    std::cout<<"PASS: six algorithms, envelope timing, FUN dependencies, eighteen eight-voice renders, all function endpoints, zero steady-state allocations\n";
}
