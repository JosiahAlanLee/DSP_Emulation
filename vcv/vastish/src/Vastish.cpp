#include "plugin.hpp"
#include "engine/Engine.hpp"
#include "../../shared/SampleFile.hpp"
#include "../../shared/SampleShare.hpp"
#include "../../shared/ByteBus.hpp"
#include <osdialog.h>
#include <atomic>
#include <cctype>
// A loaded sample. Immutable once published; owned by the handoff below.
struct SampleSlot {
    dspemu::PcmSample data; vastish::SampleView view;
    uint64_t generation=0; // set on publish; identifies the data for the save cache
    explicit SampleSlot(dspemu::PcmSample s={}) : data(std::move(s)) {
        if(data.right.size()!=data.pcm.size()) data.right.clear(); // stereo needs equal channels
        view.pcm=data.pcm.empty() ? nullptr : data.pcm.data(); view.frames=data.pcm.size(); view.rate=data.rate;
        view.pcmR=data.right.empty() ? nullptr : data.right.data();
    }
};
using namespace vastish;
// Chain mode (docs/CHAIN.md): what a Vastish sends the Vastish on its right each sample, per
// voice, through Rack's double-buffered expander messages. The right one owns the two buffers.
struct ChainMessage {
    static constexpr uint32_t MAGIC=0x56435631; // "VCV1"
    uint32_t magic=0; int voices=1;
    std::array<float,8> tap{}, note{}, velocity{};
    std::array<uint8_t,8> gate{}, gateFree{};
};
struct Vastish : Module {
    // AUX, BUS and AUDIO inputs and FUN3/4 are appended so existing patches keep their cable ids.
    enum { PITCH_IN, GATE_IN, VEL_IN, MOD_IN, AUX1_IN, AUX2_IN, AUX3_IN, AUX4_IN, BUS_A_IN, BUS_B_IN, AUDIO_IN, INPUTS };
    enum { LEFT_OUT, RIGHT_OUT, AMP_OUT, ENV2_OUT, ENV3_OUT, LFO1_OUT, LFO2_OUT, FUN1_OUT, FUN2_OUT, FUN3_OUT, FUN4_OUT, BUS_OUT, OUTPUTS };
    enum { GATE_LIGHT, FAULT_LIGHT, LIGHTS };
    vastish::Engine engine;
    dsp::SchmittTrigger triggers[8];
    std::array<VoiceInput,8> in{};
    dspemu::bus::Reader busA,busB; dspemu::bus::Writer busWriter;
    uint8_t heldBusWord=128;
    std::atomic<int> requestedProgram{-1}, displayedProgram{1};
    std::atomic<bool> requestedPanic{false};
    // AUDIO IN full scale: ±5 V (default, matching the L/R output scaling) or ±10 V.
    std::atomic<bool> audioIn10V{false};
    // Chain mode: off by default, so side-by-side Vastishes in older patches are unchanged.
    ChainMessage chainMessages[2];
    std::atomic<bool> chainFromLeft{false};
    enum ChainState { CHAIN_OFF, CHAIN_NO_VASTISH, CHAIN_CONNECTED };
    std::atomic<int> chainState{CHAIN_OFF};
    // Sample handoff (SAMPLE pitch wave). The UI thread publishes into pendingSample;
    // the audio thread adopts it only when retiredSample is empty, moving its old
    // sample there; the UI thread deletes retired samples. The audio thread never
    // allocates, frees or waits.
    static constexpr size_t MAX_SAMPLE_FRAMES=size_t(1)<<21; // ~44 s at 48 kHz
    std::atomic<SampleSlot*> pendingSample{nullptr}, retiredSample{nullptr};
    SampleSlot* liveSample=nullptr; // audio thread only
    SampleSlot* uiSample=nullptr;   // UI thread: most recently published
    std::atomic<bool> sampleLoop{false};
    std::string sampleError;        // UI thread
    // dataToJson cache, keyed by publication generation. Not by pointer: a retired
    // slot's address can be reused by a later allocation holding different audio.
    uint64_t publishedGeneration=0, encodedGeneration=~uint64_t(0); std::string encodedPcm,encodedPcmR; // no published slot has ~0
    void collectSamples() { if(auto* r=retiredSample.exchange(nullptr,std::memory_order_acq_rel)) delete r; }
    void publishSample(SampleSlot* s) {
        collectSamples(); s->generation=++publishedGeneration; uiSample=s;
        if(auto* stale=pendingSample.exchange(s,std::memory_order_acq_rel)) delete stale; // never reached audio
    }
    // A two-channel WAV loads as a stereo sample unless mono is asked for.
    void loadSampleFile(const std::string& path,bool mono=false) {
        try { publishSample(new SampleSlot(dspemu::loadWav(path,MAX_SAMPLE_FRAMES,!mono))); sampleError.clear(); }
        catch(const std::exception& e) { sampleError=e.what(); WARN("Vastish: %s",e.what()); }
    }
    // OTIS link (UI thread only): with followOtis on and Vastish directly to the
    // right of an OTIS Sampler, the SAMPLE wave tracks OTIS's finished sample,
    // including new recordings and REWORK generations.
    bool followOtis=false;
    enum LinkState { LINK_OFF, LINK_NOT_FOUND, LINK_CONNECTED } linkState=LINK_OFF;
    const dspemu::SampleShareV1* followedShare=nullptr; uint32_t followedRevision=0; bool followedAny=false;
    // stereo: the share's stereo extension (OTIS 2.7.0+), or null for the mono copy.
    void followShare(const dspemu::SampleShareV1* share,const dspemu::SampleShareStereoV1* stereo=nullptr) {
        if(!followOtis) { linkState=LINK_OFF; return; }
        if(!share) { linkState=LINK_NOT_FOUND; followedShare=nullptr; return; }
        linkState=LINK_CONNECTED;
        if(share!=followedShare) { followedShare=share; followedAny=false; }
        uint32_t rev=share->revision(share->context);
        if((rev&1) || (followedAny && rev==followedRevision)) return; // mid-change, or nothing new
        double rate=0; uint32_t got=0, channels=1;
        size_t total=stereo ? stereo->copyStereo(share->context,nullptr,nullptr,0,&rate,&got,&channels) : share->copy(share->context,nullptr,0,&rate,&got);
        dspemu::PcmSample m;
        if(total>=2) {
            m.pcm.resize(std::min(total,MAX_SAMPLE_FRAMES));
            size_t again;
            if(stereo && channels==2) {
                m.right.resize(m.pcm.size());
                again=stereo->copyStereo(share->context,m.pcm.data(),m.right.data(),m.pcm.size(),&rate,&got,&channels);
                if(channels!=2) return;
            } else again=share->copy(share->context,m.pcm.data(),m.pcm.size(),&rate,&got);
            if(again!=total || got!=rev) return; // changed while copying; try next frame
            if(!std::isfinite(rate) || rate<1000 || rate>384000) return;
            m.rate=rate; m.truncated=total>m.pcm.size();
            char name[256]={}; share->name(share->context,name,sizeof name);
            m.name=std::string("OTIS: ")+name;
        } else m.pcm.clear(); // OTIS has no sample: follow it to silence
        publishSample(new SampleSlot(std::move(m)));
        followedRevision=rev; followedAny=true; sampleError.clear();
    }
    // Only an OtisLab OTIS Sampler directly to the left, exposing a valid share.
    static const dspemu::SampleShareV1* findOtisShare(Module* left) {
        if(!left || !left->model || !left->model->plugin) return nullptr;
        if(left->model->plugin->slug!="OtisLab" || left->model->slug!="OtisSampler") return nullptr;
        void* p=left->rightExpander.producerMessage;
        return dspemu::validShare(p) ? static_cast<const dspemu::SampleShareV1*>(p) : nullptr;
    }
    // Its stereo extension: OTIS 2.7.0+ only.
    static const dspemu::SampleShareStereoV1* findOtisStereo(Module* left) {
        auto* s=findOtisShare(left);
        return s ? dspemu::stereoExtension(s,dspemu::providerHasStereo(left->model->plugin->slug.c_str(),left->model->plugin->version.c_str())) : nullptr;
    }
    void setFollowOtis(bool on) { followOtis=on; followedShare=nullptr; followedAny=false; if(!on) linkState=LINK_OFF; }
    // Compact form for the panel selector: no "OTIS: " prefix or ".wav" extension;
    // a left arrow marks a sample that comes from OTIS.
    std::string sampleShortLabel() const {
        if(!uiSample || !uiSample->view.valid()) return "NO SAMPLE";
        std::string n=uiSample->data.name; bool fromOtis=false;
        if(n.rfind("OTIS: ",0)==0) { n=n.substr(6); fromOtis=true; }
        if(n.size()>4) { std::string ext=n.substr(n.size()-4); for(auto& c:ext) c=char(std::tolower(uint8_t(c))); if(ext==".wav") n.resize(n.size()-4); }
        if(n.empty()) n="SAMPLE";
        if(uiSample->data.stereo()) n="ST "+n;
        return fromOtis ? "\xe2\x97\x80 "+n : n;
    }
    std::string sampleLabel() const {
        if(!uiSample || !uiSample->view.valid()) return "NO SAMPLE";
        return uiSample->data.name+(uiSample->data.stereo() ? " (stereo)" : "")+(uiSample->data.truncated ? " [TRUNCATED]" : "");
    }
    ~Vastish() {
        delete liveSample; delete pendingSample.load(); delete retiredSample.load();
    }
    Vastish() {
        config(PARAM_COUNT,INPUTS,OUTPUTS,LIGHTS);
        const auto& meta=metadata();
        for(int i=0;i<PARAM_COUNT;++i) {
            if(meta[i].options.empty()) configParam(i,meta[i].lo,meta[i].hi,programs()[1][i],meta[i].name);
            else configSwitch(i,meta[i].lo,meta[i].hi,programs()[1][i],meta[i].name,meta[i].options);
        }
        configInput(PITCH_IN,"Pitch, 1 V/oct (0 V = C4)");
        configInput(GATE_IN,"Gate, up to eight voices");
        configInput(VEL_IN,"Velocity, 0–10 V (latched on gate rise)");
        configInput(MOD_IN,"Mod wheel, 0–10 V");
        for(int k=0;k<4;++k) configInput(AUX1_IN+k,string::f("AUX %d, ±10 V = ±1 source, one value per voice",k+1));
        configInput(BUS_A_IN,"Byte bus A; optional CLK on channel 9 (primary clock)");
        configInput(BUS_B_IN,"Byte bus B; optional CLK on channel 9");
        configInput(AUDIO_IN,"Audio in: replaces the oscillator when PITCH is AUDIO; one voice per channel, mono reaches every voice (±5 V full scale; ±10 V in the menu)");
        const char* outNames[]={"Left audio","Right audio","Amp envelope","Envelope 2","Envelope 3","LFO 1","LFO 2","FUN 1","FUN 2","FUN 3","FUN 4"};
        for(int i=0;i<=FUN4_OUT;++i) configOutput(i,outNames[i]);
        configOutput(BUS_OUT,"Selected word or eight logical lanes; primary bus clock forwarded");
        leftExpander.producerMessage=&chainMessages[0]; leftExpander.consumerMessage=&chainMessages[1];
    }
    void onSampleRateChange(const SampleRateChangeEvent& e) override { engine.prepare(e.sampleRate); }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); engine.panic(); for(auto& t:triggers) t.reset(); in={}; displayedProgram=1;
        busA.reset(); busB.reset(); heldBusWord=128; audioIn10V=false; chainFromLeft=false;
    }
    void process(const ProcessArgs& args) override {
        if(requestedPanic.exchange(false)) {
            engine.panic();
            // Suppress held gates until they fall, so Panic also works during sustained CV.
            for(int i=0;i<8;++i) in[i].gate=false;
        }
        int pg=requestedProgram.exchange(-1);
        if(pg>=0 && pg<6) {
            for(int i=0;i<ORIGINAL_PARAM_COUNT;++i) params[i].setValue(programs()[pg][i]);
            for(int f=0;f<FUN_COUNT;++f) params[FUN1_BIT+f].setValue(0);
            displayedProgram=pg;
        }
        Parameters p;
        for(int i=0;i<PARAM_COUNT;++i) {
            float x=params[i].getValue(); p[i]=std::isfinite(x)?vastish::clamp(x,metadata()[i].lo,metadata()[i].hi):programs()[0][i];
        }
        // Chain mode: with a Vastish directly to the left, its voices' signals, notes and gates
        // arrive one sample later (Rack's expander message) and replace this one's oscillator
        // and PITCH, GATE and VEL; the voice count is the left one's.
        const ChainMessage* chain=nullptr;
        if(chainFromLeft.load(std::memory_order_relaxed)) {
            Module* left=leftExpander.module;
            auto* m=left && left->model==modelVastish ? static_cast<const ChainMessage*>(leftExpander.consumerMessage) : nullptr;
            chain=m && m->magic==ChainMessage::MAGIC ? m : nullptr;
            chainState.store(chain ? CHAIN_CONNECTED : CHAIN_NO_VASTISH,std::memory_order_relaxed);
        } else chainState.store(CHAIN_OFF,std::memory_order_relaxed);
        // AUDIO wave: AUDIO IN's channels establish voices as PITCH and GATE do. Voice v reads
        // channel v; a mono cable reaches every voice, and a voice without a channel reads silence.
        const bool audioWave=choice(p[PITCH_WAVE],AUDIO_WAVE)==AUDIO_WAVE;
        const bool audioPatched=inputs[AUDIO_IN].isConnected(), gatePatched=inputs[GATE_IN].isConnected();
        const int audioChannels=audioPatched ? inputs[AUDIO_IN].getChannels() : 0;
        int channels=chain ? std::max(1,std::min(8,chain->voices)) :
            std::min(8,std::max({inputs[PITCH_IN].getChannels(),inputs[GATE_IN].getChannels(),audioWave ? audioChannels : 0}));
        const float audioScale=audioIn10V.load(std::memory_order_relaxed) ? .1f : .2f;
        auto a=busA.process(inputs[BUS_A_IN]), b=busB.process(inputs[BUS_B_IN]);
        bool lanes=choice(p[BUS_OUT_MODE],2)==2;
        bool controlAll=a.connected || b.connected || lanes;
        bool active=false;
        for(int i=0;i<8;++i) {
            auto voltage=[&](int port) {
                float x=inputs[port].getPolyVoltage(i); return std::isfinite(x)?x:0.f;
            };
            in[i].chained=chain!=nullptr;
            if(chain) {
                in[i].chainAudio=chain->tap[i]; in[i].chainGateFree=chain->gateFree[i]!=0;
                in[i].note=chain->note[i]; in[i].velocity=chain->velocity[i]; in[i].gate=chain->gate[i]!=0;
                in[i].audioConnected=false; in[i].gateConnected=true;
                in[i].modwheel=vastish::clamp(voltage(MOD_IN)*.1f,0,1);
                for(int k=0;k<4;++k) in[i].aux[k]=vastish::clamp(voltage(AUX1_IN+k)*.1f,-1,1);
                in[i].busAConnected=a.connected;
                in[i].busA=a.connected ? dspemu::bus::bipolar(a.word) : 0.f;
                in[i].busB=b.connected ? dspemu::bus::bipolar(b.word) : 0.f;
                in[i].laneA=a.connected && (a.word&(1<<i)) ? 1.f : 0.f;
                in[i].laneB=b.connected && (b.word&(1<<i)) ? 1.f : 0.f;
                active|=in[i].gate;
                continue;
            }
            bool was=triggers[i].isHigh();
            triggers[i].process(i<channels?voltage(GATE_IN):0.f);
            bool rise=!was && triggers[i].isHigh();
            in[i].audioConnected=audioPatched && i<channels && (audioChannels==1 || i<audioChannels);
            float audio=in[i].audioConnected ? inputs[AUDIO_IN].getVoltage(audioChannels==1 ? 0 : i)*audioScale : 0.f;
            in[i].audio=std::isfinite(audio) ? audio : 0.f;
            in[i].gateConnected=gatePatched;
            if(i<channels) {
                // Pitch is held after note-off, matching the Pd voice's stored MIDI key. A gate-free
                // AUDIO voice has no notes, so it follows PITCH continuously for key tracking.
                if(triggers[i].isHigh() || (audioWave && in[i].audioConnected && !gatePatched))
                    in[i].note=vastish::clamp(60+12*voltage(PITCH_IN),-20,135);
                if(rise) { in[i].velocity=inputs[VEL_IN].isConnected()?vastish::clamp(voltage(VEL_IN)*.1f,0,1):1; in[i].gate=true; }
                in[i].modwheel=vastish::clamp(voltage(MOD_IN)*.1f,0,1);
            }
            // AUX: per voice from a poly cable; a mono cable reaches every voice.
            for(int k=0;k<4;++k) in[i].aux[k]=vastish::clamp(voltage(AUX1_IN+k)*.1f,-1,1);
            in[i].busAConnected=a.connected;
            in[i].busA=a.connected ? dspemu::bus::bipolar(a.word) : 0.f;
            in[i].busB=b.connected ? dspemu::bus::bipolar(b.word) : 0.f;
            in[i].laneA=a.connected && (a.word&(1<<i)) ? 1.f : 0.f;
            in[i].laneB=b.connected && (b.word&(1<<i)) ? 1.f : 0.f;
            if(!triggers[i].isHigh()) in[i].gate=false;
            active|=in[i].gate;
        }
        if(!retiredSample.load(std::memory_order_acquire))
            if(auto* s=pendingSample.exchange(nullptr,std::memory_order_acq_rel)) {
                if(liveSample) retiredSample.store(liveSample,std::memory_order_release);
                liveSample=s;
            }
        SampleView view; if(liveSample) { view=liveSample->view; view.loop=sampleLoop.load(std::memory_order_relaxed); }
        int selected=choice(p[BUS_OUT_SOURCE],BUS_SOURCE_MIX);
        auto y=engine.tick(p,in,std::max(1,channels),liveSample ? &view : nullptr,controlAll,lanes && selected==BUS_SOURCE_DSP);
        outputs[LEFT_OUT].setVoltage(y[0]*5); outputs[RIGHT_OUT].setVoltage(y[1]*5);
        // Send the chain on to a Vastish directly to the right, whether or not it is chaining.
        if(Module* right=rightExpander.module; right && right->model==modelVastish)
            if(auto* m=static_cast<ChainMessage*>(right->leftExpander.producerMessage)) {
                m->magic=ChainMessage::MAGIC; m->voices=std::max(1,channels);
                for(int i=0;i<8;++i) {
                    m->tap[i]=engine.voices[i].tap; m->note[i]=in[i].note; m->velocity[i]=in[i].velocity;
                    m->gate[i]=in[i].gate;
                    m->gateFree[i]=in[i].chained ? in[i].chainGateFree : audioWave && in[i].audioConnected && !gatePatched;
                }
                right->leftExpander.messageFlipRequested=true;
            }
        const int source[]={SRC_AMPENV,1,2,3,4,SRC_FUN1,SRC_FUN2,SRC_FUN3,SRC_FUN4};
        int n=controlAll ? 8 : std::max(1,channels);
        for(int o=AMP_OUT;o<=FUN4_OUT;++o) {
            outputs[o].setChannels(n);
            for(int i=0;i<n;++i) outputs[o].setVoltage(engine.voices[i].sources[source[o-AMP_OUT]]*10,i);
        }
        auto value=[&](int voice) {
            return selected==BUS_SOURCE_DSP ? engine.voices[voice].tap : selected==BUS_SOURCE_MIX ?
                (y[0]+y[1])*.5f : engine.voices[voice].sources[selected];
        };
        const auto& primary=a.connected ? a : b;
        if(choice(p[BUS_OUT_MODE],2)==0) {
            heldBusWord=128; busWriter.write(outputs[BUS_OUT],heldBusWord,false,false);
        } else {
            if(!primary.clocked || primary.updated) {
                heldBusWord=0;
                if(lanes) { for(int i=0;i<8;++i) if(positiveBit(value(i))) heldBusWord|=uint8_t(1<<i); }
                else heldBusWord=dspemu::bus::fromBipolar(value(0));
            }
            busWriter.write(outputs[BUS_OUT],heldBusWord,primary.clocked,primary.clk);
        }
        lights[GATE_LIGHT].setBrightness(active?1:0);
        lights[FAULT_LIGHT].setBrightness(engine.faults?1:0);
        (void)args;
    }
    json_t* dataToJson() override {
        json_t* j=json_object(); json_object_set_new(j,"program",json_integer(displayedProgram.load()));
        json_object_set_new(j,"sampleLoop",json_boolean(sampleLoop.load()));
        json_object_set_new(j,"followOtis",json_boolean(followOtis));
        json_object_set_new(j,"audioInRange",json_integer(audioIn10V ? 10 : 5));
        json_object_set_new(j,"chainFromLeft",json_boolean(chainFromLeft.load()));
        collectSamples();
        if(uiSample && uiSample->view.valid()) {
            // Rack autosaves often; encode each sample once.
            if(encodedGeneration!=uiSample->generation) {
                const auto& d=uiSample->data;
                encodedPcm=dspemu::encodePcm16(d.pcm.data(),d.pcm.size());
                encodedPcmR=d.stereo() ? dspemu::encodePcm16(d.right.data(),d.right.size()) : std::string();
                encodedGeneration=uiSample->generation;
            }
            json_t* s=json_object();
            json_object_set_new(s,"name",json_string(uiSample->data.name.c_str()));
            json_object_set_new(s,"rate",json_real(uiSample->data.rate));
            json_object_set_new(s,"pcm16le",json_string(encodedPcm.c_str()));
            // A stereo sample's right channel; Vastish before 2.4 loads the left channel only.
            if(!encodedPcmR.empty()) json_object_set_new(s,"pcm16le_r",json_string(encodedPcmR.c_str()));
            json_object_set_new(j,"sample",s);
        }
        return j;
    }
    void dataFromJson(json_t* j) override {
        json_t* v=json_object_get(j,"program"); if(json_is_integer(v)) displayedProgram=int(vastish::clamp(float(json_integer_value(v)),0,5));
        sampleLoop=json_is_true(json_object_get(j,"sampleLoop"));
        setFollowOtis(json_is_true(json_object_get(j,"followOtis")));
        json_t* range=json_object_get(j,"audioInRange"); audioIn10V=json_is_integer(range) && json_integer_value(range)==10;
        chainFromLeft=json_is_true(json_object_get(j,"chainFromLeft")); // absent before 2.8: off
        json_t* s=json_object_get(j,"sample");
        // A full restore defines the sample too: no saved sample means none, not
        // "keep whatever this instance had".
        if(!s) { if(uiSample && uiSample->view.valid()) publishSample(new SampleSlot()); sampleError.clear(); return; }
        try {
            dspemu::PcmSample m;
            const char* text=json_string_value(json_object_get(s,"pcm16le"));
            if(!text) throw std::runtime_error("Saved sample has no PCM");
            m.pcm=dspemu::decodePcm16(text,MAX_SAMPLE_FRAMES); m.rate=json_number_value(json_object_get(s,"rate"));
            if(const char* textR=json_string_value(json_object_get(s,"pcm16le_r"))) {
                m.right=dspemu::decodePcm16(textR,MAX_SAMPLE_FRAMES);
                if(m.right.size()!=m.pcm.size()) throw std::runtime_error("Invalid saved stereo sample");
            }
            if(!std::isfinite(m.rate) || m.rate<1000 || m.rate>384000 || m.pcm.size()<2) throw std::runtime_error("Invalid saved sample");
            const char* name=json_string_value(json_object_get(s,"name")); m.name=name ? name : "RESTORED SAMPLE";
            publishSample(new SampleSlot(std::move(m))); sampleError.clear();
        } catch(const std::exception& e) { sampleError=e.what(); WARN("Vastish: %s",e.what()); }
    }
};
struct Selector : app::ParamWidget {
    Selector() { box.size=Vec(100,20); }
    void draw(const DrawArgs& args) override {
        auto* q=getParamQuantity();
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(29,39,45)); nvgFill(args.vg);
        auto font=APP->window->uiFont; if(!font) return;
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,10); nvgTextAlign(args.vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(135,225,200));
        std::string value=q?q->getDisplayValueString():"—";
        // The SAMPLE wave shows which sample is loaded: a short name, marked with an
        // arrow when it comes from OTIS on the left, truncated rather than shrunk.
        bool sampleName=false;
        if(q && q->paramId==PITCH_WAVE && choice(q->getValue(),AUDIO_WAVE)==SAMPLE_WAVE)
            if(auto* v=dynamic_cast<Vastish*>(module)) { value=v->sampleShortLabel(); sampleName=true; }
        if(sampleName) {
            nvgFontSize(args.vg,9);
            float avail=box.size.x-8; float bounds[4];
            auto width=[&](const std::string& s) { return nvgTextBounds(args.vg,0,0,s.c_str(),nullptr,bounds); };
            if(width(value)>avail) {
                while(value.size()>1 && width(value+"\xe2\x80\xa6")>avail) {
                    value.pop_back();
                    while(!value.empty() && (uint8_t(value.back())&0xC0)==0x80) value.pop_back(); // keep UTF-8 whole
                }
                value+="\xe2\x80\xa6";
            }
        } else
            nvgFontSize(args.vg,std::min(10.f,(box.size.x-8)/std::max(1.f,float(value.size())*.65f)));
        nvgText(args.vg,box.size.x*.5,box.size.y*.5,value.c_str(),nullptr);
        ParamWidget::draw(args);
    }
    void onButton(const event::Button& e) override {
        if(e.action==GLFW_PRESS && e.button==GLFW_MOUSE_BUTTON_LEFT && getParamQuantity()) {
            auto* q=getParamQuantity(); int id=q->paramId;
            auto* m=createMenu(); auto& info=metadata()[id];
            for(size_t k=0;k<info.options.size();++k) {
                float v=info.lo+k;
                m->addChild(createCheckMenuItem(info.options[k],"",[q,v]{return q->getValue()==v;},[q,v]{q->setValue(v);}));
            }
            e.consume(this); return;
        }
        ParamWidget::onButton(e);
    }
};
struct ProgramButton : widget::OpaqueWidget {
    Vastish* module=nullptr;
    ProgramButton() { box.size=Vec(174,22); }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3); nvgFillColor(args.vg,nvgRGB(29,39,45)); nvgFill(args.vg);
        auto f=APP->window->uiFont; if(!f) return; nvgFontFaceId(args.vg,f->handle);
        nvgFontSize(args.vg,12); nvgFillColor(args.vg,nvgRGB(135,225,200)); nvgTextAlign(args.vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        nvgText(args.vg,box.size.x/2,11,programNames[module?module->displayedProgram.load():1],nullptr);
    }
    void onButton(const event::Button& e) override {
        if(module && e.action==GLFW_PRESS && e.button==GLFW_MOUSE_BUTTON_LEFT) {
            auto* m=createMenu(); for(int k=0;k<6;++k) m->addChild(createMenuItem(programNames[k],"",[this,k]{module->requestedProgram=k;}));
            e.consume(this);
        }
    }
};
struct VastishWidget : ModuleWidget {
    VastishWidget(Vastish* m) {
        setModule(m); setPanel(createPanel(asset::plugin(pluginInstance,"res/Vastish.svg")));
        // 56 HP (840 px). The right-hand column holds AUX 1-4; FUN 3/4 extend the FUN block.
        addChild(createWidget<ScrewSilver>(Vec(2,0))); addChild(createWidget<ScrewSilver>(Vec(822,0)));
        addChild(createWidget<ScrewSilver>(Vec(2,365))); addChild(createWidget<ScrewSilver>(Vec(822,365)));
        auto select=[&](int id,float x,float y,float w) {
            auto* s=createParam<Selector>(Vec(x,y),m,id); s->box.size.x=w; addParam(s);
        };
        auto knob=[&](int id,float x,float y) { addParam(createParamCentered<RoundSmallBlackKnob>(Vec(x,y),m,id)); };
        auto* program=createWidget<ProgramButton>(Vec(200,8)); program->module=m; addChild(program);
        select(ALG,386,8,194); knob(MASTER,660,20);
        select(PITCH_WAVE,15,52,77); knob(PITCH_PW,103,62);
        for(int c=1;c<=3;++c) { select(F1_TYPE+(c-1)*2,c*120+15,52,76); knob(F1_B+(c-1)*2,c*120+103,62); }
        // Each control path has two source/depth pairs plus key and velocity tracking.
        for(int c=0;c<6;++c) {
            float x=c*120; int base=PITCH_ADJ+c*7;
            knob(base,x+60,103); knob(base+1,x+30,143); knob(base+2,x+90,143);
            select(base+3,x+10,173,70); knob(base+4,x+101,183);
            select(base+5,x+10,208,70); knob(base+6,x+101,218);
        }
        for(int e=0;e<3;++e) for(int k=0;k<4;++k) knob(AMP_A+e*4+k,e*120+24+24*k,281);
        for(int l=0;l<2;++l) { float x=(l+3)*120; knob(LFO1_RATE+l*2,x+27,281); select(LFO1_SHAPE+l*2,x+49,271,65); }
        for(int f=0;f<FUN_COUNT;++f) for(int k=0;k<3;++k)
            select(funBase(f)+k,610+f*50,252+k*24,47); // clears the output labels at y=331
        for(int i=0;i<Vastish::AUX1_IN;++i) addInput(createInputCentered<PJ301MPort>(Vec(30+i*48,348),m,i));
        for(int k=0;k<4;++k) addInput(createInputCentered<PJ301MPort>(Vec(780,92+40*k),m,Vastish::AUX1_IN+k));
        for(int i=0;i<=Vastish::FUN4_OUT;++i) addOutput(createOutputCentered<PJ301MPort>(Vec(245+i*40,348),m,i));
        addInput(createInputCentered<PJ301MPort>(Vec(705,348),m,Vastish::BUS_A_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(755,348),m,Vastish::BUS_B_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(210,348),m,Vastish::AUDIO_IN));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(805,348),m,Vastish::BUS_OUT));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(180,14),m,Vastish::GATE_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(180,27),m,Vastish::FAULT_LIGHT));
    }
    void step() override {
        if(auto* m=dynamic_cast<Vastish*>(module)) {
            m->collectSamples();
            // Rack writes leftExpander.moduleId on this (UI) thread and the
            // leftExpander.module pointer on the engine thread, so resolve the ID
            // here. Modules are only deleted from this thread, so the result stays
            // valid for the rest of this step.
            int64_t id=m->leftExpander.moduleId;
            Module* left=id>=0 ? APP->engine->getModule(id) : nullptr;
            m->followShare(Vastish::findOtisShare(left),Vastish::findOtisStereo(left));
        }
        ModuleWidget::step();
    }
    void appendContextMenu(Menu* menu) override {
        auto* m=dynamic_cast<Vastish*>(module); if(!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Load Pd program","",[m](Menu* child) {
            for(int k=0;k<6;++k) child->addChild(createMenuItem(programNames[k],"",[m,k]{m->requestedProgram=k;}));
        }));
        menu->addChild(createMenuItem("Panic / silence held gates","",[m]{m->requestedPanic=true;}));
        for(int param:{BUS_OUT_MODE,BUS_OUT_SOURCE}) menu->addChild(createSubmenuItem(param==BUS_OUT_MODE ? "BUS OUT mode" : "BUS OUT source","",[m,param](Menu* child) {
            const auto& info=metadata()[param];
            for(size_t k=0;k<info.options.size();++k) child->addChild(createCheckMenuItem(info.options[k],"",
                [m,param,k]{return choice(m->params[param].getValue(),int(metadata()[param].hi))==int(k);},
                [m,param,k]{m->params[param].setValue(float(k));}));
        }));
        menu->addChild(createBoolMenuItem("Chain from the Vastish on the left","",[m]{return m->chainFromLeft.load();},[m](bool on){m->chainFromLeft=on;}));
        if(m->chainFromLeft.load()) menu->addChild(createMenuLabel(m->chainState.load()==Vastish::CHAIN_CONNECTED ?
            "Chain: voices continue the Vastish on the left" : "Chain: place this Vastish directly right of another"));
        menu->addChild(createSubmenuItem("AUDIO IN range","",[m](Menu* child) {
            for(bool ten:{false,true}) child->addChild(createCheckMenuItem(ten ? "±10 V" : "±5 V (L/R output level)","",
                [m,ten]{return m->audioIn10V.load()==ten;},[m,ten]{m->audioIn10V=ten;}));
        }));
        for(int f=0;f<FUN_COUNT;++f) menu->addChild(createCheckMenuItem(string::f("FUN %d BIT normalization",f+1),"",
            [m,f]{return m->params[FUN1_BIT+f].getValue()>=.5f;},
            [m,f]{m->params[FUN1_BIT+f].setValue(m->params[FUN1_BIT+f].getValue()<.5f ? 1.f : 0.f);}));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Sample: "+m->sampleLabel()));
        if(!m->sampleError.empty()) menu->addChild(createMenuLabel("Error: "+m->sampleError));
        menu->addChild(createMenuItem("Load sample WAV…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav,WAV"); char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if(!path) return;
            m->setFollowOtis(false); m->loadSampleFile(path); std::free(path);
            if(m->sampleError.empty()) m->params[PITCH_WAVE].setValue(SAMPLE_WAVE);
        }));
        menu->addChild(createMenuItem("Load sample WAV as mono…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav,WAV"); char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if(!path) return;
            m->setFollowOtis(false); m->loadSampleFile(path,true); std::free(path);
            if(m->sampleError.empty()) m->params[PITCH_WAVE].setValue(SAMPLE_WAVE);
        }));
        menu->addChild(createBoolMenuItem("Loop sample","",[m]{return m->sampleLoop.load();},[m](bool on){m->sampleLoop=on;}));
        menu->addChild(createMenuItem("Clear sample","",[m]{ m->setFollowOtis(false); m->publishSample(new SampleSlot()); }));
        menu->addChild(createBoolMenuItem("Follow OTIS on the left","",[m]{return m->followOtis;},
            [m](bool on){ m->setFollowOtis(on); if(on) m->params[PITCH_WAVE].setValue(SAMPLE_WAVE); }));
        if(m->followOtis) menu->addChild(createMenuLabel(m->linkState==Vastish::LINK_CONNECTED ? "OTIS link: connected" :
            "OTIS link: place Vastish directly right of OTIS"));
        menu->addChild(createMenuLabel("SAMPLE wave: 0 V / C4 plays at recorded pitch"));
        menu->addChild(createMenuLabel("Stereo samples: each side through its own blocks; PAN is balance"));
        menu->addChild(createMenuLabel("Eight voices • 0 V = C4 • gate > 1 V"));
        menu->addChild(createMenuLabel("Audio: ±1 DSP = ±5 V. Envelopes: 0–10 V."));
        menu->addChild(createMenuLabel("Programs are starting points; all edits save in the Rack patch."));
    }
};
Model* modelVastish=createModel<Vastish,VastishWidget>("Vastish");
