#pragma once
#include "Dsp.hpp"
#include "Programs.hpp"
namespace vastish {
inline float fun(int eq,float a,float b) {
    switch(eq) { case 1:return a-b; case 2:return a*b; case 3:return (a+b)*.5f;
        case 4:return std::max(a,b); case 5:return std::min(a,b); case 6:return std::abs(a-b);
        case 7:return a*(b+1)*.5f; default:return a+b; }
}
inline int choice(float v,int max) { return int(clamp(std::round(v),0,float(max))); }
struct VoiceInput {
    float note=60,velocity=1,modwheel=0; bool gate=false; std::array<float,4> aux{};
    float busA=0,busB=0,laneA=0,laneB=0; bool busAConnected=false;
};
// Control-source indices. 0-11 match the Pd patch; 12+ are native additions.
enum SourceId { SRC_FUN1=5, SRC_FUN2=6, SRC_AMPENV=11, SRC_FUN3=12, SRC_FUN4=13, SRC_AUX1=14,
    SRC_BUS_A=18, SRC_BUS_B=19, SRC_LANE_A=20, SRC_LANE_B=21, SRC_ONE=22 };
constexpr int BUS_SOURCE_DSP=SOURCE_COUNT, BUS_SOURCE_MIX=SOURCE_COUNT+1;
constexpr int ORIGINAL_PARAM_COUNT=80;
constexpr int FUN_COUNT=4;
inline int funSource(int f) { return f<2 ? SRC_FUN1+f : SRC_FUN3+(f-2); }
inline int sourceFun(int src) { return src==SRC_FUN1||src==SRC_FUN2 ? src-SRC_FUN1 : (src==SRC_FUN3||src==SRC_FUN4 ? src-SRC_FUN3+2 : -1); }
inline int funBase(int f) { static constexpr int base[FUN_COUNT]={FUN1_A,FUN2_A,FUN3_A,FUN4_A}; return base[f]; }
// Read-only view of the loaded sample for the SAMPLE pitch wave (native-only).
// 0 V / MIDI 60 plays the sample at its recorded pitch, as a K2000 keymap root.
// pcmR: a stereo sample's right channel, `frames` long; null for mono.
struct SampleView {
    const int16_t* pcm=nullptr; size_t frames=0; double rate=0; bool loop=false; const int16_t* pcmR=nullptr;
    bool valid() const { return pcm && frames>=2 && rate>0; }
};
constexpr int SAMPLE_WAVE=5;
constexpr int BUS_WAVE=6;
inline bool positiveBit(float value) { return std::isfinite(value) && value>0; }
inline float rotateBus(float value,float control) {
    double x=double(value)+1.+2.*(double(control)-60.)/12.;
    return std::isfinite(x) ? float(x-2.*std::floor(x/2.)-1.) : 0.f;
}
constexpr float SAMPLE_ROOT_NOTE=60;
struct Voice {
    Phasor pitch; Noise noise; Lfo lfo[2]; Envelope env[3]; FunctionBlock block[3];
    // A stereo sample's right channel takes its own copy of the three blocks, with the same
    // controls; stereoActive while it does.
    FunctionBlock blockR[3]; bool stereoActive=false;
    uint32_t noteRandom=1; Ramp wheel; bool gate=false; float velocity=0,random=0; std::array<float,SOURCE_COUNT> sources{};
    double samplePos=0; bool samplePlaying=false;
    float tap=0;
    // 4-point Hermite read over the half-open range [0, frames). One-shot clamps
    // the interpolation neighbours at the ends; a loop wraps them across the seam,
    // so the loop period is exactly `frames` samples at the root pitch.
    // right: a stereo sample's right channel at the same position (0 for mono).
    float sampleTick(const SampleView* s,float hz,float sr,float* right=nullptr) {
        static const double rootHz=mtof(SAMPLE_ROOT_NOTE);
        if(right) *right=0;
        if(!s || !s->valid() || !samplePlaying) return 0;
        const double n=double(s->frames);
        if(samplePos>=n) {
            if(!s->loop) { samplePlaying=false; return 0; }
            samplePos=std::fmod(samplePos,n);
        }
        const long frames=long(s->frames);
        long i=long(samplePos); float f=float(samplePos-double(i));
        auto hermite=[&](const int16_t* pcm) {
            auto at=[&](long k) {
                k=s->loop ? ((k%frames)+frames)%frames : std::max(0L,std::min(frames-1,k));
                return pcm[k]*(1.f/32768);
            };
            float y0=at(i-1), y1=at(i), y2=at(i+1), y3=at(i+2);
            float c1=.5f*(y2-y0), c2=y0-2.5f*y1+2*y2-.5f*y3, c3=.5f*(y3-y0)+1.5f*(y1-y2);
            return ((c3*f+c2)*f+c1)*f+y1;
        };
        float out=hermite(s->pcm);
        if(right && s->pcmR) *right=hermite(s->pcmR);
        samplePos+=double(hz)/rootHz*s->rate/double(sr);
        return out;
    }
    explicit Voice(uint32_t seed=307) : noise(seed) { noteRandom=seed+701; lfo[0].noise.state=seed+177; lfo[1].noise.state=seed+393; }
    void prepare(float sr) { for(auto& b:block) b.prepare(sr); for(auto& b:blockR) b.prepare(sr); }
    void panic() {
        gate=false; for(auto& e:env) e=Envelope{};
        for(auto* set:{block,blockR}) for(int k=0;k<3;++k) { auto& b=set[k]; b.comb.clear(); b.ladder.state={}; b.lop.last=b.hipLop.last=0; b.lp2=Vcf{}; b.bp=Vcf{}; b.notch=Vcf{}; }
        stereoActive=false;
        tap=0;
    }
    float control(int base,const Parameters& p,float note) const {
        return p[base]+p[base+1]*(note-60)+p[base+2]*velocity+
            sources[choice(p[base+3],SOURCE_COUNT-1)]*p[base+4]+sources[choice(p[base+5],SOURCE_COUNT-1)]*p[base+6];
    }
    float evaluateFun(int f,const Parameters& p,unsigned& busy,unsigned& done) {
        unsigned bit=1u<<f;
        if(done&bit) return sources[funSource(f)];
        if(busy&bit) return 0; // Unsupported cyclic edges are zero, never stale feedback.
        busy|=bit; int base=funBase(f);
        auto read=[&](int src) { int g=sourceFun(src); return g>=0 ? evaluateFun(g,p,busy,done) : sources[src]; };
        float a=read(choice(p[base],SOURCE_COUNT-1)), b=read(choice(p[base+1],SOURCE_COUNT-1));
        float value=fun(choice(p[base+2],7),a,b);
        sources[funSource(f)]=p[FUN1_BIT+f]>=.5f ? float(positiveBit(value)) : value;
        busy&=~bit; done|=bit; return sources[funSource(f)];
    }
    void tickControl(const Parameters& p,const VoiceInput& in,float sr) {
        if(in.gate!=gate) {
            gate=in.gate;
            if(gate) { velocity=clamp(in.velocity,0,1); noteRandom=noteRandom*472940017u+832416023u; samplePos=0; samplePlaying=true;
                random=float(int(10001.*noteRandom/4294967296.))/5000.f-1;
                for(auto& l:lfo) l.reset(); }
            for(int i=0;i<3;++i) { int b=AMP_A+4*i; env[i].gate(gate,p[b],p[b+1],p[b+2],p[b+3],sr); }
        }
        float mw=clamp(in.modwheel,0,1);
        // Continuous CV slews at a full-scale / 20 ms rate; step inputs take 20 ms.
        if(mw!=wheel.target) wheel.set(mw,20,sr);
        sources[0]=0; sources[1]=env[1].tick(); sources[2]=env[2].tick();
        sources[3]=lfo[0].tick(p[LFO1_RATE],choice(p[LFO1_SHAPE],4),sr);
        sources[4]=lfo[1].tick(p[LFO2_RATE],choice(p[LFO2_SHAPE],4),sr);
        sources[7]=wheel.tick(); sources[8]=velocity; sources[9]=in.note/127;
        sources[10]=random; sources[11]=env[0].tick();
        for(int k=0;k<4;++k) sources[SRC_AUX1+k]=std::isfinite(in.aux[k]) ? clamp(in.aux[k],-1,1) : 0.f;
        sources[SRC_BUS_A]=in.busA; sources[SRC_BUS_B]=in.busB;
        sources[SRC_LANE_A]=in.laneA; sources[SRC_LANE_B]=in.laneB; sources[SRC_ONE]=1;
        unsigned busy=0,done=0; for(int f=0;f<FUN_COUNT;++f) evaluateFun(f,p,busy,done);
    }
    std::array<float,2> tickAudio(const Parameters& p,const VoiceInput& in,float sr,const SampleView* sample=nullptr) {
        float pitchControl=control(PITCH_ADJ,p,in.note);
        float x=0,xR=0; bool stereo=false;
        if(choice(p[PITCH_WAVE],BUS_WAVE)==BUS_WAVE) x=in.busAConnected ? rotateBus(in.busA,pitchControl) : 0.f;
        else {
        float pitchHz=mtof(clamp(pitchControl,-20,135));
        float ph=pitch.tick(pitchHz,sr), n=noise.tick();
        switch(choice(p[PITCH_WAVE],BUS_WAVE)) { case 1:x=(ph>clamp(p[PITCH_PW],.01,.99)?1.f:-1.f); break;
            case 2:x=1-std::abs(ph*4-2); break; case 3:x=cosine(ph); break; case 4:x=n; break;
            case SAMPLE_WAVE:x=sampleTick(sample,pitchHz,sr,&xR); stereo=sample && sample->valid() && sample->pcmR; break;
            default:x=ph*2-1; }
        }
        const float* r=routes[std::max(0,choice(p[ALG],6)-1)];
        const int t1=choice(p[F1_TYPE],14), t2=choice(p[F2_TYPE],14), t3=choice(p[F3_TYPE],14);
        const float k1=control(F1_ADJ,p,in.note), k2=control(F2_ADJ,p,in.note), k3=control(F3_ADJ,p,in.note);
        // Entering stereo, the right blocks take the left blocks' state before either side runs,
        // so a stereo sample with identical channels sounds exactly like the mono one, even mid-note.
        if(stereo && !stereoActive) { for(int k=0;k<3;++k) blockR[k]=block[k]; stereoActive=true; }
        float a=block[0].tick(t1,x,k1,p[F1_B],sr);
        float b=block[1].tick(t2,r[0]*x+r[1]*a,k2,p[F2_B],sr);
        float c=block[2].tick(t3,r[2]*x+r[3]*a+r[4]*b+r[5]*a*b,k3,p[F3_B],sr);
        tap=r[6]*a+r[7]*b+r[8]*c;
        const float amp=dbgain(control(AMP_ADJ,p,in.note));
        float out=(r[6]*a+r[7]*b+r[8]*c)*amp*sources[11]*sources[11];
        float pan=clamp(control(PAN_ADJ,p,in.note),-1,1), t=(pan+1)*.125f;
        if(stereo) {   // PAN is balance: each side keeps its own gain from the pan law
            float aR=blockR[0].tick(t1,xR,k1,p[F1_B],sr);
            float bR=blockR[1].tick(t2,r[0]*xR+r[1]*aR,k2,p[F2_B],sr);
            float cR=blockR[2].tick(t3,r[2]*xR+r[3]*aR+r[4]*bR+r[5]*aR*bR,k3,p[F3_B],sr);
            float outR=(r[6]*aR+r[7]*bR+r[8]*cR)*amp*sources[11]*sources[11];
            return {{out*cosine(t),outR*cosine(t-.25f)}};
        }
        stereoActive=false;
        return {{out*cosine(t),out*cosine(t-.25f)}};
    }
    std::array<float,2> tick(const Parameters& p,const VoiceInput& in,float sr,const SampleView* sample=nullptr) {
        tickControl(p,in,sr); return tickAudio(p,in,sr,sample);
    }
};
struct Engine {
    std::array<Voice,8> voices;
    Ramp master; Highpass dc[2]; float sampleRate=48000; uint64_t faults=0;
    Engine() { (void)cosineTable(); for(int i=0;i<8;++i) voices[i]=Voice(307+i*1009); prepare(48000); }
    void prepare(float sr) { sampleRate=sr; for(auto& v:voices) v.prepare(sr); }
    void panic() { for(auto& v:voices) v.panic(); for(auto& d:dc) d.last=0; }
    // usedVoices = polyphony channels actually patched. With the default of 8 every
    // voice free-runs, as in the Pd clone (the reference tests rely on this). Voices
    // at or above usedVoices are skipped once their gate is low and their amp
    // envelope has finished, so a mono patch no longer pays for eight voices; a
    // voice that is still releasing keeps running until its tail ends.
    std::array<float,2> tick(const Parameters& p,const std::array<VoiceInput,8>& inputs,int usedVoices=8,const SampleView* sample=nullptr,bool controlAll=false,bool audioAll=false) {
        std::array<float,2> sum{};
        for(int i=0;i<8;++i) {
            if(!audioAll && i>=usedVoices && !inputs[i].gate && !voices[i].gate && voices[i].env[0].stage==Envelope::Idle) {
                if(controlAll) voices[i].tickControl(p,inputs[i],sampleRate);
                continue;
            }
            auto y=voices[i].tick(p,inputs[i],sampleRate,sample); sum[0]+=y[0]; sum[1]+=y[1];
        }
        float gain=dbgain(p[MASTER]); if(gain!=master.target) master.set(gain,50,sampleRate);
        gain=master.tick();
        for(int c=0;c<2;++c) {
            sum[c]=dc[c].tick(sum[c]*gain,5,sampleRate);
            if(!std::isfinite(sum[c])) { ++faults; panic(); sum={}; break; }
        }
        return sum;
    }
};
} // namespace vastish
