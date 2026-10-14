// Pd 0.55 DSP equations adapted from Miller Puckette's BSD-licensed sources.
// See THIRD_PARTY_NOTICES.md and docs/PURE-DATA-LICENSE.txt.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace vastish {
inline float clamp(float x, float lo, float hi) { return std::max(lo, std::min(hi,x)); }
inline float mtof(float note) { return float(8.17579891564 * std::exp(0.0577622650 * note)); }
inline float dbgain(float db) { return db <= -100 ? 0.f : float(std::exp(2.302585092994046 * .05 * std::min(db,385.f))); }
inline const std::array<float,2049>& cosineTable() {
    static const std::array<float,2049> t = [] {
        std::array<float,2049> a{};
        for (int i=0;i<=2048;++i) a[i]=float(std::cos(6.283185307179586*i/2048));
        a[0]=a[2048]=1; a[512]=a[1536]=0; a[1024]=-1; return a;
    }(); return t;
}
inline float tableAt(float index) {
    if(!std::isfinite(index)) return 0;
    index -= std::floor(index/2048.f)*2048.f;
    int whole=int(index); int i=whole & 2047; float f=index-whole;
    const auto& t=cosineTable(); return t[i]+f*(t[i+1]-t[i]);
}
inline float cosine(float cycles) { return tableAt(cycles*2048.f); }
struct Phasor {
    double phase=0;
    float tick(float hz, float sr) {
        float out=float(phase);
        phase += double(hz * (1.f/sr)); phase-=std::floor(phase); return out;
    }
};
struct Noise {
    uint32_t state;
    explicit Noise(uint32_t seed=307) : state(seed) {}
    float tick() {
        float out=float(int32_t(state&0x7fffffff)-int32_t(0x40000000))*(1.f/0x40000000);
        state=state*435898247u+382842987u; return out;
    }
};
struct Lowpass {
    float last=0;
    float tick(float x, float hz, float sr) {
        float c=clamp(hz*(2.f*3.14159f)/sr,0,1);
        return last=c*x+(1-c)*last;
    }
};
struct Highpass {
    float last=0;
    float tick(float x, float hz, float sr) {
        float c=clamp(1-hz*(2.f*3.14159f)/sr,0,1);
        float n=x+c*last, y=.5f*(1+c)*(n-last); last=n; return y;
    }
};
struct Vcf {
    float re=0, im=0;
    std::array<float,2> tick(float x, float hz, float q, float sr) {
        float cf=std::max(0.f,hz*(6.28318f/sr));
        float idx=cf*(2048/6.28318f);
        float r=q>0 ? std::max(0.f,1-cf/q) : 0;
        float cr=r*tableAt(idx), ci=r*tableAt(idx-512);
        float old=re;
        re=(2-2/(q+2))*(1-r)*x+cr*old-ci*im;
        im=ci*old+cr*im; return {{re,im}};
    }
};
struct Ladder {
    std::array<double,4> state{};
    static double saturate(double v) {
        float c=clamp(float(v/3),-1,1); return 3*(c-(1./3.)*c*c*c);
    }
    static std::array<double,4> derivatives(const std::array<double,4>& s, float x, float hz, float res) {
        double k=double(float(2*3.14159))*hz;
        double a=saturate(s[0]),b=saturate(s[1]),c=saturate(s[2]);
        return {{k*(saturate(x-res*s[3])-a), k*(a-b), k*(b-c), k*(c-saturate(s[3]))}};
    }
    float tick(float x,float hz,float res,float sr) {
        double h=1./(2*sr);
        for(int over=0;over<2;++over) {
            auto a=derivatives(state,x,hz,res); std::array<double,4> tmp;
            for(int i=0;i<4;++i) tmp[i]=state[i]+.5*h*a[i];
            auto b=derivatives(tmp,x,hz,res);
            for(int i=0;i<4;++i) tmp[i]=state[i]+.5*h*b[i];
            auto c=derivatives(tmp,x,hz,res);
            for(int i=0;i<4;++i) tmp[i]=state[i]+h*c[i];
            auto d=derivatives(tmp,x,hz,res);
            for(int i=0;i<4;++i) state[i]+=h/6*(a[i]+2*b[i]+2*c[i]+d[i]);
        }
        return float(state[3]);
    }
};
struct Comb {
    std::vector<float> buffer; size_t pos=0;
    void prepare(float sr) { buffer.assign(size_t(std::ceil(.3*sr))+20,0); pos=0; }
    void clear() { std::fill(buffer.begin(),buffer.end(),0); pos=0; }
    float ago(int n) const { return buffer[(pos+buffer.size()-size_t(n))%buffer.size()]; }
    float tick(float x,float hz,float feedback,float sr) {
        float delay=clamp(sr/std::max(hz,5.f),17.00001f,float(buffer.size()-4));
        int n=int(delay); float f=delay-n;
        float a=ago(n-1), b=ago(n), c=ago(n+1), d=ago(n+2), diff=c-b;
        float y=b+f*(diff-.1666667f*(1-f)*((d-a-3*diff)*f+(d+2*a-3*b)));
        float sum=x+clamp(feedback,-.98f,.98f)*y;
        buffer[pos]=sum; pos=(pos+1)%buffer.size(); return sum*.5f;
    }
};
struct Ramp {
    float value=0, target=0, step=0; int remaining=0;
    void set(float to,float ms,float sr) {
        target=to; remaining=std::max(0,int(std::round(ms*.001*sr)));
        if(remaining) step=(target-value)/remaining; else value=target;
    }
    float tick() { if(remaining>0) { value+=step; if(!--remaining) value=target; } return value; }
};
struct Envelope {
    Ramp ramp; enum Stage { Idle, Attack, Decay, Sustain, Release } stage=Idle;
    float decay=0, sustain=0, sr=48000;
    void gate(bool on,float a,float d,float s,float r,float rate) {
        sr=rate;
        if(on) { decay=d; sustain=s; stage=Attack; ramp.set(1,a,sr); }
        else { stage=Release; ramp.set(0,r,sr); }
    }
    float tick() {
        // Zero-length attack jumps to 1 then begins decay on this sample.
        if(stage==Attack && !ramp.remaining) { stage=Decay; ramp.set(sustain,decay,sr); }
        float out=ramp.tick();
        if(stage==Attack && !ramp.remaining) { stage=Decay; ramp.set(sustain,decay,sr); }
        else if(stage==Decay && !ramp.remaining) stage=Sustain;
        else if(stage==Release && !ramp.remaining) stage=Idle;
        return out;
    }
};
struct Lfo {
    Phasor phase; Noise noise; float held=0, previous=0;
    float tick(float rate,int shape,float sr) {
        float p=phase.tick(rate,sr), n=noise.tick(); if(p<previous) held=n; previous=p;
        switch(shape) { case 1:return 1-std::abs(p*4-2); case 2:return p*2-1;
            case 3:return p<.5f?1:-1; case 4:return held; default:return cosine(p); }
    }
    void reset() { phase.phase=0; }
};
struct FunctionBlock {
    // Each Pd switch~ branch freezes independently when not selected.
    Lowpass lop, hipLop; Vcf lp2,bp,notch; Ladder ladder;
    Phasor sine,ring,decimate; float held=0,prev=0; Comb comb;
    void prepare(float sr) { comb.prepare(sr); }
    float tick(int type,float x,float a,float b,float sr) {
        switch(type) {
            case 1:return lop.tick(x,clamp(mtof(clamp(a,-20,135)),5,20000),sr);
            case 2:return x-hipLop.tick(x,clamp(mtof(clamp(a,-20,135)),5,20000),sr);
            case 3:return lp2.tick(x,clamp(mtof(clamp(a,-20,135)),5,16000),clamp(b,.5,40),sr)[1];
            case 4:return bp.tick(x,clamp(mtof(clamp(a,-20,135)),5,16000),clamp(b,.5,40),sr)[0];
            case 5:return x-notch.tick(x,clamp(mtof(clamp(a,-20,135)),5,16000),clamp(b,.5,40),sr)[0];
            case 6:return ladder.tick(x,clamp(mtof(clamp(a,-20,135)),5,12000),clamp(b,0,1)*3.9f,sr);
            case 7:return cosine(x*clamp(a,.05,16)*.25f-.25f);
            case 8:return std::tanh(x*dbgain(a))*.8f;
            case 9:return cosine(sine.tick(mtof(clamp(a,-20,135)),sr));
            case 10:return x*cosine(ring.tick(mtof(clamp(a,-20,135)),sr));
            case 11:return x*dbgain(a);
            case 12: { float q=std::pow(2.f,clamp(a,1,16)-1); return std::floor(x*q+.5f)/q; }
            case 13: { float p=decimate.tick(clamp(mtof(clamp(a,-20,135)),5,44100),sr); if(p<prev) held=x; prev=p; return held; }
            case 14:return comb.tick(x,mtof(clamp(a,-20,100)),b,sr);
            default:return x;
        }
    }
};
} // namespace vastish
