// Original LAB loop-reworking algorithm. Inspired by the ASR-10 workflow;
// does not implement Ensoniq's undocumented Synthesized Loop algorithm.
#pragma once
#include "OtisEngine.hpp"
#include <chrono>
#include <thread>

namespace otis {
inline uint32_t mutationRandom(uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5; return state;
}
// Off-thread only: circular, randomized grain overlap/add, seam smoothing,
// then a dry/processed blend. No data outside [begin,end) is changed.
// rightOffset: a stereo sample's right channel starts there; both channels take the same grains and
// one gain, so the stereo image is kept.
inline bool reworkLoop(std::vector<int16_t>& pcm,size_t begin,size_t end,
                       float smoothness,float amount,uint32_t seed,
                       const std::atomic<bool>* stop=nullptr,size_t rightOffset=0) {
    end=std::min(end,pcm.size());
    if(begin>=end || end-begin<2 || !(amount>0)) return false;
    if(rightOffset && rightOffset+end>pcm.size()) return false;
    smoothness=std::isfinite(smoothness) ? clamp(smoothness,0.f,1.f) : .5f;
    amount=std::isfinite(amount) ? clamp(amount,0.f,1.f) : 0;
    size_t n=end-begin;
    const int channels=rightOffset ? 2 : 1;
    const size_t base[2]={begin,rightOffset+begin};
    std::vector<float> source[2],sum[2],result[2],weight(n,0);
    for(int c=0;c<channels;++c) {
        source[c].resize(n); sum[c].assign(n,0); result[c].resize(n);
        for(size_t i=0;i<n;++i) source[c][i]=pcm[base[c]+i];
    }
    size_t grain=std::min(n,size_t(std::lround(std::exp2(11-7*smoothness))));
    size_t hop=std::max(size_t(1),grain/4);
    uint32_t rng=seed ? seed : 1;
    for(size_t dest=0;dest<n;dest+=hop) {
        if(stop && stop->load(std::memory_order_relaxed)) return false;
        size_t origin=mutationRandom(rng)%n;
        for(size_t j=0;j<grain;++j) {
            float w=float(.5-.5*std::cos(2*pi*(j+.5)/grain));
            size_t k=(dest+j)%n;
            for(int c=0;c<channels;++c) sum[c][k]+=source[c][(origin+j)%n]*w;
            weight[k]+=w;
        }
    }
    size_t seam=std::min(n/2,size_t(8+120*smoothness));
    for(int c=0;c<channels;++c) {
        auto& r=result[c];
        for(size_t i=0;i<n;++i) r[i]=weight[i]>0 ? sum[c][i]/weight[i] : source[c][i];
        for(size_t i=0;i<seam;++i) {
            float w=float(.5+.5*std::cos(pi*i/seam)), mean=(r[i]+r[n-1-i])*.5f;
            r[i]+=(mean-r[i])*w; r[n-1-i]+=(mean-r[n-1-i])*w;
        }
    }
    // Only [begin,end) is rewritten, so taper the change to zero at both edges.
    // Without this the region boundaries jump by up to full scale and click
    // whenever a moving window or position CV plays across them.
    size_t edge=std::min(n/4,size_t(64));
    std::vector<float> taper(n,1.f);
    for(size_t i=0;i<n;++i) {
        size_t d=std::min(i,n-1-i);
        if(d<edge) taper[i]=float(.5-.5*std::cos(pi*(d+.5)/edge));
    }
    // Overlap-adding uncorrelated grains and normalizing by the summed window
    // weight averages them, which costs several dB per generation (and the
    // dry/wet blend of decorrelated material loses more). Choose the gain g on
    // the processed blend m so that out = s + t*(g*m - s) has the source's
    // power: |(1-t)s|^2 + 2g<(1-t)s, t*m> + g^2|t*m|^2 = |s|^2.
    double P=0,A=0,B=0,C=0;
    for(int c=0;c<channels;++c) for(size_t i=0;i<n;++i) {
        result[c][i]=source[c][i]+amount*(result[c][i]-source[c][i]);
        double s=source[c][i], dry=(1-taper[i])*s, wet=taper[i]*result[c][i];
        P+=s*s; A+=dry*dry; B+=dry*wet; C+=wet*wet;
    }
    float gain=1;
    if(C>1e-9) {
        double disc=B*B-C*(A-P);
        gain=clamp(float((-B+std::sqrt(std::max(0.0,disc)))/C),.25f,4.f);
    }
    bool changed=false;
    for(int c=0;c<channels;++c) for(size_t i=0;i<n;++i) {
        float processed=result[c][i]*gain;
        int16_t v=int16_t(clamp<long>(std::lround(source[c][i]+taper[i]*(processed-source[c][i])),-32768,32767));
        changed|=v!=pcm[base[c]+i]; pcm[base[c]+i]=v;
    }
    return changed;
}

class LoopMutator {
public:
    enum State { Idle, Preparing, Requested, Working, Ready, Fading, Finished };
    enum Outcome { None, Applied, SourceChanged, NoChange, Failed, Unavailable, Canceled };
private:
    Engine& engine;
    struct Request { size_t begin,end; float smoothness,amount; uint32_t revision,seed,epoch; } requestData{};
    std::atomic<int> state{Idle}, outcome{None};
    std::atomic<uint32_t> cancelEpoch{0};
    std::atomic<bool> stopping{false};
    std::vector<int16_t> prepared;
    uint32_t nextSeed=0x41535210;
    std::thread worker;
    void run() {
        while(!stopping.load(std::memory_order_acquire)) {
            if(state.load(std::memory_order_acquire)!=Requested) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue;
            }
            Request job=requestData;
            state.store(Working,std::memory_order_release);
            try {
                double rate=0; uint32_t revision=0; size_t frames=0; bool stereo=false;
                engine.snapshotImage(prepared,rate,revision,frames,stereo);
                if(revision!=job.revision || job.end>frames) {
                    outcome.store(SourceChanged,std::memory_order_relaxed); state.store(Finished,std::memory_order_release); continue;
                }
                bool changed=reworkLoop(prepared,job.begin,job.end,job.smoothness,job.amount,job.seed,&stopping,stereo ? stereoOffset : 0);
                if(!changed) { outcome.store(NoChange,std::memory_order_relaxed); state.store(Finished,std::memory_order_release); continue; }
                prepared.resize(memoryWords,0); // allocation/reuse occurs on the worker only
                state.store(Ready,std::memory_order_release);
            } catch(...) { outcome.store(Failed,std::memory_order_relaxed); state.store(Finished,std::memory_order_release); }
        }
    }
public:
    explicit LoopMutator(Engine& e) : engine(e), worker([this]{run();}) {}
    // Owner must stop processing before destroying the worker.
    ~LoopMutator() {
        stopping.store(true,std::memory_order_release); worker.join();
        if(engine.fadingPcm==&prepared) engine.clearPcmFade();
    }
    LoopMutator(const LoopMutator&)=delete;
    LoopMutator& operator=(const LoopMutator&)=delete;
    // Request and service belong to the single audio/control thread. No lock,
    // allocation, file I/O, or condition-variable notification on this path.
    bool request(size_t begin,size_t end,float smoothness,float amount) {
        if(engine.recording || engine.capturePending || engine.frames<2 || end>engine.frames || begin>=end || end-begin<2) {
            outcome.store(Unavailable,std::memory_order_relaxed); return false;
        }
        int expected=Idle;
        if(!state.compare_exchange_strong(expected,Preparing,std::memory_order_acq_rel)) return false;
        requestData={begin,end,smoothness,amount,engine.sequence(),mutationRandom(nextSeed),cancelEpoch.load(std::memory_order_acquire)};
        outcome.store(None,std::memory_order_relaxed);
        state.store(Requested,std::memory_order_release); return true;
    }
    void service() {
        int s=state.load(std::memory_order_acquire);
        if(s==Ready) {
            if(requestData.epoch!=cancelEpoch.load(std::memory_order_acquire)) {
                outcome.store(Canceled,std::memory_order_relaxed); state.store(Idle,std::memory_order_release); return;
            }
            auto result=engine.publishPrepared(prepared,requestData.revision);
            if(result==Engine::Publication::Published) {
                outcome.store(Applied,std::memory_order_relaxed); state.store(Fading,std::memory_order_release);
            } else if(result==Engine::Publication::Obsolete) {
                outcome.store(SourceChanged,std::memory_order_relaxed); state.store(Idle,std::memory_order_release);
            }
        } else if(s==Finished || (s==Fading && !engine.fadingPcm)) state.store(Idle,std::memory_order_release);
    }
    void cancel() { cancelEpoch.fetch_add(1,std::memory_order_acq_rel); }
    bool busy() const { return state.load(std::memory_order_acquire)!=Idle; }
    int status() const { return state.load(std::memory_order_acquire); }
    int lastOutcome() const { return outcome.load(std::memory_order_relaxed); }
};
} // namespace otis
