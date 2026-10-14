// SPDX-License-Identifier: BSD-3-Clause
// Playback arithmetic adapted from MAME's ES5505/6 device, copyright Aaron Giles.
// Host, sampling and control code copyright 2026 OtisLab contributors.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace otis {
constexpr double pi = 3.14159265358979323846;
constexpr size_t memoryWords = (1u << 20) - 256;
constexpr unsigned baseAddress = 128; // guards prevent reverse accumulator underflow
constexpr unsigned fracBits = 11;
constexpr uint32_t fracOne = 1u << fracBits;
constexpr double chipRates[3] = {10000000.0/336.0, 10000000.0/224.0, 10000000.0/128.0};
constexpr int voiceLimits[3] = {16, 14, 8};
inline double captureRate(int index) { return 10000000.0 / (112.0 * (8 - std::max(0, std::min(6,index)))); }
template<class T> T clamp(T x, T a, T b) { return std::max(a, std::min(b,x)); }
inline int16_t quantize(float x) {
    if (!std::isfinite(x)) return 0;
    return static_cast<int16_t>(std::lround(clamp(double(x), -1.0, 32767.0/32768.0) * 32768.0));
}
// Explicit arithmetic shift: negative rounding follows the integer chip model.
inline int32_t shiftFloor(int64_t x, int bits) {
    int64_t d = int64_t(1) << bits;
    return static_cast<int32_t>(x >= 0 ? x/d : -((-x+d-1)/d));
}
// ES5505 8-bit log volume: 4-bit exponent, 4-bit mantissa (MAME compute_tables(8,4,4)).
// output = (sample * table[reg]) >> 11, arithmetic shift. 0xc0 is exactly unity; 0 is silence.
constexpr uint8_t unityVolume = 0xc0;
inline const std::array<int32_t,256>& volumeTable() {
    static const std::array<int32_t,256> table = [] {
        std::array<int32_t,256> t{};
        for(int i=0;i<256;i++) { uint32_t exponent=(i>>4)&15, mantissa=(i&15)|16; t[i]=int32_t((mantissa<<11)>>(16-exponent)); }
        return t;
    }();
    return table;
}
inline int32_t applyVolume(int32_t x, uint8_t reg) {
    int64_t p=int64_t(x)*volumeTable()[reg];
    return static_cast<int32_t>(p>=0 ? p>>11 : -((-p+2047)>>11)); // floor, as MAME rshift_signed
}
// Level CV (0..1) to a volume register, never above unity.
// Chip mode: register is linear in CV, so 10 V = 0xc0 and each 0.52 V is about 6 dB (dB-linear, like a log VCA).
// Linear mode: nearest register to a linear amplitude CV/10, still quantized through the chip table.
inline uint8_t levelRegister(float level, bool linear) {
    double v=std::isfinite(level) ? clamp(double(level),0.0,1.0) : 0.0;
    if(!linear) return uint8_t(std::lround(v*unityVolume));
    if(v<=0) return 0;
    const auto& t=volumeTable(); double target=v*2048.0; int best=0; double err=1e30;
    for(int i=0;i<=unityVolume;i++) { double e=std::fabs(t[i]-target); if(e<err) { err=e; best=i; } }
    return uint8_t(best);
}
struct Sample {
    std::vector<int16_t> pcm;
    double rate = captureRate(5);
    Sample() : pcm(memoryWords, 0) {}
};
struct Voice {
    bool running=false, reverse=false;
    uint32_t accumulator=0, start=0, end=0, frequency=0;
    uint16_t k1=0xfff0, k2=0xfff0;
    uint8_t volume=unityVolume;
    // Host-side declick (not chip behaviour): linear fade over fadeLength chip samples.
    int fadeRemaining=0, fadeLength=0;
    bool restartAfterFade=false;
    int filterMode=2, loop=0;
    // Lab: fraction bits used for interpolation. All bits = chip behaviour (the chip's increments
    // are multiples of 4, so 9 effective bits); 0 = drop-sample, no interpolation.
    uint32_t interpMask=fracOne-1;
    std::array<int32_t,6> pole{}; // 1,2,previous2,3,previous3,4
    int32_t lowpass(int32_t x, uint16_t k, int32_t previous) {
        return static_cast<int32_t>((int64_t(k >> 4)*(x-previous))/4096 + previous);
    }
    int32_t highpass(int32_t x, uint16_t k, int32_t previous, int32_t delayedInput) {
        return static_cast<int32_t>(x-delayedInput + (int64_t(k >> 4)*previous)/8192 + previous/2);
    }
    int32_t filter(int32_t x) {
        // MAME apply_filters(), with widened products to avoid undefined overflow.
        x=lowpass(x,k1,pole[0]); pole[0]=x;
        x=lowpass(x,k1,pole[1]); pole[2]=pole[1]; pole[1]=x;
        int32_t p3 = (filterMode < 2) ?
            (filterMode==0 ? highpass(x,k2,pole[3],pole[2]) : lowpass(x,k1,pole[3])) :
            lowpass(x,filterMode==2 ? k2 : k1,pole[3]);
        pole[4]=pole[3]; pole[3]=p3;
        x=filterMode < 2 ? highpass(p3,k2,pole[5],pole[4]) : lowpass(p3,k2,pole[5]);
        pole[5]=x; return x;
    }
    static uint32_t pitchIncrement(double ratio) {
        // ES5505 FC ignores bit 0; MAME shifts the remaining 16-bit word by one.
        int64_t registerValue=std::llround(ratio * 1024.0 / 2.0)*2;
        return static_cast<uint32_t>(clamp<int64_t>(registerValue,2,65534))*2;
    }
    int16_t read(const Sample& s, size_t frames, uint32_t address) const {
        if (address < baseAddress || address-baseAddress >= frames) return 0;
        return s.pcm[address-baseAddress];
    }
    int32_t tick(const Sample& s, size_t frames, bool& ended) {
        if (!running) return 0;
        uint32_t address=accumulator>>fracBits, f=accumulator&(fracOne-1)&interpMask;
        int32_t a=read(s,frames,address), b=read(s,frames,address+1);
        int32_t x=shiftFloor(int64_t(a)*(fracOne-f)+int64_t(b)*f,fracBits);
        x=filter(x);
        // Signed intermediate plus guards. Short loops fold arbitrary overshoot safely.
        int64_t next=int64_t(accumulator)+(reverse ? -int64_t(frequency) : frequency);
        if (next < start || next > end) {
            ended=true;
            if (!loop) { running=false; return x; }
            int64_t span=int64_t(end)-start;
            if (span <= 0) { running=false; return x; }
            if (loop==1) {
                if (next>end) next=start+(next-end)%span;
                else next=end-(start-next)%span;
            } else {
                // Same reflection as the chip, generalized for increments > loop length.
                int64_t m=(next-start)%(2*span); if(m<0) m+=2*span;
                bool crossedOdd;
                if(next>end) crossedOdd=((next-end-1)/span)%2==0;
                else crossedOdd=((start-next-1)/span)%2==0;
                if(crossedOdd) reverse=!reverse;
                next=start+(m<=span ? m : 2*span-m);
            }
        }
        accumulator=static_cast<uint32_t>(next)&0x7fffffff;
        return x;
    }
};

// Host conversion is AFTER chip synthesis, retaining aliases already generated by it.
// 64-tap, 1024-phase Blackman-windowed sinc; fixed arrays, no audio-thread allocation.
struct OutputConverter {
    static constexpr int taps=64, phases=1024;
    std::array<std::array<float,taps>,phases> kernels{};
    std::array<float,taps> history{};
    unsigned head=0;
    void configure(double inputRate, double outputRate) {
        double cutoff=.46*std::min(1.0,outputRate/inputRate);
        for(int p=0;p<phases;p++) {
            double fraction=double(p)/phases, sum=0;
            for(int k=0;k<taps;k++) {
                double d=k-(taps/2.0-1)+fraction;
                double window=.42-.5*std::cos(2*pi*k/(taps-1))+.08*std::cos(4*pi*k/(taps-1));
                double h=std::abs(d)<1e-12 ? 2*cutoff : std::sin(2*pi*cutoff*d)/(pi*d);
                kernels[p][k]=float(h*window); sum+=h*window;
            }
            for(auto& h:kernels[p]) h=float(h/sum);
        }
        reset();
    }
    void reset() { history.fill(0); head=0; }
    void push(float x) { head=(head+1)%taps; history[head]=x; }
    float read(double fraction) const {
        int p=clamp(int(fraction*phases),0,phases-1); float x=0;
        for(int k=0;k<taps;k++) x+=history[(head+taps-k)%taps]*kernels[p][k];
        return x;
    }
};
// Lab: converter for a continuously variable chip clock. Kernels are precomputed off the audio
// thread for nine cutoffs; the bucket at or below the needed anti-alias cutoff is used.
// The authentic fixed clocks keep using OutputConverter, so their output is unchanged.
struct LabConverter {
    static constexpr int taps=64, phases=512, buckets=9;
    std::vector<float> kernels; // buckets*phases*taps
    std::array<float,taps> history{};
    unsigned head=0;
    static double bucketCutoff(int b) { return .46*std::exp2(-b/4.0); }
    void configure() {
        kernels.assign(size_t(buckets)*phases*taps,0.f);
        for(int b=0;b<buckets;b++) for(int p=0;p<phases;p++) {
            double cutoff=bucketCutoff(b), fraction=double(p)/phases, sum=0;
            float* k=&kernels[(size_t(b)*phases+p)*taps];
            for(int t=0;t<taps;t++) {
                double d=t-(taps/2.0-1)+fraction;
                double window=.42-.5*std::cos(2*pi*t/(taps-1))+.08*std::cos(4*pi*t/(taps-1));
                double h=std::abs(d)<1e-12 ? 2*cutoff : std::sin(2*pi*cutoff*d)/(pi*d);
                k[t]=float(h*window); sum+=h*window;
            }
            for(int t=0;t<taps;t++) k[t]=float(k[t]/sum);
        }
        reset();
    }
    static int bucketFor(double chipRate,double hostRate) {
        double r=hostRate/chipRate; if(r>=1) return 0;
        return clamp(int(std::ceil(-4*std::log2(r)-1e-9)),1,buckets-1);
    }
    void reset() { history.fill(0); head=0; }
    void push(float x) { head=(head+1)%taps; history[head]=x; }
    float read(double fraction,int bucket) const {
        if(kernels.empty()) return 0;
        int p=clamp(int(fraction*phases),0,phases-1); float x=0;
        const float* k=&kernels[(size_t(clamp(bucket,0,buckets-1))*phases+p)*taps];
        for(int t=0;t<taps;t++) x+=history[(head+taps-t)%taps]*k[t];
        return x;
    }
};
// Approximate capture anti-alias filtering; not an EPS analog input circuit model.
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void lowpass(double rate,double hz,double q) {
        double w=2*pi*std::min(hz,.45*rate)/rate,c=std::cos(w),alpha=std::sin(w)/(2*q),a0=1+alpha;
        b0=(1-c)/(2*a0); b1=(1-c)/a0; b2=b0; a1=-2*c/a0; a2=(1-alpha)/a0; z1=z2=0;
    }
    float process(float x) { double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return float(y); }
};
struct Controls {
    std::array<float,16> pitch{};
    // Per-voice filter coefficients and level (0..1). Level 1 = chip register 0xc0 = unity.
    std::array<float,16> k1v,k2v,level;
    // Per-voice segment/loop points (0..1); start/end below mirror voice 0 for display.
    std::array<float,16> startv,endv;
    float start=0,end=1,k1=1,k2=1,outputGain=1,recordGain=1;
    int channels=1,loop=0,filterMode=2,captureIndex=5,engineMode=0;
    bool reverse=false,gated=false,linearLevel=false,declick=true;
    // Lab (non-authentic) controls. Defaults reproduce chip behaviour exactly.
    float clockOctaves=0;   // chip clock offset from the selected mode's clock
    bool varispeed=false;   // false: pitch held as the clock moves; true: pitch follows the clock
    int interpBits=9;       // 9 = chip interpolation, 0 = drop-sample
    int recordSource=0;     // 0 input, 1 own output (resample), 2 input + own output
    Controls() { k1v.fill(1); k2v.fill(1); level.fill(1); startv.fill(0); endv.fill(1); }
};
struct Engine {
    Sample sample,recordBuffer;
    size_t frames=0,recordFrames=0;
    std::array<Voice,16> voices{};
    std::array<OutputConverter,3> conversion;
    LabConverter labConversion;
    bool usingLab=false;
    double currentRate=chipRates[0];
    float lastOutput=0;
    std::array<Biquad,3> recordFilters;
    double hostRate=48000,chipPhase=0,recordPhase=0;
    float previousRecord=0;
    int currentMode=0;
    bool recording=false;
    bool recordFull=false;
    bool capturePending=false, restartCapture=false;
    double endPulse=0;
    Controls controls;
    // Only finished-sample metadata/buffers use this lock. Snapshot readers may wait;
    // the audio writer uses try_lock and defers publication, never playback.
    // Other Engine state still belongs to its single processing/control thread.
    mutable std::mutex sampleMutex;
    // Revision for the serialized PCM cache; not a substitute for synchronization.
    std::atomic<uint32_t> sampleSequence{0};
    void beginSampleChange() { sampleSequence.fetch_add(1,std::memory_order_acq_rel); }
    void endSampleChange() { sampleSequence.fetch_add(1,std::memory_order_release); }
    uint32_t sequence() const { return sampleSequence.load(std::memory_order_acquire); }
    // Copy on a non-audio thread. The buffer cannot become record memory (or be
    // replaced/freed by install) until this copy releases sampleMutex.
    bool snapshot(std::vector<int16_t>& out,double& rate,uint32_t& seq) const {
        std::lock_guard<std::mutex> guard(sampleMutex);
        out.assign(sample.pcm.begin(),sample.pcm.begin()+std::min(frames,memoryWords));
        rate=sample.rate; seq=sampleSequence.load(std::memory_order_relaxed);
        return true;
    }
    Engine() { setHostRate(48000); }
    void setHostRate(double rate) {
        hostRate=clamp(rate,8000.0,384000.0);
        for(int i=0;i<3;i++) conversion[i].configure(chipRates[i],hostRate);
        if(labConversion.kernels.empty()) labConversion.configure(); else labConversion.reset();
        chipPhase=0;
        stopRecord();
    }
    void stopAll() { for(auto& v:voices) v.running=false; }
    void install(std::vector<int16_t>& pcm,double rate) {
        stopAll(); recording=false; recordFull=false; capturePending=false; restartCapture=false;
        size_t newFrames=std::min(pcm.size(),memoryWords);
        // Caller prepares the capacity off the audio thread.
        pcm.resize(memoryWords,0);
        std::lock_guard<std::mutex> guard(sampleMutex);
        beginSampleChange();
        frames=newFrames;
        sample.pcm.swap(pcm);
        sample.rate=clamp(rate,1000.0,384000.0);
        endSampleChange();
        for(auto& c:conversion) c.reset();
        labConversion.reset();
        chipPhase=0;
    }
    void beginRecording() {
        recording=true; recordFull=false; recordFrames=0; recordPhase=0; previousRecord=0;
        recordBuffer.rate=captureRate(controls.captureIndex);
        const double q[3]={.517638090205,.707106781187,1.931851652578};
        for(int i=0;i<3;i++) recordFilters[i].lowpass(hostRate,.45*recordBuffer.rate,q[i]);
    }
    void startRecord() {
        if(capturePending) {
            // Do not overwrite a completed take while a snapshot pins the old
            // playback buffer. Start the requested capture after publication.
            restartCapture=true;
            publishRecording();
            return;
        }
        restartCapture=false;
        beginRecording();
    }
    void stopRecord() {
        restartCapture=false; // A falling gate also cancels a queued start.
        if(recording) {
            recording=false;
            capturePending=recordFrames>=2;
        }
        publishRecording();
    }
    bool publishRecording() {
        if(!capturePending) return false;
        std::unique_lock<std::mutex> guard(sampleMutex,std::try_to_lock);
        if(!guard.owns_lock()) return false;
        stopAll();
        beginSampleChange();
        sample.pcm.swap(recordBuffer.pcm); sample.rate=recordBuffer.rate; frames=recordFrames;
        endSampleChange();
        capturePending=false;
        for(auto& c:conversion) c.reset();
        labConversion.reset();
        chipPhase=0;
        bool restart=restartCapture; restartCapture=false;
        guard.unlock();
        if(restart) beginRecording();
        return true;
    }
    // Declick fade length in chip samples (about 1.5 ms at the current chip clock).
    int fadeSamples() const { return std::max(1,int(std::lround(currentRate*0.0015))); }
    void start(Voice& v) {
        v=Voice(); v.running=true; v.reverse=controls.reverse;
        bounds(v,int(&v-voices.data())); v.accumulator=v.reverse ? v.end : v.start;
    }
    void trigger(int channel) {
        int mode=clamp(controls.engineMode,0,2);
        if(channel<0 || channel>=voiceLimits[mode] || frames<2) return;
        auto& v=voices[channel];
        if(controls.declick && v.running) {
            // Fade the sounding note out, then restart; costs ~1.5 ms of onset latency.
            if(v.fadeRemaining==0) { v.fadeLength=fadeSamples(); v.fadeRemaining=v.fadeLength; }
            v.restartAfterFade=true;
            return;
        }
        start(v);
    }
    void release(int channel) {
        if(!controls.gated || channel<0 || channel>=16) return;
        auto& v=voices[channel];
        if(!v.running) return;
        v.restartAfterFade=false;
        if(controls.declick) { if(v.fadeRemaining==0) { v.fadeLength=fadeSamples(); v.fadeRemaining=v.fadeLength; } }
        else v.running=false;
    }
    void bounds(Voice& v,int i) {
        i=clamp(i,0,15);
        size_t a=std::min(size_t(clamp(controls.startv[i],0.f,1.f)*(frames-1)),frames-2);
        size_t b=std::max(a+1,std::min(size_t(clamp(controls.endv[i],0.f,1.f)*(frames-1)),frames-1));
        v.start=uint32_t(baseAddress+a)<<fracBits; v.end=uint32_t(baseAddress+b)<<fracBits;
        if(v.accumulator<v.start || v.accumulator>v.end) v.accumulator=v.reverse ? v.end : v.start;
    }
    float process(float input) {
        if(capturePending) publishRecording();
        if(recording) {
            // Lab: the record source can be this module's own previous output (resampling).
            float source=controls.recordSource==1 ? lastOutput : controls.recordSource==2 ? input+lastOutput : input;
            float x=std::isfinite(source) ? source*controls.recordGain : 0;
            for(auto& f:recordFilters) x=f.process(x);
            double before=recordPhase; recordPhase+=recordBuffer.rate/hostRate;
            while(recordPhase>=1 && recording) {
                double fraction=(1-before)/(recordBuffer.rate/hostRate);
                recordBuffer.pcm[recordFrames++]=quantize(previousRecord+(x-previousRecord)*float(clamp(fraction,0.0,1.0)));
                recordPhase-=1; before-=1;
                if(recordFrames==memoryWords) { stopRecord(); recordFull=true; }
            }
            previousRecord=x;
        }
        int mode=clamp(controls.engineMode,0,2);
        if(mode!=currentMode) { currentMode=mode; chipPhase=0; conversion[mode].reset(); labConversion.reset(); for(int i=voiceLimits[mode];i<16;i++) voices[i].running=false; }
        // Lab clock: the voice count (and so polyphony) stays with the mode; only the clock moves.
        double octaves=std::isfinite(controls.clockOctaves) ? clamp(double(controls.clockOctaves),-6.0,3.0) : 0.0;
        bool lab=octaves!=0;
        double rate=lab ? clamp(chipRates[mode]*std::exp2(octaves),1000.0,std::min(4*hostRate,400000.0)) : chipRates[mode];
        if(lab!=usingLab) {
            // Hand the converter history across so switching paths does not click.
            if(lab) { labConversion.history=conversion[mode].history; labConversion.head=conversion[mode].head; }
            else { conversion[mode].history=labConversion.history; conversion[mode].head=labConversion.head; }
            usingLab=lab;
        }
        currentRate=rate;
        double pitchRate=controls.varispeed ? chipRates[mode] : rate;
        int bits=clamp(controls.interpBits,0,9);
        uint32_t mask=bits>=9 ? fracOne-1 : (fracOne-1)&~((1u<<(fracBits-bits))-1);
        // A signal cable can be removed while its voices remain latched. Read channel pitch safely.
        for(int i=0;i<voiceLimits[mode];i++) if(voices[i].running) {
            auto& v=voices[i]; bounds(v,i);
            v.frequency=Voice::pitchIncrement(sample.rate/pitchRate*std::exp2(clamp(double(controls.pitch[i]),-10.0,10.0)));
            v.interpMask=mask;
            v.k1=uint16_t(uint16_t(clamp(controls.k1v[i],0.f,1.f)*4095)<<4);
            v.k2=uint16_t(uint16_t(clamp(controls.k2v[i],0.f,1.f)*4095)<<4);
            v.volume=levelRegister(controls.level[i],controls.linearLevel);
            v.filterMode=clamp(controls.filterMode,0,3); v.loop=clamp(controls.loop,0,2);
        }
        chipPhase+=rate/hostRate;
        while(chipPhase>=1) {
            int64_t sum=0; bool ended=false;
            for(int i=0;i<voiceLimits[mode];i++) {
                auto& v=voices[i];
                if(!v.running) continue;
                bool voiceEnded=false;
                int32_t x=applyVolume(v.tick(sample,frames,voiceEnded),v.volume);
                if(voiceEnded) ended=true;
                if(v.fadeRemaining>0) {
                    x=static_cast<int32_t>(int64_t(x)*v.fadeRemaining/v.fadeLength);
                    // Done fading, or a one-shot reached its end mid-fade: stop, or restart a pending retrigger.
                    if(--v.fadeRemaining==0 || !v.running) { bool restart=v.restartAfterFade; v.fadeRemaining=0; if(restart) start(v); else v.running=false; }
                }
                sum+=x;
            }
            if(ended) endPulse=.001;
            // Per-voice chip volume (ES5505 log table) is applied above; no EPS envelope generator.
            if(lab) labConversion.push(float(sum)/32768.f); else conversion[mode].push(float(sum)/32768.f);
            chipPhase-=1;
        }
        endPulse=std::max(0.0,endPulse-1/hostRate);
        float out=(lab ? labConversion.read(chipPhase,LabConverter::bucketFor(rate,hostRate)) : conversion[mode].read(chipPhase))*controls.outputGain;
        lastOutput=std::isfinite(out) ? out : 0.f;
        return out;
    }
    float position() const {
        for(const auto& v:voices) if(v.running && frames>1)
            return clamp((double(v.accumulator)/fracOne-baseAddress)/(frames-1),0.0,1.0);
        return 0;
    }
    bool playing() const { for(const auto& v:voices) if(v.running) return true; return false; }
};
} // namespace otis
