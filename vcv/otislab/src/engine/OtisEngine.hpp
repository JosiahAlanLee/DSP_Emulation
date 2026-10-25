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
// A stereo sample keeps its left channel at the start of sample memory and its right channel at this
// offset, as two chip voices would play two regions: each channel gets half the memory.
constexpr size_t stereoOffset = memoryWords/2;
constexpr size_t stereoFrames = memoryWords/2;
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
    bool stereo = false; // right channel at stereoOffset
    Sample() : pcm(memoryWords, 0) {}
};
// Sample memory holding a stereo sample: left at 0, right at stereoOffset (at most stereoFrames each).
inline std::vector<int16_t> stereoImage(const int16_t* left,const int16_t* right,size_t frames) {
    frames=std::min(frames,stereoFrames);
    std::vector<int16_t> image(memoryWords,0);
    std::copy(left,left+frames,image.begin());
    std::copy(right,right+frames,image.begin()+stereoOffset);
    return image;
}
struct Voice {
    bool running=false, reverse=false;
    uint32_t accumulator=0, start=0, end=0, frequency=0;
    uint16_t k1=0xfff0, k2=0xfff0;
    uint8_t volume=unityVolume;
    // Host-side declick (not chip behaviour): linear fade over fadeLength chip samples.
    int fadeRemaining=0, fadeLength=0;
    bool restartAfterFade=false;
    int filterMode=2, loop=0;
    bool window=false; // LAB windows use half-open [start,end) sample frames.
    // Lab: fraction bits used for interpolation. All bits = chip behaviour (the chip's increments
    // are multiples of 4, so 9 effective bits); 0 = drop-sample, no interpolation.
    uint32_t interpMask=fracOne-1;
    std::array<int32_t,6> pole{}; // 1,2,previous2,3,previous3,4
    std::array<int32_t,6> poleR{}; // a stereo pair's right voice: same registers, its own filter state
    int32_t lowpass(int32_t x, uint16_t k, int32_t previous) {
        return static_cast<int32_t>((int64_t(k >> 4)*(x-previous))/4096 + previous);
    }
    int32_t highpass(int32_t x, uint16_t k, int32_t previous, int32_t delayedInput) {
        return static_cast<int32_t>(x-delayedInput + (int64_t(k >> 4)*previous)/8192 + previous/2);
    }
    int32_t filter(int32_t x) { return filter(x,pole); }
    int32_t filter(int32_t x, std::array<int32_t,6>& p) {
        // MAME apply_filters(), with widened products to avoid undefined overflow.
        x=lowpass(x,k1,p[0]); p[0]=x;
        x=lowpass(x,k1,p[1]); p[2]=p[1]; p[1]=x;
        int32_t p3 = (filterMode < 2) ?
            (filterMode==0 ? highpass(x,k2,p[3],p[2]) : lowpass(x,k1,p[3])) :
            lowpass(x,filterMode==2 ? k2 : k1,p[3]);
        p[4]=p[3]; p[3]=p3;
        x=filterMode < 2 ? highpass(p3,k2,p[5],p[4]) : lowpass(p3,k2,p[5]);
        p[5]=x; return x;
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
    // right: for a stereo sample, the right voice of the pair. It reads the right channel at the same
    // address and fraction with the same registers, so the two sides cannot drift apart.
    int32_t tick(const Sample& s, size_t frames, bool& ended,
                 const std::vector<int16_t>* oldPcm=nullptr, float blend=1.f, int32_t* right=nullptr) {
        if (!running) { if(right) *right=0; return 0; }
        uint32_t address=accumulator>>fracBits, f=accumulator&(fracOne-1)&interpMask;
        uint32_t nextAddress=address+1;
        if(window && nextAddress >= (end>>fracBits))
            nextAddress=loop==1 ? start>>fracBits : (end>>fracBits)-1;
        auto interpolate=[&](const std::vector<int16_t>* old, size_t channel) {
            auto at=[&](uint32_t a) -> int32_t {
                if(a<baseAddress || a-baseAddress>=frames) return 0;
                return old ? (*old)[channel+(a-baseAddress)] : s.pcm[channel+(a-baseAddress)];
            };
            return shiftFloor(int64_t(at(address))*(fracOne-f)+int64_t(at(nextAddress))*f,fracBits);
        };
        int32_t x=interpolate(nullptr,0);
        if(oldPcm) { int32_t old=interpolate(oldPcm,0); x=int32_t(std::lround(old+(x-old)*double(blend))); }
        x=filter(x);
        if(right) {
            int32_t r=interpolate(nullptr,stereoOffset);
            if(oldPcm) { int32_t old=interpolate(oldPcm,stereoOffset); r=int32_t(std::lround(old+(r-old)*double(blend))); }
            *right=filter(r,poleR);
        }
        if(window) {
            int64_t next=int64_t(accumulator)+(reverse ? -int64_t(frequency) : frequency);
            int64_t span=int64_t(end)-start;
            if(loop==2) {
                int64_t last=int64_t(end)-fracOne, distance=last-start;
                if(next<start || next>last) {
                    ended=true;
                    if(distance<=0) { accumulator=start; return x; }
                    bool odd=next>last ? ((next-last-1)/distance)%2==0 : ((start-next-1)/distance)%2==0;
                    if(odd) reverse=!reverse;
                    int64_t phase=(next-start)%(2*distance); if(phase<0) phase+=2*distance;
                    next=start+(phase<=distance ? phase : 2*distance-phase);
                }
            } else if(next<start || next>=end) {
                ended=true;
                if(!loop) { running=false; return x; }
                int64_t phase=(next-start)%span; if(phase<0) phase+=span;
                next=start+phase;
            }
            accumulator=uint32_t(next); return x;
        }
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
    float read(double fraction) const { return read(history,head,fraction); }
    // Another channel's history through these kernels (the right channel of a stereo sample).
    float read(const std::array<float,taps>& h, unsigned at, double fraction) const {
        int p=clamp(int(fraction*phases),0,phases-1); float x=0;
        for(int k=0;k<taps;k++) x+=h[(at+taps-k)%taps]*kernels[p][k];
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
    float read(double fraction,int bucket) const { return read(history,head,fraction,bucket); }
    float read(const std::array<float,taps>& h, unsigned at, double fraction, int bucket) const {
        if(kernels.empty()) return 0;
        int p=clamp(int(fraction*phases),0,phases-1); float x=0;
        const float* k=&kernels[(size_t(clamp(bucket,0,buckets-1))*phases+p)*taps];
        for(int t=0;t<taps;t++) x+=h[(at+taps-t)%taps]*k[t];
        return x;
    }
};
// The right channel's converter state; its kernels are the left converter's.
struct ConverterHistory {
    static constexpr int taps=64;
    static_assert(taps==OutputConverter::taps && taps==LabConverter::taps, "converters share one history length");
    std::array<float,taps> history{};
    unsigned head=0;
    void reset() { history.fill(0); head=0; }
    void push(float x) { head=(head+1)%taps; history[head]=x; }
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
    bool recordStereo=false; // read when a take starts: record both inputs into a stereo sample
    int windowMode=0;       // 0 legacy segment, 1 fixed-size scan, 2 whole-frame steps
    std::array<float,16> position{}, sizeOctaves{}; // position 0..1, log2(sample count)
    Controls() { k1v.fill(1); k2v.fill(1); level.fill(1); startv.fill(0); endv.fill(1); sizeOctaves.fill(8); }
};
struct Engine {
    Sample sample,recordBuffer;
    size_t frames=0,recordFrames=0;
    std::array<Voice,16> voices{};
    std::array<OutputConverter,3> conversion;
    LabConverter labConversion;
    std::array<ConverterHistory,3> conversionR; // right channel of a stereo sample
    ConverterHistory labConversionR;
    bool usingLab=false;
    double currentRate=chipRates[0];
    float lastOutput=0,lastOutputR=0;
    // The last process() call's right output: the right channel of a stereo sample, else the left.
    float rightOutput=0;
    std::array<Biquad,3> recordFilters,recordFiltersR;
    double hostRate=48000,chipPhase=0,recordPhase=0;
    float previousRecord=0,previousRecordR=0;
    int currentMode=0;
    bool recording=false;
    bool recordFull=false;
    bool capturePending=false, restartCapture=false;
    double endPulse=0;
    Controls controls;
    const std::vector<int16_t>* fadingPcm=nullptr;
    int pcmFadeRemaining=0, pcmFadeTotal=0;
    void clearPcmFade() { fadingPcm=nullptr; pcmFadeRemaining=pcmFadeTotal=0; }
    enum class Publication { Deferred, Published, Obsolete };
    // Called by the processing thread. Prepared/retired storage stays worker-owned
    // and must not be reused until fadingPcm becomes null.
    Publication publishPrepared(std::vector<int16_t>& prepared,uint32_t revision) {
        if(recording || capturePending || sequence()!=revision) return Publication::Obsolete;
        if(fadingPcm) return Publication::Deferred;
        std::unique_lock<std::mutex> guard(sampleMutex,std::try_to_lock);
        if(!guard.owns_lock()) return Publication::Deferred;
        if(sequence()!=revision || prepared.size()!=memoryWords || frames<2) return Publication::Obsolete;
        beginSampleChange(); sample.pcm.swap(prepared); endSampleChange();
        fadingPcm=&prepared; pcmFadeRemaining=pcmFadeTotal=std::max(1,int(std::lround(hostRate*.005)));
        return Publication::Published;
    }
    struct Window { size_t begin=0,end=0; bool active=false; }; // end exclusive
    Window windowFor(int channel) const {
        if(frames<2) return {};
        int i=clamp(channel,0,15);
        size_t a=std::min(size_t(clamp(controls.startv[i],0.f,1.f)*(frames-1)),frames-2);
        size_t b=std::max(a+1,std::min(size_t(clamp(controls.endv[i],0.f,1.f)*(frames-1)),frames-1));
        int mode=clamp(controls.windowMode,0,2);
        if(!mode) return {a,b+1,false};
        double oct=std::isfinite(controls.sizeOctaves[i]) ? clamp(double(controls.sizeOctaves[i]),1.0,16.0) : 8;
        size_t n=std::min(size_t(std::llround(std::exp2(oct))),b-a+1);
        double pos=std::isfinite(controls.position[i]) ? clamp(double(controls.position[i]),0.0,1.0) : 0;
        size_t offset=mode==1 ? size_t(std::llround(pos*(b-a+1-n))) :
            size_t(std::llround(pos*((b-a+1)/n-1)))*n;
        return {a+offset,a+offset+n,true};
    }
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
    // A stereo sample's left channel; see snapshotStereo.
    bool snapshot(std::vector<int16_t>& out,double& rate,uint32_t& seq) const {
        std::lock_guard<std::mutex> guard(sampleMutex);
        out.assign(sample.pcm.begin(),sample.pcm.begin()+std::min(frames,memoryWords));
        rate=sample.rate; seq=sampleSequence.load(std::memory_order_relaxed);
        return true;
    }
    // Both channels; right is left empty for a mono sample. Returns whether the sample is stereo.
    bool snapshotStereo(std::vector<int16_t>& left,std::vector<int16_t>& right,double& rate,uint32_t& seq) const {
        std::lock_guard<std::mutex> guard(sampleMutex);
        size_t n=std::min(frames,sample.stereo ? stereoFrames : memoryWords);
        left.assign(sample.pcm.begin(),sample.pcm.begin()+n);
        if(sample.stereo) right.assign(sample.pcm.begin()+stereoOffset,sample.pcm.begin()+stereoOffset+n); else right.clear();
        rate=sample.rate; seq=sampleSequence.load(std::memory_order_relaxed);
        return sample.stereo;
    }
    // The sample memory in use, for a rework: [0,frames) of a mono sample, [0,stereoOffset+frames) of a
    // stereo one. channelFrames is the length of one channel.
    void snapshotImage(std::vector<int16_t>& out,double& rate,uint32_t& seq,size_t& channelFrames,bool& stereo) const {
        std::lock_guard<std::mutex> guard(sampleMutex);
        stereo=sample.stereo; channelFrames=std::min(frames,stereo ? stereoFrames : memoryWords);
        out.assign(sample.pcm.begin(),sample.pcm.begin()+(stereo ? stereoOffset : 0)+channelFrames);
        rate=sample.rate; seq=sampleSequence.load(std::memory_order_relaxed);
    }
    Engine() { setHostRate(48000); }
    void setHostRate(double rate) {
        hostRate=clamp(rate,8000.0,384000.0);
        for(int i=0;i<3;i++) conversion[i].configure(chipRates[i],hostRate);
        if(labConversion.kernels.empty()) labConversion.configure(); else labConversion.reset();
        chipPhase=0; clearPcmFade();
        stopRecord();
    }
    void stopAll() { for(auto& v:voices) v.running=false; }
    // Voices available to the selected clock: a stereo sample uses two chip voices per Rack voice.
    int voiceLimit(int mode) const { return sample.stereo ? voiceLimits[mode]/2 : voiceLimits[mode]; }
    void resetConversion() {
        for(auto& c:conversion) c.reset();
        for(auto& c:conversionR) c.reset();
        labConversion.reset(); labConversionR.reset();
    }
    void install(std::vector<int16_t>& pcm,double rate) { install(pcm,rate,std::min(pcm.size(),memoryWords),false); }
    // stereo: pcm is a stereoImage() and channelFrames the length of one channel.
    void install(std::vector<int16_t>& pcm,double rate,size_t channelFrames,bool stereo) {
        stopAll(); clearPcmFade(); recording=false; recordFull=false; capturePending=false; restartCapture=false;
        size_t newFrames=std::min(channelFrames,stereo ? stereoFrames : memoryWords);
        // Caller prepares the capacity off the audio thread.
        pcm.resize(memoryWords,0);
        std::lock_guard<std::mutex> guard(sampleMutex);
        beginSampleChange();
        frames=newFrames;
        sample.pcm.swap(pcm);
        sample.rate=clamp(rate,1000.0,384000.0);
        sample.stereo=stereo;
        endSampleChange();
        resetConversion();
        chipPhase=0;
    }
    void beginRecording() {
        recording=true; recordFull=false; recordFrames=0; recordPhase=0; previousRecord=0; previousRecordR=0;
        recordBuffer.rate=captureRate(controls.captureIndex);
        recordBuffer.stereo=controls.recordStereo;
        const double q[3]={.517638090205,.707106781187,1.931851652578};
        for(int i=0;i<3;i++) { recordFilters[i].lowpass(hostRate,.45*recordBuffer.rate,q[i]); recordFiltersR[i].lowpass(hostRate,.45*recordBuffer.rate,q[i]); }
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
        stopAll(); clearPcmFade();
        beginSampleChange();
        sample.pcm.swap(recordBuffer.pcm); sample.rate=recordBuffer.rate; frames=recordFrames; sample.stereo=recordBuffer.stereo;
        endSampleChange();
        capturePending=false;
        resetConversion();
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
        bounds(v,int(&v-voices.data())); v.accumulator=v.reverse ? (v.window ? v.end-fracOne : v.end) : v.start;
    }
    void trigger(int channel) {
        int mode=clamp(controls.engineMode,0,2);
        if(channel<0 || channel>=voiceLimit(mode) || frames<2) return;
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
        Window w=windowFor(i);
        uint32_t start=uint32_t(baseAddress+w.begin)<<fracBits;
        uint32_t end=uint32_t(baseAddress+w.end-(w.active ? 0 : 1))<<fracBits;
        if(w.active && v.window && v.end>v.start && (start!=v.start || end!=v.end)) {
            int64_t span=int64_t(end)-start;
            int64_t phase=(int64_t(v.accumulator)-v.start)%span; if(phase<0) phase+=span;
            v.accumulator=uint32_t(start+phase);
        }
        v.start=start; v.end=end; v.window=w.active;
        if(v.accumulator<start || (w.active ? v.accumulator>=end : v.accumulator>end))
            v.accumulator=v.reverse ? (w.active ? end-fracOne : end) : start;
    }
    float process(float input) { return process(input,input); }
    // Returns the left (or mono) output; the right one is in rightOutput. inputR is recorded only
    // into a stereo take (controls.recordStereo when the take started).
    float process(float input, float inputR) {
        if(capturePending) publishRecording();
        if(recording) {
            // Lab: the record source can be this module's own previous output (resampling).
            float source=controls.recordSource==1 ? lastOutput : controls.recordSource==2 ? input+lastOutput : input;
            float x=std::isfinite(source) ? source*controls.recordGain : 0;
            for(auto& f:recordFilters) x=f.process(x);
            bool stereo=recordBuffer.stereo; float xR=0;
            if(stereo) {
                float sourceR=controls.recordSource==1 ? lastOutputR : controls.recordSource==2 ? inputR+lastOutputR : inputR;
                xR=std::isfinite(sourceR) ? sourceR*controls.recordGain : 0;
                for(auto& f:recordFiltersR) xR=f.process(xR);
            }
            size_t capacity=stereo ? stereoFrames : memoryWords;
            double before=recordPhase; recordPhase+=recordBuffer.rate/hostRate;
            while(recordPhase>=1 && recording) {
                double fraction=(1-before)/(recordBuffer.rate/hostRate);
                if(stereo) recordBuffer.pcm[stereoOffset+recordFrames]=quantize(previousRecordR+(xR-previousRecordR)*float(clamp(fraction,0.0,1.0)));
                recordBuffer.pcm[recordFrames++]=quantize(previousRecord+(x-previousRecord)*float(clamp(fraction,0.0,1.0)));
                recordPhase-=1; before-=1;
                if(recordFrames==capacity) { stopRecord(); recordFull=true; }
            }
            previousRecord=x; previousRecordR=xR;
        }
        int mode=clamp(controls.engineMode,0,2);
        int limit=voiceLimit(mode);
        if(mode!=currentMode) {
            currentMode=mode; chipPhase=0; conversion[mode].reset(); labConversion.reset();
            conversionR[mode].reset(); labConversionR.reset();
            for(int i=limit;i<16;i++) voices[i].running=false;
        }
        // Lab clock: the voice count (and so polyphony) stays with the mode; only the clock moves.
        double octaves=std::isfinite(controls.clockOctaves) ? clamp(double(controls.clockOctaves),-6.0,3.0) : 0.0;
        bool lab=octaves!=0;
        double rate=lab ? clamp(chipRates[mode]*std::exp2(octaves),1000.0,std::min(4*hostRate,400000.0)) : chipRates[mode];
        if(lab!=usingLab) {
            // Hand the converter history across so switching paths does not click.
            if(lab) { labConversion.history=conversion[mode].history; labConversion.head=conversion[mode].head; labConversionR=conversionR[mode]; }
            else { conversion[mode].history=labConversion.history; conversion[mode].head=labConversion.head; conversionR[mode]=labConversionR; }
            usingLab=lab;
        }
        currentRate=rate;
        double pitchRate=controls.varispeed ? chipRates[mode] : rate;
        int bits=clamp(controls.interpBits,0,9);
        uint32_t mask=bits>=9 ? fracOne-1 : (fracOne-1)&~((1u<<(fracBits-bits))-1);
        // A signal cable can be removed while its voices remain latched. Read channel pitch safely.
        for(int i=0;i<limit;i++) if(voices[i].running) {
            auto& v=voices[i]; bounds(v,i);
            v.frequency=Voice::pitchIncrement(sample.rate/pitchRate*std::exp2(clamp(double(controls.pitch[i]),-10.0,10.0)));
            v.interpMask=mask;
            v.k1=uint16_t(uint16_t(clamp(controls.k1v[i],0.f,1.f)*4095)<<4);
            v.k2=uint16_t(uint16_t(clamp(controls.k2v[i],0.f,1.f)*4095)<<4);
            v.volume=levelRegister(controls.level[i],controls.linearLevel);
            v.filterMode=clamp(controls.filterMode,0,3); v.loop=clamp(controls.loop,0,2);
        }
        bool stereo=sample.stereo;
        chipPhase+=rate/hostRate;
        while(chipPhase>=1) {
            int64_t sum=0,sumR=0; bool ended=false;
            for(int i=0;i<limit;i++) {
                auto& v=voices[i];
                if(!v.running) continue;
                bool voiceEnded=false; int32_t right=0;
                int32_t x=applyVolume(v.tick(sample,frames,voiceEnded,fadingPcm,
                    pcmFadeTotal ? float(pcmFadeTotal-pcmFadeRemaining)/pcmFadeTotal : 1.f,stereo ? &right : nullptr),v.volume);
                int32_t xR=stereo ? applyVolume(right,v.volume) : 0;
                if(voiceEnded) ended=true;
                if(v.fadeRemaining>0) {
                    x=static_cast<int32_t>(int64_t(x)*v.fadeRemaining/v.fadeLength);
                    xR=static_cast<int32_t>(int64_t(xR)*v.fadeRemaining/v.fadeLength);
                    // Done fading, or a one-shot reached its end mid-fade: stop, or restart a pending retrigger.
                    if(--v.fadeRemaining==0 || !v.running) { bool restart=v.restartAfterFade; v.fadeRemaining=0; if(restart) start(v); else v.running=false; }
                }
                sum+=x; sumR+=xR;
            }
            if(ended) endPulse=.001;
            // Per-voice chip volume (ES5505 log table) is applied above; no EPS envelope generator.
            if(lab) labConversion.push(float(sum)/32768.f); else conversion[mode].push(float(sum)/32768.f);
            if(stereo) { if(lab) labConversionR.push(float(sumR)/32768.f); else conversionR[mode].push(float(sumR)/32768.f); }
            chipPhase-=1;
        }
        endPulse=std::max(0.0,endPulse-1/hostRate);
        int bucket=lab ? LabConverter::bucketFor(rate,hostRate) : 0;
        float out=(lab ? labConversion.read(chipPhase,bucket) : conversion[mode].read(chipPhase))*controls.outputGain;
        float outR=out;
        if(stereo) outR=(lab ? labConversion.read(labConversionR.history,labConversionR.head,chipPhase,bucket)
                             : conversion[mode].read(conversionR[mode].history,conversionR[mode].head,chipPhase))*controls.outputGain;
        lastOutput=std::isfinite(out) ? out : 0.f;
        lastOutputR=std::isfinite(outR) ? outR : 0.f;
        rightOutput=outR;
        if(pcmFadeRemaining>0 && !--pcmFadeRemaining) clearPcmFade();
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
