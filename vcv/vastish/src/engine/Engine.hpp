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
struct VoiceInput { float note=60,velocity=1,modwheel=0; bool gate=false; };
// Read-only view of the loaded sample for the SAMPLE pitch wave (native-only).
// 0 V / MIDI 60 plays the sample at its recorded pitch, as a K2000 keymap root.
struct SampleView {
    const int16_t* pcm=nullptr; size_t frames=0; double rate=0; bool loop=false;
    bool valid() const { return pcm && frames>=2 && rate>0; }
};
constexpr int SAMPLE_WAVE=5;
constexpr float SAMPLE_ROOT_NOTE=60;
struct Voice {
    Phasor pitch; Noise noise; Lfo lfo[2]; Envelope env[3]; FunctionBlock block[3];
    uint32_t noteRandom=1; Ramp wheel; bool gate=false; float velocity=0,random=0; std::array<float,12> sources{};
    double samplePos=0; bool samplePlaying=false;
    // 4-point Hermite read of the sample; one-shot unless the view loops.
    float sampleTick(const SampleView* s,float hz,float sr) {
        static const double rootHz=mtof(SAMPLE_ROOT_NOTE);
        if(!s || !s->valid() || !samplePlaying) return 0;
        double n=double(s->frames);
        if(samplePos>=n-1) {
            if(!s->loop) { samplePlaying=false; return 0; }
            samplePos=std::fmod(samplePos,n-1);
        }
        size_t i=size_t(samplePos); float f=float(samplePos-double(i));
        auto at=[&](long k) { k=std::max(0L,std::min(long(s->frames)-1,k)); return s->pcm[k]*(1.f/32768); };
        float y0=at(long(i)-1), y1=at(long(i)), y2=at(long(i)+1), y3=at(long(i)+2);
        float c1=.5f*(y2-y0), c2=y0-2.5f*y1+2*y2-.5f*y3, c3=.5f*(y3-y0)+1.5f*(y1-y2);
        float out=((c3*f+c2)*f+c1)*f+y1;
        samplePos+=double(hz)/rootHz*s->rate/double(sr);
        return out;
    }
    explicit Voice(uint32_t seed=307) : noise(seed) { noteRandom=seed+701; lfo[0].noise.state=seed+177; lfo[1].noise.state=seed+393; }
    void prepare(float sr) { for(auto& b:block) b.prepare(sr); }
    void panic() { gate=false; for(auto& e:env) e=Envelope{}; for(auto& b:block) { b.comb.clear(); b.ladder.state={}; b.lop.last=b.hipLop.last=0; b.lp2=Vcf{}; b.bp=Vcf{}; b.notch=Vcf{}; } }
    float control(int base,const Parameters& p,float note) const {
        return p[base]+p[base+1]*(note-60)+p[base+2]*velocity+
            sources[choice(p[base+3],11)]*p[base+4]+sources[choice(p[base+5],11)]*p[base+6];
    }
    float evaluateFun(int f,const Parameters& p,unsigned& busy,unsigned& done) {
        unsigned bit=1u<<f;
        if(done&bit) return sources[5+f];
        if(busy&bit) return 0; // Unsupported cyclic edges are zero, never stale feedback.
        busy|=bit; int base=FUN1_A+f*3;
        auto read=[&](int src) { return src==5||src==6 ? evaluateFun(src-5,p,busy,done) : sources[src]; };
        float a=read(choice(p[base],11)), b=read(choice(p[base+1],11));
        sources[5+f]=fun(choice(p[base+2],7),a,b);
        busy&=~bit; done|=bit; return sources[5+f];
    }
    std::array<float,2> tick(const Parameters& p,const VoiceInput& in,float sr,const SampleView* sample=nullptr) {
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
        unsigned busy=0,done=0; evaluateFun(0,p,busy,done); evaluateFun(1,p,busy,done);
        float pitchHz=mtof(clamp(control(PITCH_ADJ,p,in.note),-20,135));
        float ph=pitch.tick(pitchHz,sr);
        float n=noise.tick(), x;
        switch(choice(p[PITCH_WAVE],SAMPLE_WAVE)) { case 1:x=(ph>clamp(p[PITCH_PW],.01,.99)?1.f:-1.f); break;
            case 2:x=1-std::abs(ph*4-2); break; case 3:x=cosine(ph); break; case 4:x=n; break;
            case SAMPLE_WAVE:x=sampleTick(sample,pitchHz,sr); break; default:x=ph*2-1; }
        const float* r=routes[std::max(0,choice(p[ALG],6)-1)];
        float a=block[0].tick(choice(p[F1_TYPE],14),x,control(F1_ADJ,p,in.note),p[F1_B],sr);
        float b=block[1].tick(choice(p[F2_TYPE],14),r[0]*x+r[1]*a,control(F2_ADJ,p,in.note),p[F2_B],sr);
        float c=block[2].tick(choice(p[F3_TYPE],14),r[2]*x+r[3]*a+r[4]*b+r[5]*a*b,control(F3_ADJ,p,in.note),p[F3_B],sr);
        float out=(r[6]*a+r[7]*b+r[8]*c)*dbgain(control(AMP_ADJ,p,in.note))*sources[11]*sources[11];
        float pan=clamp(control(PAN_ADJ,p,in.note),-1,1), t=(pan+1)*.125f;
        return {{out*cosine(t),out*cosine(t-.25f)}};
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
    std::array<float,2> tick(const Parameters& p,const std::array<VoiceInput,8>& inputs,int usedVoices=8,const SampleView* sample=nullptr) {
        std::array<float,2> sum{};
        for(int i=0;i<8;++i) {
            if(i>=usedVoices && !inputs[i].gate && !voices[i].gate && voices[i].env[0].stage==Envelope::Idle) continue;
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
