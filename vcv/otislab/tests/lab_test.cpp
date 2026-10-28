// LAB (non-authentic) feature tests: continuous chip clock, interpolation bits,
// resample record source, per-voice start/end. Authentic defaults are covered by
// engine_test and the reference comparisons.
#include "../src/engine/OtisEngine.hpp"
#include <cassert>
#include <complex>
#include <iostream>
#include <memory>
using namespace otis;

// Frequency of the strongest spectral peak (simple DFT scan, 5 Hz steps).
static double peakHz(const std::vector<float>& x, double rate, double lo, double hi) {
    double best=0, bestHz=0;
    for(double hz=lo; hz<=hi; hz+=5) {
        std::complex<double> acc=0;
        for(size_t i=0;i<x.size();i++) acc+=double(x[i])*std::polar(1.0,-2*pi*hz*i/rate);
        if(std::abs(acc)>best) { best=std::abs(acc); bestHz=hz; }
    }
    return bestHz;
}
// Test tones use an integer period in samples so the loop seam is clean.
static double actualHz(double hz) { double sr=captureRate(5); return sr/std::round(sr/hz); }
static std::unique_ptr<Engine> sineEngine(double hz, double host=48000) {
    auto e=std::make_unique<Engine>(); e->setHostRate(host);
    double sr=captureRate(5); size_t period=size_t(std::round(sr/hz)), n=period*size_t(sr/period);
    std::vector<int16_t> s(n);
    for(size_t i=0;i<n;i++) s[i]=quantize(float(.5*std::sin(2*pi*double(i%period)/period)));
    e->install(s,sr); e->controls.loop=1; return e;
}
static std::vector<float> run(Engine& e, int n, int skip=2000) {
    for(int i=0;i<skip;i++) e.process(0);
    std::vector<float> out(n); for(auto& y:out) y=e.process(0); return out;
}

int main() {
    // Host clocks through Rack's maximum keep capture duration and chip timing.
    for(double host:{705600.,768000.}) {
        auto e=sineEngine(440,host); e->trigger(0);
        const auto initial=e->voices[0].accumulator;
        e->controls.captureIndex=5; e->startRecord();
        for(int i=0;i<int(host/10);++i) e->process(.1f);
        assert(std::abs(double(e->recordFrames)-captureRate(5)*.1)<1);
        // Count chip ticks using the converter's ring head before stopping the take.
        const size_t ticks=size_t(std::floor(chipRates[0]*.1));
        assert(e->conversion[0].head==ticks%OutputConverter::taps);
        assert(e->voices[0].accumulator!=initial);
    }
    // 1. Resolution mode: clock moves, pitch stays put.
    for(float oct:{0.f,-1.f,-2.f,1.f}) {
        auto e=sineEngine(440); e->controls.clockOctaves=oct; e->trigger(0);
        double hz=peakHz(run(*e,9600),48000,300,600);
        assert(std::abs(hz-actualHz(440))<=10);
    }
    // 2. Varispeed: pitch follows the clock.
    {
        auto e=sineEngine(440); e->controls.varispeed=true; e->controls.clockOctaves=-1; e->trigger(0);
        assert(std::abs(peakHz(run(*e,9600),48000,150,600)-actualHz(440)/2)<=10);
        auto f=sineEngine(440); f->controls.varispeed=true; f->controls.clockOctaves=.5f; f->trigger(0);
        assert(std::abs(peakHz(run(*f,9600),48000,300,900)-actualHz(440)*std::sqrt(2.0))<=10);
    }
    // 3. Lower clock: content above the new Nyquist folds back (pitch preserved, 3 kHz tone
    //    at a 5.26 kHz chip clock aliases to about 2.26 kHz).
    {
        auto a=sineEngine(3000); a->trigger(0); assert(std::abs(peakHz(run(*a,4800),48000,1500,4000)-actualHz(3000))<=10);
        auto b=sineEngine(3000); b->controls.clockOctaves=-2.5f; b->trigger(0);
        double alias=chipRates[0]*std::exp2(-2.5)-actualHz(3000);
        assert(std::abs(peakHz(run(*b,4800),48000,1500,4000)-alias)<=15);
    }
    // 4. Interpolation bits: drop-sample transposition leaves far more non-fundamental energy
    //    (residual after a least-squares sine fit) than the chip's interpolation.
    {
        auto residual=[](const std::vector<float>& x,double hz,double rate) {
            double ss=0,sc=0,cc=0,xs=0,xc=0,e=0;
            for(size_t i=0;i<x.size();i++) { double sn=std::sin(2*pi*hz*i/rate), cs=std::cos(2*pi*hz*i/rate);
                ss+=sn*sn; cc+=cs*cs; sc+=sn*cs; xs+=x[i]*sn; xc+=x[i]*cs; e+=double(x[i])*x[i]; }
            double det=ss*cc-sc*sc, a=(xs*cc-xc*sc)/det, b=(xc*ss-xs*sc)/det, fit=0;
            for(size_t i=0;i<x.size();i++) { double r=x[i]-a*std::sin(2*pi*hz*i/rate)-b*std::cos(2*pi*hz*i/rate); fit+=r*r; }
            return fit/e;
        };
        // The chip's pitch register quantizes the ratio, so fit at the best nearby frequency.
        auto bestResidual=[&](const std::vector<float>& x,double hz,double rate) {
            double best=1; for(double d=-3;d<=3;d+=.02) best=std::min(best,residual(x,hz+d,rate)); return best; };
        double hz=actualHz(1000)*std::exp2(-1.3/12);
        auto a=sineEngine(1000); a->controls.pitch[0]=-1.3f/12; a->trigger(0); double chip=bestResidual(run(*a,9600),hz,48000);
        auto b=sineEngine(1000); b->controls.pitch[0]=-1.3f/12; b->controls.interpBits=0; b->trigger(0); double drop=bestResidual(run(*b,9600),hz,48000);
        assert(drop>chip*10 && drop>1e-4);
        auto c=sineEngine(1000); c->controls.interpBits=9; c->trigger(0); c->process(0);
        assert(c->voices[0].interpMask==fracOne-1);
    }
    // 5. Switching between authentic and LAB clock paths stays finite and continuous.
    {
        auto e=sineEngine(220); e->trigger(0); float prev=0, steady=0, atSwitch=0;
        for(int i=0;i<20000;i++) {
            if(i==8000) e->controls.clockOctaves=-.75f;
            if(i==14000) e->controls.clockOctaves=0;
            float y=e->process(0); assert(std::isfinite(y));
            float d=std::abs(y-prev); prev=y;
            if(i>2000 && i<7000) steady=std::max(steady,d);
            if((i>=8000&&i<8100)||(i>=14000&&i<14100)) atSwitch=std::max(atSwitch,d);
        }
        assert(atSwitch<steady*3);
        e->controls.clockOctaves=std::nanf(""); for(int i=0;i<1000;i++) assert(std::isfinite(e->process(0)));
        e->controls.clockOctaves=1e9f; for(int i=0;i<1000;i++) assert(std::isfinite(e->process(0)));
        e->controls.clockOctaves=-1e9f; for(int i=0;i<1000;i++) assert(std::isfinite(e->process(0)));
    }
    // 6. Resample: recording the module's own output captures what it plays.
    {
        auto e=sineEngine(330); e->trigger(0); for(int i=0;i<4800;i++) e->process(0);
        double outEnergy=0; for(int i=0;i<4800;i++) { float y=e->process(0); outEnergy+=y*y; }
        e->controls.recordSource=1; e->controls.captureIndex=5; e->startRecord();
        for(int i=0;i<24000;i++) e->process(0);
        e->stopRecord(); assert(!e->capturePending && e->frames>10000);
        double takeEnergy=0; for(size_t i=0;i<e->frames;i++) { double v=e->sample.pcm[i]/32768.0; takeEnergy+=v*v; }
        double outRms=std::sqrt(outEnergy/4800), takeRms=std::sqrt(takeEnergy/e->frames);
        assert(std::abs(takeRms/outRms-1)<.1);
        // AUDIO-only source with silent input records silence.
        auto f=sineEngine(330); f->trigger(0); f->controls.recordSource=0; f->startRecord();
        for(int i=0;i<4800;i++) f->process(0);
        f->stopRecord(); for(size_t i=0;i<f->frames;i++) assert(f->sample.pcm[i]==0);
    }
    // 7. Per-voice START/END: each voice gets its own segment.
    {
        auto e=sineEngine(100); e->controls.startv[1]=.5f; e->controls.endv[1]=.75f;
        e->trigger(0); e->trigger(1); e->process(0);
        uint32_t half=uint32_t(baseAddress+size_t(.5f*(e->frames-1)))<<fracBits;
        assert(e->voices[0].start==uint32_t(baseAddress)<<fracBits);
        assert(e->voices[1].start==half && e->voices[1].end>half);
    }
    std::cout<<"PASS: LAB clock (resolution and varispeed), aliasing fold-back, interpolation bits, path switching, resample source, per-voice start/end\n";
}
