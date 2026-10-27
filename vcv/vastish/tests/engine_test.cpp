#include <limits>
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
    // FUN 3/4 and AUX: chained functions, per-voice AUX, clamping, cycle safety.
    {
        auto p=programs()[0];
        p[FUN3_A]=SRC_AUX1; p[FUN3_B]=SRC_AUX1+1; p[FUN3_EQ]=2;       // FUN3 = AUX1 * AUX2
        p[FUN4_A]=SRC_FUN3; p[FUN4_B]=SRC_AUX1+2; p[FUN4_EQ]=0;       // FUN4 = FUN3 + AUX3
        p[FUN1_A]=SRC_FUN4; p[FUN1_B]=0; p[FUN1_EQ]=0;                 // FUN1 reads FUN4 (old block, new source)
        p[PITCH_S1]=SRC_FUN1; p[PITCH_D1]=12;                          // pitch +12 per unit of FUN1
        Engine e; std::array<VoiceInput,8> in{};
        for(int v=0;v<8;++v) { in[v].gate=true; in[v].aux={.5f,float(v)/8,.25f,9.f}; }
        e.tick(p,in);
        for(int v=0;v<8;++v) {
            auto& s=e.voices[v].sources;
            assert(std::abs(s[SRC_FUN3]-.5f*v/8)<1e-6f && std::abs(s[SRC_FUN4]-(.5f*v/8+.25f))<1e-6f);
            assert(s[SRC_FUN1]==s[SRC_FUN4] && s[SRC_AUX1+3]==1.f);       // AUX clamps to +-1
            assert(std::abs(e.voices[v].control(PITCH_ADJ,p,60)-(p[PITCH_ADJ]+12*s[SRC_FUN4]))<1e-4f);
        }
        in[0].aux[0]=std::numeric_limits<float>::quiet_NaN(); e.tick(p,in); assert(e.voices[0].sources[SRC_AUX1]==0);
        // A FUN3 <-> FUN4 cycle evaluates to zero on the cyclic edge, never NaN or stale.
        p[FUN3_A]=SRC_FUN4; p[FUN4_A]=SRC_FUN3;
        for(int i=0;i<100;++i) { auto y=e.tick(p,in); assert(std::isfinite(y[0])&&std::isfinite(y[1])); }
        for(auto& v:e.voices) assert(std::isfinite(v.sources[SRC_FUN3])&&std::isfinite(v.sources[SRC_FUN4]));
    }
    // Sleeping unpatched voices must not change the audio of the voices in use,
    // including a voice whose channel disappears while it is still releasing.
    for(int program=0;program<6;++program) {
        Engine all,used; auto params=programs()[program];
        std::array<VoiceInput,8> in{}; in[0].note=43; in[1].note=55; in[0].velocity=in[1].velocity=.9f;
        for(int i=0;i<96000;++i) {
            in[0].gate=(i%24000)<15000; in[1].gate=i<20000;
            int channels=i<20000 ? 2 : 1; // channel 2 is unplugged mid-release
            auto a=all.tick(params,in), b=used.tick(params,in,channels);
            assert(a[0]==b[0] && a[1]==b[1]);
        }
    }
    {
        Engine eng; auto params=programs()[1]; std::array<VoiceInput,8> in{}; in[0].gate=true; in[0].note=48;
        auto t0=std::chrono::steady_clock::now();
        for(int i=0;i<48000;++i) eng.tick(params,in,1);
        std::cout<<"mono_ladder_bass_cpu_seconds_per_audio_second="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count()<<"\n";
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
    // AUDIO wave (Live Mode): AUDIO IN replaces the oscillator. Through NONE blocks,
    // algorithm 1, 0 dB AMP and PAN hard left, the left output is the input exactly.
    {
        auto p=programs()[0]; p[ALG]=1; p[PITCH_WAVE]=AUDIO_WAVE; p[MASTER]=0;
        p[F1_TYPE]=p[F2_TYPE]=p[F3_TYPE]=0; p[AMP_ADJ]=0; p[PAN_ADJ]=-1;
        for(float rate:{44100.f,48000.f,96000.f}) {
            Voice v; v.prepare(rate); VoiceInput in; in.audioConnected=true; // GATE unpatched
            for(int n=0;n<int(rate);++n) {
                in.audio=n==7 ? 1.f : n<100 ? 0.f : float(std::sin(6.283185307179586*100*n/rate)); // impulse, then 100 Hz
                auto y=v.tick(p,in,rate); assert(y[0]==in.audio);
            }
            // Through the engine the DC blocker is the only difference, once MASTER's 50 ms ramp has settled.
            Engine eng; eng.prepare(rate); std::array<VoiceInput,8> inputs{}; inputs[0].audioConnected=true;
            for(int n=0;n<int(rate*.1f);++n) eng.tick(p,inputs,1);
            Highpass dc; double peak=0;
            for(int n=0;n<int(rate);++n) {
                inputs[0].audio=float(std::sin(6.283185307179586*100*n/rate));
                auto y=eng.tick(p,inputs,1); assert(y[0]==dc.tick(inputs[0].audio,5,rate));
                if(n>rate/2) peak=std::max(peak,double(std::abs(y[0])));
            }
            assert(std::abs(peak-1)<.002); // a 5 Hz one-pole high-pass passes 100 Hz at -0.011 dB
        }
        // Gate-free is unity; a patched gate brings back the amp envelope.
        p[AMP_A]=10; p[AMP_D]=0; p[AMP_S]=1;
        Voice freeVoice,gated; freeVoice.prepare(48000); gated.prepare(48000);
        VoiceInput in; in.audioConnected=true; in.audio=.5f;
        assert(freeVoice.tick(p,in,48000)[0]==.5f);
        in.gateConnected=true; assert(gated.tick(p,in,48000)[0]==0);      // patched, low: silent
        in.gate=true; float first=gated.tick(p,in,48000)[0];
        for(int n=0;n<1000;++n) gated.tick(p,in,48000);
        float held=gated.tick(p,in,48000)[0];
        assert(first>0 && first<.01f && std::abs(held-.5f)<1e-6f);         // attack, then sustain at 1
        // AUDIO with nothing patched to AUDIO IN is a silent input under the usual envelope.
        Voice idle; idle.prepare(48000); VoiceInput none; none.audio=.5f;
        for(int n=0;n<100;++n) assert(idle.tick(p,none,48000)[0]==0);
        // Non-finite input reads 0; out-of-range input clamps to ±1.
        for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            Voice v; v.prepare(48000); VoiceInput b; b.audioConnected=true; b.audio=bad;
            assert(v.tick(p,b,48000)[0]==0);
        }
        { Voice v; v.prepare(48000); VoiceInput b; b.audioConnected=true; b.audio=3; assert(v.tick(p,b,48000)[0]==1); }
    }
    // A key-tracked F block follows the voice's note: a 1 kHz sine through LOWPASS with
    // ADJUST 60 and KEY 1 is much brighter at note 96 than at note 36.
    {
        auto p=programs()[0]; p[ALG]=1; p[PITCH_WAVE]=AUDIO_WAVE; p[AMP_ADJ]=0; p[PAN_ADJ]=-1;
        p[F1_TYPE]=1; p[F1_ADJ]=60; p[F1_KT]=1; p[F2_TYPE]=p[F3_TYPE]=0;
        auto rms=[&](float note) {
            Voice v; v.prepare(48000); VoiceInput in; in.audioConnected=true; in.note=note; double sum=0;
            for(int n=0;n<48000;++n) { in.audio=float(std::sin(6.283185307179586*1000*n/48000)); float y=v.tick(p,in,48000)[0]; if(n>=24000) sum+=y*y; }
            return std::sqrt(sum/24000);
        };
        double low=rms(36), high=rms(96);
        assert(high>5*low && high>.4);
    }
    // Every block type processes full-scale external audio without faults at each rate.
    for(float rate:{44100.f,48000.f,96000.f}) for(int type=0;type<=14;++type) {
        auto p=programs()[0]; p[PITCH_WAVE]=AUDIO_WAVE; p[F1_TYPE]=p[F2_TYPE]=p[F3_TYPE]=type;
        p[F1_B]=p[F2_B]=p[F3_B]=type==14 ? .95f : 4;
        for(int alg=1;alg<=6;++alg) {
            p[ALG]=alg; Engine eng; eng.prepare(rate); std::array<VoiceInput,8> inputs{}; inputs[0].audioConnected=true;
            for(int n=0;n<int(rate*.25f);++n) {
                inputs[0].audio=float(std::sin(6.283185307179586*220*n/rate))+((n*7919)%13==0 ? .9f : 0.f);
                auto y=eng.tick(p,inputs,1); assert(std::isfinite(y[0]) && std::isfinite(y[1]));
            }
            assert(eng.faults==0);
        }
    }
    // Chain mode: chainAudio replaces the oscillator whatever the wave; a gate-free chained voice is
    // at unity, otherwise its own amp envelope runs on the gate it is given; non-finite reads 0.
    {
        auto p=programs()[0]; p[ALG]=1; p[MASTER]=0; p[F1_TYPE]=p[F2_TYPE]=p[F3_TYPE]=0; p[AMP_ADJ]=0; p[PAN_ADJ]=-1;
        for(int wave=0;wave<=AUDIO_WAVE;++wave) {
            p[PITCH_WAVE]=float(wave); Voice v; v.prepare(48000); VoiceInput in; in.chained=in.chainGateFree=true;
            for(int n=0;n<2000;++n) { in.chainAudio=float(std::sin(.01*n))*1.5f; assert(v.tick(p,in,48000)[0]==in.chainAudio); }
        }
        p[PITCH_WAVE]=0; p[AMP_A]=10; p[AMP_D]=0; p[AMP_S]=1;
        Voice gated; gated.prepare(48000); VoiceInput in; in.chained=true; in.chainAudio=.5f;
        for(int n=0;n<100;++n) assert(gated.tick(p,in,48000)[0]==0);              // no gate: silent
        in.gate=true; float first=gated.tick(p,in,48000)[0];
        for(int n=0;n<1000;++n) gated.tick(p,in,48000);
        assert(first>0 && first<.01f && std::abs(gated.tick(p,in,48000)[0]-.5f)<1e-6f);
        Voice bad; bad.prepare(48000); VoiceInput b; b.chained=b.chainGateFree=true; b.chainAudio=std::numeric_limits<float>::quiet_NaN();
        assert(bad.tick(p,b,48000)[0]==0);
        b.chainAudio=100; assert(bad.tick(p,b,48000)[0]==16);                   // a safety clamp, far above ±1
    }
    std::cout<<"PASS: six algorithms, envelope timing, FUN dependencies, eighteen eight-voice renders, bit-identical idle-voice sleeping, FUN 3/4 and per-voice AUX, all function endpoints, zero steady-state allocations, AUDIO wave unity and gating, key tracking and every block type, chain input\n";
}
