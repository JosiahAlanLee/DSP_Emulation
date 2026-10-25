#include "plugin.hpp"
#include "engine/OtisEngine.hpp"
#include "engine/LoopMutator.hpp"
#include "engine/Wav.hpp"
#include "engine/FrameTable.hpp"
#include "../../shared/SampleShare.hpp"
#include "../../shared/ByteBus.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <osdialog.h>
#include <mutex>
#include <atomic>

struct OtisSampler : Module {
    // CLOCK and INTERP (LAB column) are appended so existing patches keep their parameter ids.
    enum Params { PITCH, START, END, K1, K2, RECORD_GAIN, OUTPUT_GAIN, CAPTURE_RATE, PLAY, RECORD, LOOP, REVERSE, CLOCK, INTERP, POSITION, SIZE, WINDOW_MODE, SMOOTHNESS, MUTATION_AMOUNT, MUTATE, NUM_PARAMS };
    // LEVEL_IN and AUDIO_R_IN are appended so existing patches keep their cable ids.
    enum Inputs { AUDIO_IN, PITCH_IN, GATE_IN, RECORD_IN, START_IN, END_IN, K1_IN, K2_IN, LEVEL_IN, CLOCK_IN, INTERP_IN, POSITION_IN, SIZE_IN, MUTATE_IN, MUTATION_AMOUNT_IN, AUDIO_R_IN, FRAME_BUS_IN, NUM_INPUTS };
    enum Outputs { LEFT_OUT, RIGHT_OUT, EOC_OUT, POSITION_OUT, NUM_OUTPUTS };
    enum Lights { PLAY_LIGHT, RECORD_LIGHT, FULL_LIGHT, MUTATION_LIGHT, NUM_LIGHTS };
    otis::Engine engine;
    otis::LoopMutator mutator{engine};
    std::mutex engineMutex;
    std::mutex nameMutex;
    std::array<dsp::SchmittTrigger,16> gates;
    dsp::SchmittTrigger playButton;
    dsp::SchmittTrigger recordGate,mutationGate;
    dspemu::bus::Reader frameReader;
    bool previousRecord=false, previousReverse=false;
    std::atomic<bool> gated{false};
    std::atomic<bool> linearLevel{false}, declick{true}, varispeed{false};
    std::atomic<bool> smoothFrameBus{true};
    std::atomic<int> recordSource{0};
    // UI-thread cache of the encoded sample, keyed by the engine's sample sequence counter.
    bool pcmCacheValid=false; uint32_t pcmCacheSequence=0; double pcmCacheRate=0; std::string pcmCacheText,pcmCacheTextR;
    std::atomic<int> engineMode{0},filterMode{2};
    std::atomic<size_t> displayedFrames{0};
    std::atomic<double> displayedRate{otis::captureRate(5)};
    std::atomic<bool> displayedRecording{false},displayedStereo{false};
    std::atomic<bool> liveSample{false};
    std::string sampleName="NO SAMPLE - RIGHT CLICK TO LOAD";
    std::string lastError;
    // Lets a module placed to the right (Vastish) read the finished sample. See
    // shared/SampleShare.hpp. Served from that module's UI thread; snapshot() only
    // takes the finished-sample lock, which the audio thread never waits on.
    dspemu::SampleShareV1 share{};
    void exposeShare() {
        share.magic=dspemu::SAMPLE_SHARE_MAGIC; share.version=dspemu::SAMPLE_SHARE_VERSION; share.context=this;
        share.revision=[](void* c)->uint32_t { return static_cast<OtisSampler*>(c)->engine.sequence(); };
        share.copy=[](void* c,int16_t* dst,size_t capacity,double* rate,uint32_t* revision)->size_t {
            try {
                // SampleShareV1 is mono: a stereo sample is shared as (L+R)/2.
                std::vector<int16_t> pcm,right; double r=0; uint32_t s=0;
                if(static_cast<OtisSampler*>(c)->engine.snapshotStereo(pcm,right,r,s))
                    for(size_t i=0;i<pcm.size();i++) pcm[i]=int16_t((int32_t(pcm[i])+right[i])>>1);
                if(dst) std::memcpy(dst,pcm.data(),std::min(capacity,pcm.size())*sizeof(int16_t));
                if(rate) *rate=r;
                if(revision) *revision=s;
                return pcm.size();
            } catch(...) { return 0; }
        };
        share.name=[](void* c,char* buffer,size_t size) {
            if(!buffer || !size) return;
            auto* m=static_cast<OtisSampler*>(c);
            std::lock_guard<std::mutex> guard(m->nameMutex);
            const std::string& n=m->liveSample.load() ? std::string("LIVE RECORDING") : m->sampleName;
            std::snprintf(buffer,size,"%s",n.c_str());
        };
        // Fixed pointers; no flip is ever requested, so Rack leaves them alone.
        rightExpander.producerMessage=rightExpander.consumerMessage=&share;
    }
    OtisSampler() {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        exposeShare();
        configParam(PITCH,-48,48,0,"Pitch offset"," semitones");
        configParam(START,0,1,0,"Start / loop start","%",0,100);
        configParam(END,0,1,1,"End / loop end","%",0,100);
        configParam(K1,0,1,1,"Filter K1 coefficient","%",0,100);
        configParam(K2,0,1,1,"Filter K2 coefficient","%",0,100);
        configParam(CLOCK,-6,3,0,"LAB chip clock offset (0 = authentic; double-click to reset)"," oct");
        configParam(INTERP,0,9,9,"LAB interpolation bits (9 = chip; 0 = drop-sample)"," bits");
        configParam(POSITION,0,1,0,"LAB window position / frame selection","%",0,100);
        configParam(SIZE,1,16,8,"LAB window size (CV = octaves of samples)"," samples",2);
        configSwitch(WINDOW_MODE,0,2,0,"LAB window addressing",{"Legacy segment","Fixed-size scan","Transwave-style frames"});
        configParam(SMOOTHNESS,0,1,.5,"LAB loop rework: coarse to fine","%",0,100);
        configParam(MUTATION_AMOUNT,0,1,.5,"LAB loop rework amount","%",0,100);
        configButton(MUTATE,"Rework selected voice-1 window (original experimental algorithm)");
        configParam(RECORD_GAIN,0,4,1,"Recording gain","x");
        configParam(OUTPUT_GAIN,0,2,1,"Output gain","x");
        configSwitch(CAPTURE_RATE,0,6,5,"Capture rate",{"11.16 kHz","12.76 kHz","14.88 kHz","17.86 kHz","22.32 kHz","29.76 kHz","44.64 kHz"});
        configButton(PLAY,"Play / retrigger voice 1");
        configSwitch(RECORD,0,1,0,"Record",{"Off","On"});
        configSwitch(LOOP,0,2,0,"Loop",{"Off","Forward","Ping-pong"});
        configSwitch(REVERSE,0,1,0,"Reverse",{"Off","On"});
        configInput(AUDIO_IN,"Recording audio, L or mono (±5 V = full scale)");
        configInput(AUDIO_R_IN,"Recording audio R: patched when a take starts, it records stereo");
        configInput(PITCH_IN,"Pitch (1 V/oct; 0 V = original pitch)");
        configInput(GATE_IN,"Play gate / trigger (polyphonic)");
        configInput(RECORD_IN,"Record gate (high = record)");
        configInput(START_IN,"Start CV, polyphonic per voice (0–10 V adds 0–100%)");
        configInput(END_IN,"End CV, polyphonic per voice (0–10 V adds 0–100%)");
        configInput(K1_IN,"K1 CV, polyphonic per voice (0–10 V adds 0–100%)");
        configInput(K2_IN,"K2 CV, polyphonic per voice (0–10 V adds 0–100%)");
        configInput(CLOCK_IN,"LAB chip clock CV, 1 V/oct added to CLOCK");
        configInput(INTERP_IN,"LAB interpolation CV, 1.1 V per bit added to INTERP");
        configInput(POSITION_IN,"LAB window position / frame selection, polyphonic (0–10 V)");
        configInput(FRAME_BUS_IN,"Byte frame selection in FRM mode (LSB first; optional CLK on channel 9)");
        configInput(SIZE_IN,"LAB window size, polyphonic (1 V doubles sample count)");
        configInput(MUTATE_IN,"Rework selected voice-1 window on rising gate (2 V)");
        configInput(MUTATION_AMOUNT_IN,"Rework amount CV (0–10 V), latched on trigger");
        configInput(LEVEL_IN,"VCA: per-voice chip volume, polyphonic (0–10 V; 10 V = unity; unpatched = unity)");
        configOutput(LEFT_OUT,"Audio L (a mono sample plays on both outputs)"); configOutput(RIGHT_OUT,"Audio R");
        configOutput(EOC_OUT,"End / loop crossing (1 ms, 10 V)");
        configOutput(POSITION_OUT,"First active voice position (0–10 V)");
    }
    void refreshDisplay() {
        displayedFrames.store(engine.recording ? engine.recordFrames : engine.frames,std::memory_order_relaxed);
        displayedRate.store(engine.recording ? engine.recordBuffer.rate : engine.sample.rate,std::memory_order_relaxed);
        displayedRecording.store(engine.recording,std::memory_order_relaxed);
        displayedStereo.store(engine.recording ? engine.recordBuffer.stereo : engine.sample.stereo,std::memory_order_relaxed);
    }
    // A two-channel WAV loads as stereo (half the memory and voices) unless mono is asked for.
    void load(const std::string& path,bool mono=false) {
        try {
            auto wav=otis::loadWav(path,!mono); // file I/O and allocations run on the UI thread
            size_t slash=path.find_last_of("/\\"); std::string name=path.substr(slash==std::string::npos ? 0 : slash+1);
            std::vector<int16_t> image;
            if(wav.stereo) image=otis::stereoImage(wav.pcm.data(),wav.right.data(),wav.pcm.size());
            std::lock_guard<std::mutex> guard(engineMutex);
            if(wav.stereo) engine.install(image,wav.rate,wav.pcm.size(),true); else engine.install(wav.pcm,wav.rate);
            liveSample=false;
            { std::lock_guard<std::mutex> names(nameMutex); sampleName=name+(wav.truncated ? " [TRUNCATED]" : ""); lastError.clear(); }
            refreshDisplay();
        } catch(const std::exception& e) {
            std::lock_guard<std::mutex> guard(nameMutex); lastError=e.what();
            WARN("OtisLab: %s",e.what());
        }
    }
    // Reading Spoon Bender captures from the module directly to the LEFT (UI thread only).
    // Same versioned interface OTIS itself provides to Vastish on its right.
    bool followFx=false; uint32_t fxRevision=0; bool fxSeen=false; const dspemu::SampleShareV1* fxShare=nullptr;
    static const dspemu::SampleShareV1* findFxShare(Module* left) {
        if(!left || !left->model || !left->model->plugin) return nullptr;
        if(left->model->plugin->slug!="SpoonBenderLab" || left->model->slug!="SpoonBender") return nullptr;
        void* p=left->rightExpander.producerMessage;
        return dspemu::validShare(p) ? static_cast<const dspemu::SampleShareV1*>(p) : nullptr;
    }
    // Copy the provider's sample and install it. Returns false if there is nothing
    // usable or it changed while copying (the caller retries on the next frame).
    bool importFrom(const dspemu::SampleShareV1* s) {
        if(!s) return false;
        uint32_t rev=s->revision(s->context);
        if(rev&1) return false;
        double rate=0; uint32_t got=0;
        size_t total=s->copy(s->context,nullptr,0,&rate,&got);
        if(total<2 || !std::isfinite(rate) || rate<1000 || rate>384000) return false;
        std::vector<int16_t> pcm(std::min(total,otis::memoryWords));
        if(s->copy(s->context,pcm.data(),pcm.size(),&rate,&got)!=total || got!=rev) return false;
        char name[256]={}; s->name(s->context,name,sizeof name);
        {
            std::lock_guard<std::mutex> guard(engineMutex);
            engine.install(pcm,rate); liveSample=false;
        }
        { std::lock_guard<std::mutex> names(nameMutex); sampleName=std::string(name)+(total>otis::memoryWords ? " [TRUNCATED]" : ""); lastError.clear(); }
        refreshDisplay();
        fxRevision=rev; fxSeen=true;
        return true;
    }
    // Called every UI frame with the current left neighbour's share (or null).
    void followFxShare(const dspemu::SampleShareV1* s) {
        if(s!=fxShare) { fxShare=s; fxSeen=false; }
        if(!followFx || !s) return;
        uint32_t rev=s->revision(s->context);
        if(rev==0 || (rev&1) || (fxSeen && rev==fxRevision)) return;  // 0: nothing captured yet
        importFrom(s);
    }
    void save(const std::string& path) {
        std::vector<int16_t> pcm,right; double rate=0;
        bool stereo=copyFinishedSample(pcm,right,rate);
        try { if(pcm.empty()) throw std::runtime_error("No finished sample to export"); otis::writeWav(path,pcm.data(),pcm.size(),rate,stereo ? right.data() : nullptr); }
        catch(const std::exception& e) { std::lock_guard<std::mutex> guard(nameMutex); lastError=e.what(); WARN("OtisLab: %s",e.what()); }
    }
    // Copy under the separate finished-sample lock. Publication is deferred if
    // needed; the audio thread keeps playing and never waits for this copy.
    bool copyFinishedSample(std::vector<int16_t>& pcm,std::vector<int16_t>& right,double& rate) {
        uint32_t seq=0;
        return engine.snapshotStereo(pcm,right,rate,seq);
    }
    // UI only. Capture a small selection under engineMutex, then copy PCM using
    // the separate finished-sample lock so file export never pins the audio thread.
    json_t* frameTableJson(otis::frame_table::Channel channel) {
        otis::Engine::Window window; uint32_t expected=0; std::string name;
        {
            std::lock_guard<std::mutex> guard(engineMutex);
            if(engine.controls.windowMode!=2) throw std::runtime_error("Select FRM mode before exporting a Lookup table");
            if(engine.recording || engine.capturePending) throw std::runtime_error("Finish recording before exporting a frame");
            window=engine.windowFor(0); expected=engine.sequence();
            std::lock_guard<std::mutex> names(nameMutex); name=liveSample.load() ? "LIVE RECORDING" : sampleName;
        }
        std::vector<int16_t> left,right; double rate=0; uint32_t revision=0;
        engine.snapshotStereo(left,right,rate,revision);
        if(revision!=expected || (revision&1)) throw std::runtime_error("Sample changed during frame export; retry when ready");
        auto values=otis::frame_table::resample(left,right,window.begin,window.end,channel);
        const char* side=channel==otis::frame_table::Channel::Left ? "left" : channel==otis::frame_table::Channel::Right ? "right" : "mix";
        json_t* root=json_object();
        json_object_set_new(root,"format",json_string("ByteBusLab.LookupTable"));
        json_object_set_new(root,"version",json_integer(1));
        json_object_set_new(root,"name",json_string(string::f("OTIS %s frame at %zu",side,window.begin).c_str()));
        json_object_set_new(root,"hash",json_string(otis::frame_table::fingerprint(values).c_str()));
        json_t* bytes=json_array(); for(uint8_t v:values) json_array_append_new(bytes,json_integer(v));
        json_object_set_new(root,"values",bytes);
        json_t* source=json_object();
        json_object_set_new(source,"kind",json_string("OTIS.frame"));
        json_object_set_new(source,"exporter",json_string("OtisLab.FrameTable/1"));
        json_object_set_new(source,"sample_name",json_string(name.c_str()));
        json_object_set_new(source,"sample_revision",json_integer(revision));
        json_object_set_new(source,"sample_rate",json_real(rate));
        json_object_set_new(source,"begin",json_integer(window.begin));
        json_object_set_new(source,"length",json_integer(window.end-window.begin));
        json_object_set_new(source,"channel",json_string(side));
        json_object_set_new(source,"resampling",json_string("periodic-linear-256"));
        json_object_set_new(source,"quantization",json_string("offset-binary-round-half-up-8-bit"));
        json_object_set_new(root,"source",source);
        return root;
    }
    void saveFrameTable(const std::string& path,otis::frame_table::Channel channel) {
        try {
            json_t* root=frameTableJson(channel);
            int result=json_dump_file(root,path.c_str(),JSON_INDENT(2) | JSON_SORT_KEYS); json_decref(root);
            if(result) throw std::runtime_error("Cannot write Lookup table JSON");
            std::lock_guard<std::mutex> names(nameMutex); lastError.clear();
        } catch(const std::exception& e) { std::lock_guard<std::mutex> names(nameMutex); lastError=e.what(); }
    }
    void process(const ProcessArgs& args) override {
        // Never wait on UI/file operations from the audio thread. Rare contention outputs silence.
        std::unique_lock<std::mutex> guard(engineMutex,std::try_to_lock);
        if(!guard.owns_lock()) { for(auto& output:outputs) output.setVoltage(0); return; }
        auto& c=engine.controls;
        c.engineMode=engineMode.load(std::memory_order_relaxed); c.filterMode=filterMode.load(std::memory_order_relaxed); c.gated=gated.load(std::memory_order_relaxed);
        for(int i=0;i<16;i++) {
            c.startv[i]=otis::clamp(params[START].getValue()+inputs[START_IN].getPolyVoltage(i)/10,0.f,1.f);
            c.endv[i]=otis::clamp(params[END].getValue()+inputs[END_IN].getPolyVoltage(i)/10,0.f,1.f);
            c.position[i]=otis::clamp(params[POSITION].getValue()+inputs[POSITION_IN].getPolyVoltage(i)/10,0.f,1.f);
            c.sizeOctaves[i]=otis::clamp(params[SIZE].getValue()+inputs[SIZE_IN].getPolyVoltage(i),1.f,16.f);
        }
        c.windowMode=int(params[WINDOW_MODE].getValue());
        auto frame=frameReader.process(inputs[FRAME_BUS_IN]);
        c.frameBus=frame.connected; c.frameWord=frame.word;
        c.frameCrossfade=smoothFrameBus.load(std::memory_order_relaxed);
        c.start=c.startv[0]; c.end=c.endv[0];
        // LAB: a tiny dead band keeps a re-centred knob on the authentic clock path.
        float clock=params[CLOCK].getValue()+inputs[CLOCK_IN].getVoltage();
        c.clockOctaves=std::abs(clock)<.005f ? 0.f : clock;
        c.interpBits=int(std::lround(otis::clamp(params[INTERP].getValue()+inputs[INTERP_IN].getVoltage()/1.1f,0.f,9.f)));
        c.varispeed=varispeed.load(std::memory_order_relaxed); c.recordSource=recordSource.load(std::memory_order_relaxed);
        // A take is stereo when AUDIO R is patched, or when it resamples a stereo sample's own output.
        bool audioR=inputs[AUDIO_R_IN].isConnected();
        c.recordStereo=audioR || (c.recordSource>0 && engine.sample.stereo);
        bool levelPatched=inputs[LEVEL_IN].isConnected();
        for(int i=0;i<16;i++) {
            c.k1v[i]=otis::clamp(params[K1].getValue()+inputs[K1_IN].getPolyVoltage(i)/10,0.f,1.f);
            c.k2v[i]=otis::clamp(params[K2].getValue()+inputs[K2_IN].getPolyVoltage(i)/10,0.f,1.f);
            c.level[i]=levelPatched ? otis::clamp(inputs[LEVEL_IN].getPolyVoltage(i)/10,0.f,1.f) : 1.f;
        }
        c.k1=c.k1v[0]; c.k2=c.k2v[0];
        c.linearLevel=linearLevel.load(std::memory_order_relaxed); c.declick=declick.load(std::memory_order_relaxed);
        c.outputGain=params[OUTPUT_GAIN].getValue(); c.recordGain=params[RECORD_GAIN].getValue();
        c.captureIndex=int(params[CAPTURE_RATE].getValue()); c.loop=int(params[LOOP].getValue()); c.reverse=params[REVERSE].getValue()>.5;
        if(c.reverse!=previousReverse) { for(auto& v:engine.voices) v.reverse=c.reverse; previousReverse=c.reverse; }
        c.channels=std::min(16,std::max(1,std::max(inputs[PITCH_IN].getChannels(),inputs[GATE_IN].getChannels())));
        for(int i=0;i<16;i++) c.pitch[i]=params[PITCH].getValue()/12+inputs[PITCH_IN].getPolyVoltage(i);
        recordGate.process(inputs[RECORD_IN].getVoltage(),.1f,2.f);
        bool requestedRecord=params[RECORD].getValue()>.5 || recordGate.isHigh();
        if(requestedRecord && !previousRecord) engine.startRecord();
        if(!requestedRecord && previousRecord) { if(engine.recording && engine.recordFrames>=2) liveSample=true; engine.stopRecord(); }
        previousRecord=requestedRecord;
        for(int i=0;i<16;i++) {
            bool wasHigh=gates[i].isHigh();
            if(gates[i].process(i<c.channels ? inputs[GATE_IN].getPolyVoltage(i) : 0,.1f,2.f)) engine.trigger(i);
            if(wasHigh && !gates[i].isHigh()) engine.release(i);
        }
        if(playButton.process(params[PLAY].getValue())) engine.trigger(0);
        float mutateVoltage=std::max(params[MUTATE].getValue()*10.f,inputs[MUTATE_IN].getVoltage());
        if(mutationGate.process(mutateVoltage,.1f,2.f)) {
            auto window=engine.windowFor(0);
            float amount=otis::clamp(params[MUTATION_AMOUNT].getValue()+inputs[MUTATION_AMOUNT_IN].getVoltage()/10,0.f,1.f);
            mutator.request(window.begin,window.end,params[SMOOTHNESS].getValue(),amount);
        }
        mutator.service();
        bool wasRecording=engine.recording;
        float in=inputs[AUDIO_IN].getVoltage()/5.f;
        float out=engine.process(in,audioR ? inputs[AUDIO_R_IN].getVoltage()/5.f : in)*5.f, outR=engine.rightOutput*5.f;
        if(wasRecording && !engine.recording && engine.frames>=2) liveSample=true;
        if(!std::isfinite(out)) out=0;
        if(!std::isfinite(outR)) outR=0;
        outputs[LEFT_OUT].setVoltage(out); outputs[RIGHT_OUT].setVoltage(outR);
        outputs[EOC_OUT].setVoltage(engine.endPulse>0 ? 10.f : 0.f);
        outputs[POSITION_OUT].setVoltage(engine.position()*10.f);
        lights[MUTATION_LIGHT].setBrightness(mutator.busy());
        lights[PLAY_LIGHT].setBrightness(engine.playing()); lights[RECORD_LIGHT].setBrightness(engine.recording); lights[FULL_LIGHT].setBrightness(engine.recordFull);
        refreshDisplay();
    }
    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        std::lock_guard<std::mutex> guard(engineMutex);
        if(engine.recording && engine.recordFrames>=2) liveSample=true;
        engine.setHostRate(e.sampleRate); refreshDisplay();
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        std::lock_guard<std::mutex> guard(engineMutex); mutator.cancel(); engine.stopAll(); engine.clearPcmFade();
        if(engine.recording && engine.recordFrames>=2) liveSample=true;
        engine.stopRecord();
        engineMode=0; filterMode=2; gated=false; linearLevel=false; declick=true; varispeed=false; recordSource=0; previousRecord=false; previousReverse=false;
        smoothFrameBus=true; frameReader.reset();
        for(auto& gate:gates) gate.reset();
        recordGate.reset(); mutationGate.reset(); playButton.reset(); refreshDisplay();
    }
    json_t* dataToJson() override {
        // Called on every Rack autosave. Reuse the encoded sample unless it changed, and never hold
        // the engine lock while copying, so saving does not drop audio.
        std::string name;
        { std::lock_guard<std::mutex> names(nameMutex); name=liveSample.load() ? "LIVE RECORDING" : sampleName; }
        uint32_t seq=engine.sequence();
        if(!pcmCacheValid || seq!=pcmCacheSequence || (seq&1)) {
            std::vector<int16_t> pcm,right; double rate=0; uint32_t got=0;
            bool stereo=engine.snapshotStereo(pcm,right,rate,got);
            pcmCacheSequence=got; pcmCacheValid=true;
            pcmCacheRate=rate; pcmCacheText=otis::encodePcm(pcm.data(),pcm.size());
            pcmCacheTextR=stereo ? otis::encodePcm(right.data(),right.size()) : std::string();
        }
        double rate=pcmCacheRate;
        json_t* root=json_object(); json_object_set_new(root,"schema",json_integer(1));
        json_object_set_new(root,"rate",json_real(rate)); json_object_set_new(root,"name",json_string(name.c_str()));
        json_object_set_new(root,"pcm16le",json_string(pcmCacheText.c_str()));
        // A stereo sample: pcm16le is the left channel (all that versions before 2.6 load), this the right.
        if(!pcmCacheTextR.empty()) json_object_set_new(root,"pcm16le_r",json_string(pcmCacheTextR.c_str()));
        json_object_set_new(root,"engineMode",json_integer(engineMode.load())); json_object_set_new(root,"filterMode",json_integer(filterMode.load())); json_object_set_new(root,"gated",json_boolean(gated.load()));
        json_object_set_new(root,"linearLevel",json_boolean(linearLevel.load())); json_object_set_new(root,"declick",json_boolean(declick.load()));
        json_object_set_new(root,"varispeed",json_boolean(varispeed.load())); json_object_set_new(root,"recordSource",json_integer(recordSource.load()));
        json_object_set_new(root,"followFx",json_boolean(followFx));
        json_object_set_new(root,"smoothFrameBus",json_boolean(smoothFrameBus.load()));
        return root;
    }
    void dataFromJson(json_t* root) override {
        mutator.cancel();
        try {
            engineMode=int(otis::clamp<json_int_t>(json_integer_value(json_object_get(root,"engineMode")),0,2));
            auto fm=json_object_get(root,"filterMode"); filterMode=fm ? int(otis::clamp<json_int_t>(json_integer_value(fm),0,3)) : 2;
            gated=json_is_true(json_object_get(root,"gated"));
            linearLevel=json_is_true(json_object_get(root,"linearLevel"));
            varispeed=json_is_true(json_object_get(root,"varispeed"));
            auto rs=json_object_get(root,"recordSource"); recordSource=rs ? int(otis::clamp<json_int_t>(json_integer_value(rs),0,2)) : 0;
            followFx=json_is_true(json_object_get(root,"followFx")); fxSeen=false;
            auto dc=json_object_get(root,"declick"); declick=dc ? json_is_true(dc) : true;
            auto smooth=json_object_get(root,"smoothFrameBus"); smoothFrameBus=smooth ? json_is_true(smooth) : true;
            frameReader.reset();
            auto text=json_string_value(json_object_get(root,"pcm16le"));
            if(text && *text) {
                auto pcm=otis::decodePcm(text); double rate=json_number_value(json_object_get(root,"rate"));
                if(!std::isfinite(rate)||rate<1000||rate>384000) throw std::runtime_error("Invalid saved sample rate");
                auto textR=json_string_value(json_object_get(root,"pcm16le_r"));
                std::vector<int16_t> image;
                if(textR && *textR) {
                    auto right=otis::decodePcm(textR);
                    if(right.size()!=pcm.size() || pcm.size()>otis::stereoFrames) throw std::runtime_error("Invalid saved stereo sample");
                    image=otis::stereoImage(pcm.data(),right.data(),pcm.size());
                }
                std::lock_guard<std::mutex> guard(engineMutex);
                if(!image.empty()) engine.install(image,rate,pcm.size(),true); else engine.install(pcm,rate);
                auto name=json_string_value(json_object_get(root,"name"));
                { std::lock_guard<std::mutex> names(nameMutex); sampleName=name ? name : "RESTORED SAMPLE"; liveSample=sampleName=="LIVE RECORDING"; }
                refreshDisplay();
            }
        } catch(const std::exception& e) { std::lock_guard<std::mutex> names(nameMutex); lastError=e.what(); WARN("OtisLab: %s",e.what()); }
    }
};

struct SampleDisplay : widget::Widget {
    OtisSampler* module=nullptr;
    void draw(const DrawArgs& args) override {
        // Rack 2 ships ShareTechMono, not DejaVu Sans Mono; a failed load returns null.
        auto font=APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if(!font) font=APP->window->uiFont;
        if(!font) return;
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,10);
        nvgTextAlign(args.vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
        nvgFillColor(args.vg,nvgRGB(126,225,202));
        std::string name="OTIS / ES5505 PLAYBACK";
        if(module) {
            std::lock_guard<std::mutex> guard(module->nameMutex);
            name=module->displayedRecording.load() || module->liveSample.load() ? "LIVE RECORDING" : module->sampleName;
            if(!module->lastError.empty()) name="ERROR: "+module->lastError;
        }
        nvgText(args.vg,0,0,name.substr(0,44).c_str(),nullptr);
        std::string status="RIGHT CLICK: LOAD WAV / DEMO";
        if(module && module->displayedFrames.load()>0) {
            double rate=module->displayedRate.load(); size_t frames=module->displayedFrames.load();
            status=string::f("%s%s %.2fs  %.2fk  / CHIP %.2fk",module->displayedRecording.load() ? "REC" : "PCM",module->displayedStereo.load() ? " ST" : "",frames/rate,rate/1000,otis::chipRates[module->engineMode.load()]/1000);
        }
        nvgText(args.vg,0,19,status.c_str(),nullptr);
        if(module) {
            nvgFontSize(args.vg,9);
            const char* message=module->mutator.busy() ? "REWORK: BUSY" : "REWORK: READY";
            if(!module->mutator.busy()) switch(module->mutator.lastOutcome()) {
                case otis::LoopMutator::Applied: message="REWORK: APPLIED"; break;
                case otis::LoopMutator::SourceChanged: message="REWORK: SOURCE CHANGED"; break;
                case otis::LoopMutator::NoChange: message="REWORK: UNCHANGED"; break;
                case otis::LoopMutator::Failed: message="REWORK: FAILED"; break;
                case otis::LoopMutator::Unavailable: message="REWORK: NEED FINISHED SAMPLE"; break;
                case otis::LoopMutator::Canceled: message="REWORK: CANCELED"; break;
            }
            nvgText(args.vg,319,0,message,nullptr);
        }
    }
};
struct OtisSamplerWidget : ModuleWidget {
    OtisSamplerWidget(OtisSampler* module) {
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/OtisSampler.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(box.size.x-15,0)));
        addChild(createWidget<ScrewSilver>(Vec(0,365))); addChild(createWidget<ScrewSilver>(Vec(box.size.x-15,365)));
        auto display=new SampleDisplay; display->module=module; display->box.pos=Vec(16,54); display->box.size=Vec(508,40); addChild(display);
        const int first[4]={OtisSampler::PITCH,OtisSampler::START,OtisSampler::END,OtisSampler::K1};
        const int second[4]={OtisSampler::CAPTURE_RATE,OtisSampler::RECORD_GAIN,OtisSampler::OUTPUT_GAIN,OtisSampler::K2};
        for(int i=0;i<4;i++) {
            float x=45+80*i;
            addParam(createParamCentered<RoundBlackKnob>(Vec(x,125),module,first[i]));
            auto knob=createParamCentered<RoundBlackKnob>(Vec(x,187),module,second[i]); addParam(knob);
            if(module && i==0) module->getParamQuantity(second[i])->snapEnabled=true;
        }
        // LAB column (x = 360): CLOCK and INTERP knobs, their CV inputs below.
        addParam(createParamCentered<RoundBlackKnob>(Vec(360,125),module,OtisSampler::CLOCK));
        auto interp=createParamCentered<RoundBlackKnob>(Vec(360,187),module,OtisSampler::INTERP); addParam(interp);
        if(module) module->getParamQuantity(OtisSampler::INTERP)->snapEnabled=true;
        addInput(createInputCentered<PJ301MPort>(Vec(360,292),module,OtisSampler::CLOCK_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(360,341),module,OtisSampler::INTERP_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(360,246),module,OtisSampler::FRAME_BUS_IN));
        addParam(createParamCentered<RoundBlackKnob>(Vec(435,125),module,OtisSampler::POSITION));
        addParam(createParamCentered<RoundBlackKnob>(Vec(435,187),module,OtisSampler::SIZE));
        addParam(createParamCentered<CKSSThree>(Vec(435,246),module,OtisSampler::WINDOW_MODE));
        addInput(createInputCentered<PJ301MPort>(Vec(435,292),module,OtisSampler::POSITION_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(435,341),module,OtisSampler::SIZE_IN));
        addParam(createParamCentered<RoundBlackKnob>(Vec(510,125),module,OtisSampler::SMOOTHNESS));
        addParam(createParamCentered<RoundBlackKnob>(Vec(510,187),module,OtisSampler::MUTATION_AMOUNT));
        addParam(createParamCentered<LEDButton>(Vec(510,246),module,OtisSampler::MUTATE));
        addChild(createLightCentered<SmallLight<YellowLight>>(Vec(529,246),module,OtisSampler::MUTATION_LIGHT));
        addInput(createInputCentered<PJ301MPort>(Vec(510,292),module,OtisSampler::MUTATE_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(510,341),module,OtisSampler::MUTATION_AMOUNT_IN));
        addParam(createParamCentered<LEDButton>(Vec(45,246),module,OtisSampler::PLAY));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(45,246),module,OtisSampler::PLAY_LIGHT));
        addParam(createParamCentered<CKSS>(Vec(125,246),module,OtisSampler::RECORD));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(143,246),module,OtisSampler::RECORD_LIGHT));
        addParam(createParamCentered<CKSSThree>(Vec(205,246),module,OtisSampler::LOOP));
        addParam(createParamCentered<CKSS>(Vec(285,246),module,OtisSampler::REVERSE));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(302,246),module,OtisSampler::FULL_LIGHT));
        // Record row: AUDIO L, AUDIO R, V/OCT, GATE, REC, START, END, 45 px apart.
        const int recordRow[7]={OtisSampler::AUDIO_IN,OtisSampler::AUDIO_R_IN,OtisSampler::PITCH_IN,OtisSampler::GATE_IN,OtisSampler::RECORD_IN,OtisSampler::START_IN,OtisSampler::END_IN};
        for(int i=0;i<7;i++) addInput(createInputCentered<PJ301MPort>(Vec(30+45*i,292),module,recordRow[i]));
        // Bottom row: K1, K2, VCA inputs then L, R, EOC, POS outputs, 46 px apart.
        addInput(createInputCentered<PJ301MPort>(Vec(27,341),module,OtisSampler::K1_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(73,341),module,OtisSampler::K2_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(119,341),module,OtisSampler::LEVEL_IN));
        for(int i=0;i<4;i++) addOutput(createOutputCentered<PJ301MPort>(Vec(165+46*i,341),module,i));
    }
    // Rack writes leftExpander.moduleId on this (UI) thread and the pointer on the
    // engine thread, so resolve the ID here; modules are only deleted from this thread.
    static Module* leftNeighbour(Module* m) {
        int64_t id=m ? m->leftExpander.moduleId : -1;
        return id>=0 ? APP->engine->getModule(id) : nullptr;
    }
    void step() override {
        if(auto* m=getModule<OtisSampler>()) m->followFxShare(OtisSampler::findFxShare(leftNeighbour(m)));
        ModuleWidget::step();
    }
    void appendContextMenu(Menu* menu) override {
        auto* m=getModule<OtisSampler>(); if(!m) return;
        menu->addChild(new MenuSeparator);
        bool fxPresent=OtisSampler::findFxShare(leftNeighbour(m))!=nullptr;
        // Resolve the neighbour again at click time; it may have moved or been deleted.
        menu->addChild(createMenuItem("Import capture from Spoon Bender on the left","",[m]{ m->importFrom(OtisSampler::findFxShare(leftNeighbour(m))); },!fxPresent));
        menu->addChild(createBoolMenuItem("Follow Spoon Bender captures","",[m]{return m->followFx;},[m](bool on){ m->followFx=on; m->fxSeen=false; }));
        if(!fxPresent) menu->addChild(createMenuLabel("Place Spoon Bender directly left of OTIS to import its captures"));
        menu->addChild(createMenuItem("Load WAV…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav"); char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if(path) { m->load(path); std::free(path); }
        }));
        menu->addChild(createMenuItem("Load WAV as mono…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav"); char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if(path) { m->load(path,true); std::free(path); }
        }));
        menu->addChild(createMenuItem("Load included demo","",[m]() { m->load(asset::plugin(pluginInstance,"examples/bright-demo.wav")); }));
        menu->addChild(createMenuItem("Export finished sample as WAV…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav"); char* path=osdialog_file(OSDIALOG_SAVE,nullptr,"otis-sample.wav",filters); osdialog_filters_free(filters);
            if(path) { m->save(path); std::free(path); }
        }));
        menu->addChild(createSubmenuItem("Export selected frame to Lookup","",[m](Menu* child) {
            const char* labels[]={"Left channel…","Right channel…","Mono mix…"};
            for(int i=0;i<3;++i) child->addChild(createMenuItem(labels[i],"",[m,i] {
                auto* filters=osdialog_filters_parse("Lookup table:json");
                char* path=osdialog_file(OSDIALOG_SAVE,nullptr,"otis-frame.lookup.json",filters); osdialog_filters_free(filters);
                if(path) { m->saveFrameTable(path,otis::frame_table::Channel(i)); std::free(path); }
            },i==1 && !m->displayedStereo.load()));
        }));
        menu->addChild(createMenuItem("Stop all voices","",[m]() { std::lock_guard<std::mutex> g(m->engineMutex); m->engine.stopAll(); }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem("Gate release stops playback (default: one-shot)","",[m](){return m->gated.load();},[m](bool b){m->gated=b;}));
        menu->addChild(createBoolMenuItem("Declick release/retrigger (1.5 ms fades)","",[m](){return m->declick.load();},[m](bool b){m->declick=b;}));
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("LAB (not EPS behaviour)"));
        menu->addChild(createBoolMenuItem("Smooth FRAME BUS changes (2 ms)","",[m]{return m->smoothFrameBus.load();},[m](bool on){m->smoothFrameBus=on;}));
        menu->addChild(createSubmenuItem("CLOCK knob moves","",[m](Menu* child) {
            child->addChild(createCheckMenuItem("Resolution only (pitch held)","",[m](){return !m->varispeed.load();},[m](){m->varispeed=false;}));
            child->addChild(createCheckMenuItem("Varispeed (pitch follows clock)","",[m](){return m->varispeed.load();},[m](){m->varispeed=true;}));
        }));
        menu->addChild(createSubmenuItem("Record source","",[m](Menu* child) {
            const char* names[3]={"AUDIO input","Own output (resample)","AUDIO + own output"};
            for(int i=0;i<3;i++) child->addChild(createCheckMenuItem(names[i],"",[m,i](){return m->recordSource.load()==i;},[m,i](){m->recordSource=i;}));
        }));
        menu->addChild(createSubmenuItem("VCA CV response","",[m](Menu* child) {
            child->addChild(createCheckMenuItem("Chip log (10 V = unity, ~6 dB per 0.52 V)","",[m](){return !m->linearLevel.load();},[m](){m->linearLevel=false;}));
            child->addChild(createCheckMenuItem("Linear amplitude (via chip volume steps)","",[m](){return m->linearLevel.load();},[m](){m->linearLevel=true;}));
        }));
        menu->addChild(createSubmenuItem("Chip output clock","",[m](Menu* child) {
            const char* names[3]={"29.76 kHz / up to 16 Rack voices (8 stereo)","44.64 kHz / up to 14 voices (7 stereo)","78.125 kHz / up to 8 voices (4 stereo)"};
            for(int i=0;i<3;i++) child->addChild(createCheckMenuItem(names[i],"",[m,i](){return m->engineMode.load()==i;},[m,i](){m->engineMode=i;}));
        }));
        menu->addChild(createSubmenuItem("Chip filter topology","",[m](Menu* child) {
            const char* names[4]={"2 low-pass + 2 high-pass","3 low-pass + 1 high-pass","4 low-pass (K1 / K1 / K2 / K2)","4 low-pass (K1 / K1 / K1 / K2)"};
            for(int i=0;i<4;i++) child->addChild(createCheckMenuItem(names[i],"",[m,i](){return m->filterMode.load()==i;},[m,i](){m->filterMode=i;}));
        }));
        menu->addChild(createMenuItem("Cancel pending loop rework","",[m](){m->mutator.cancel();}));
        menu->addChild(createMenuLabel("WINDOW: legacy / fixed-size scan / frame steps."));
        menu->addChild(createMenuLabel("REWORK replaces voice 1's selected window; repeated triggers while busy are ignored."));
        menu->addChild(createMenuLabel("REWORK is an original experiment, not ASR Synthesized Loop emulation."));
        menu->addChild(createMenuLabel("Samples are embedded in saved patches."));
        menu->addChild(createMenuLabel("Stereo samples use two chip voices each: half the voices and memory."));
        menu->addChild(createMenuLabel("Prototype • original ESP effects not included."));
    }
};
Model* modelOtisSampler=createModel<OtisSampler,OtisSamplerWidget>("OtisSampler");
