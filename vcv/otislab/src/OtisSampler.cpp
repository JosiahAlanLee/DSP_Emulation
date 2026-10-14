#include "plugin.hpp"
#include "engine/OtisEngine.hpp"
#include "engine/Wav.hpp"
#include <osdialog.h>
#include <mutex>
#include <atomic>

struct OtisSampler : Module {
    enum Params { PITCH, START, END, K1, K2, RECORD_GAIN, OUTPUT_GAIN, CAPTURE_RATE, PLAY, RECORD, LOOP, REVERSE, NUM_PARAMS };
    // LEVEL_IN is appended so existing patches keep their cable ids.
    enum Inputs { AUDIO_IN, PITCH_IN, GATE_IN, RECORD_IN, START_IN, END_IN, K1_IN, K2_IN, LEVEL_IN, NUM_INPUTS };
    enum Outputs { LEFT_OUT, RIGHT_OUT, EOC_OUT, POSITION_OUT, NUM_OUTPUTS };
    enum Lights { PLAY_LIGHT, RECORD_LIGHT, FULL_LIGHT, NUM_LIGHTS };
    otis::Engine engine;
    std::mutex engineMutex;
    std::mutex nameMutex;
    std::array<dsp::SchmittTrigger,16> gates;
    dsp::SchmittTrigger playButton;
    dsp::SchmittTrigger recordGate;
    bool previousRecord=false, previousReverse=false;
    std::atomic<bool> gated{false};
    std::atomic<bool> linearLevel{false}, declick{true};
    // UI-thread cache of the encoded sample, keyed by the engine's sample sequence counter.
    bool pcmCacheValid=false; uint32_t pcmCacheSequence=0; double pcmCacheRate=0; std::string pcmCacheText;
    std::atomic<int> engineMode{0},filterMode{2};
    std::atomic<size_t> displayedFrames{0};
    std::atomic<double> displayedRate{otis::captureRate(5)};
    std::atomic<bool> displayedRecording{false};
    std::atomic<bool> liveSample{false};
    std::string sampleName="NO SAMPLE - RIGHT CLICK TO LOAD";
    std::string lastError;
    OtisSampler() {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        configParam(PITCH,-48,48,0,"Pitch offset"," semitones");
        configParam(START,0,1,0,"Start / loop start","%",0,100);
        configParam(END,0,1,1,"End / loop end","%",0,100);
        configParam(K1,0,1,1,"Filter K1 coefficient","%",0,100);
        configParam(K2,0,1,1,"Filter K2 coefficient","%",0,100);
        configParam(RECORD_GAIN,0,4,1,"Recording gain","x");
        configParam(OUTPUT_GAIN,0,2,1,"Output gain","x");
        configSwitch(CAPTURE_RATE,0,6,5,"Capture rate",{"11.16 kHz","12.76 kHz","14.88 kHz","17.86 kHz","22.32 kHz","29.76 kHz","44.64 kHz"});
        configButton(PLAY,"Play / retrigger voice 1");
        configSwitch(RECORD,0,1,0,"Record",{"Off","On"});
        configSwitch(LOOP,0,2,0,"Loop",{"Off","Forward","Ping-pong"});
        configSwitch(REVERSE,0,1,0,"Reverse",{"Off","On"});
        configInput(AUDIO_IN,"Recording audio (±5 V = full scale)");
        configInput(PITCH_IN,"Pitch (1 V/oct; 0 V = original pitch)");
        configInput(GATE_IN,"Play gate / trigger (polyphonic)");
        configInput(RECORD_IN,"Record gate (high = record)");
        configInput(START_IN,"Start CV (0–10 V adds 0–100%)");
        configInput(END_IN,"End CV (0–10 V adds 0–100%)");
        configInput(K1_IN,"K1 CV, polyphonic per voice (0–10 V adds 0–100%)");
        configInput(K2_IN,"K2 CV, polyphonic per voice (0–10 V adds 0–100%)");
        configInput(LEVEL_IN,"VCA: per-voice chip volume, polyphonic (0–10 V; 10 V = unity; unpatched = unity)");
        configOutput(LEFT_OUT,"Audio L (mono)"); configOutput(RIGHT_OUT,"Audio R (same mono signal)");
        configOutput(EOC_OUT,"End / loop crossing (1 ms, 10 V)");
        configOutput(POSITION_OUT,"First active voice position (0–10 V)");
    }
    void refreshDisplay() {
        displayedFrames.store(engine.recording ? engine.recordFrames : engine.frames,std::memory_order_relaxed);
        displayedRate.store(engine.recording ? engine.recordBuffer.rate : engine.sample.rate,std::memory_order_relaxed);
        displayedRecording.store(engine.recording,std::memory_order_relaxed);
    }
    void load(const std::string& path) {
        try {
            auto wav=otis::loadWav(path); // file I/O and allocations run on the UI thread
            size_t slash=path.find_last_of("/\\"); std::string name=path.substr(slash==std::string::npos ? 0 : slash+1);
            std::lock_guard<std::mutex> guard(engineMutex);
            engine.install(wav.pcm,wav.rate); liveSample=false;
            { std::lock_guard<std::mutex> names(nameMutex); sampleName=name+(wav.truncated ? " [TRUNCATED]" : ""); lastError.clear(); }
            refreshDisplay();
        } catch(const std::exception& e) {
            std::lock_guard<std::mutex> guard(nameMutex); lastError=e.what();
            WARN("OtisLab: %s",e.what());
        }
    }
    void save(const std::string& path) {
        std::vector<int16_t> pcm; double rate=0;
        copyFinishedSample(pcm,rate);
        try { if(pcm.empty()) throw std::runtime_error("No finished sample to export"); otis::writeWav(path,pcm.data(),pcm.size(),rate); }
        catch(const std::exception& e) { std::lock_guard<std::mutex> guard(nameMutex); lastError=e.what(); WARN("OtisLab: %s",e.what()); }
    }
    // Copy the finished sample without blocking the audio thread; falls back to the lock only if
    // recordings keep completing during the copy.
    void copyFinishedSample(std::vector<int16_t>& pcm,double& rate) {
        uint32_t seq=0;
        if(engine.snapshot(pcm,rate,seq)) return;
        std::lock_guard<std::mutex> guard(engineMutex);
        pcm.assign(engine.sample.pcm.begin(),engine.sample.pcm.begin()+engine.frames); rate=engine.sample.rate;
    }
    void process(const ProcessArgs& args) override {
        // Never wait on UI/file operations from the audio thread. Rare contention outputs silence.
        std::unique_lock<std::mutex> guard(engineMutex,std::try_to_lock);
        if(!guard.owns_lock()) { for(auto& output:outputs) output.setVoltage(0); return; }
        auto& c=engine.controls;
        c.engineMode=engineMode.load(std::memory_order_relaxed); c.filterMode=filterMode.load(std::memory_order_relaxed); c.gated=gated.load(std::memory_order_relaxed);
        c.start=otis::clamp(params[START].getValue()+inputs[START_IN].getVoltage()/10,0.f,1.f);
        c.end=otis::clamp(params[END].getValue()+inputs[END_IN].getVoltage()/10,0.f,1.f);
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
        bool wasRecording=engine.recording;
        float out=engine.process(inputs[AUDIO_IN].getVoltage()/5.f)*5.f;
        if(wasRecording && !engine.recording && engine.frames>=2) liveSample=true;
        if(!std::isfinite(out)) out=0;
        outputs[LEFT_OUT].setVoltage(out); outputs[RIGHT_OUT].setVoltage(out);
        outputs[EOC_OUT].setVoltage(engine.endPulse>0 ? 10.f : 0.f);
        outputs[POSITION_OUT].setVoltage(engine.position()*10.f);
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
        std::lock_guard<std::mutex> guard(engineMutex); engine.stopAll();
        if(engine.recording && engine.recordFrames>=2) liveSample=true;
        engine.stopRecord();
        engineMode=0; filterMode=2; gated=false; linearLevel=false; declick=true; previousRecord=false; previousReverse=false;
        for(auto& gate:gates) gate.reset();
        recordGate.reset(); playButton.reset(); refreshDisplay();
    }
    json_t* dataToJson() override {
        // Called on every Rack autosave. Reuse the encoded sample unless it changed, and never hold
        // the engine lock while copying, so saving does not drop audio.
        std::string name;
        { std::lock_guard<std::mutex> names(nameMutex); name=liveSample.load() ? "LIVE RECORDING" : sampleName; }
        uint32_t seq=engine.sequence();
        if(!pcmCacheValid || seq!=pcmCacheSequence || (seq&1)) {
            std::vector<int16_t> pcm; double rate=0; uint32_t got=0;
            if(engine.snapshot(pcm,rate,got)) { pcmCacheSequence=got; pcmCacheValid=true; }
            else { copyFinishedSample(pcm,rate); pcmCacheValid=false; }
            pcmCacheRate=rate; pcmCacheText=otis::encodePcm(pcm.data(),pcm.size());
        }
        double rate=pcmCacheRate;
        json_t* root=json_object(); json_object_set_new(root,"schema",json_integer(1));
        json_object_set_new(root,"rate",json_real(rate)); json_object_set_new(root,"name",json_string(name.c_str()));
        json_object_set_new(root,"pcm16le",json_string(pcmCacheText.c_str()));
        json_object_set_new(root,"engineMode",json_integer(engineMode.load())); json_object_set_new(root,"filterMode",json_integer(filterMode.load())); json_object_set_new(root,"gated",json_boolean(gated.load()));
        json_object_set_new(root,"linearLevel",json_boolean(linearLevel.load())); json_object_set_new(root,"declick",json_boolean(declick.load()));
        return root;
    }
    void dataFromJson(json_t* root) override {
        try {
            engineMode=int(otis::clamp<json_int_t>(json_integer_value(json_object_get(root,"engineMode")),0,2));
            auto fm=json_object_get(root,"filterMode"); filterMode=fm ? int(otis::clamp<json_int_t>(json_integer_value(fm),0,3)) : 2;
            gated=json_is_true(json_object_get(root,"gated"));
            linearLevel=json_is_true(json_object_get(root,"linearLevel"));
            auto dc=json_object_get(root,"declick"); declick=dc ? json_is_true(dc) : true;
            auto text=json_string_value(json_object_get(root,"pcm16le"));
            if(text && *text) {
                auto pcm=otis::decodePcm(text); double rate=json_number_value(json_object_get(root,"rate"));
                if(!std::isfinite(rate)||rate<1000||rate>384000) throw std::runtime_error("Invalid saved sample rate");
                std::lock_guard<std::mutex> guard(engineMutex); engine.install(pcm,rate);
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
        auto font=APP->window->loadFont(asset::system("res/fonts/DejaVuSansMono.ttf"));
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
            status=string::f("%s %.2fs  %.2fk  / CHIP %.2fk",module->displayedRecording.load() ? "REC" : "PCM",frames/rate,rate/1000,otis::chipRates[module->engineMode.load()]/1000);
        }
        nvgText(args.vg,0,19,status.c_str(),nullptr);
    }
};
struct OtisSamplerWidget : ModuleWidget {
    OtisSamplerWidget(OtisSampler* module) {
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/OtisSampler.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(box.size.x-15,0)));
        addChild(createWidget<ScrewSilver>(Vec(0,365))); addChild(createWidget<ScrewSilver>(Vec(box.size.x-15,365)));
        auto display=new SampleDisplay; display->module=module; display->box.pos=Vec(16,54); display->box.size=Vec(300,40); addChild(display);
        const int first[4]={OtisSampler::PITCH,OtisSampler::START,OtisSampler::END,OtisSampler::K1};
        const int second[4]={OtisSampler::CAPTURE_RATE,OtisSampler::RECORD_GAIN,OtisSampler::OUTPUT_GAIN,OtisSampler::K2};
        for(int i=0;i<4;i++) {
            float x=45+80*i;
            addParam(createParamCentered<RoundBlackKnob>(Vec(x,125),module,first[i]));
            auto knob=createParamCentered<RoundBlackKnob>(Vec(x,187),module,second[i]); addParam(knob);
            if(module && i==0) module->getParamQuantity(second[i])->snapEnabled=true;
        }
        addParam(createParamCentered<LEDButton>(Vec(45,242),module,OtisSampler::PLAY));
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(45,242),module,OtisSampler::PLAY_LIGHT));
        addParam(createParamCentered<CKSS>(Vec(125,242),module,OtisSampler::RECORD));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(143,242),module,OtisSampler::RECORD_LIGHT));
        addParam(createParamCentered<CKSSThree>(Vec(205,242),module,OtisSampler::LOOP));
        addParam(createParamCentered<CKSS>(Vec(285,242),module,OtisSampler::REVERSE));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(302,242),module,OtisSampler::FULL_LIGHT));
        for(int i=0;i<6;i++) addInput(createInputCentered<PJ301MPort>(Vec(30+54*i,292),module,i));
        // Bottom row: K1, K2, VCA inputs then L, R, EOC, POS outputs, 46 px apart.
        addInput(createInputCentered<PJ301MPort>(Vec(27,341),module,OtisSampler::K1_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(73,341),module,OtisSampler::K2_IN));
        addInput(createInputCentered<PJ301MPort>(Vec(119,341),module,OtisSampler::LEVEL_IN));
        for(int i=0;i<4;i++) addOutput(createOutputCentered<PJ301MPort>(Vec(165+46*i,341),module,i));
    }
    void appendContextMenu(Menu* menu) override {
        auto* m=getModule<OtisSampler>(); if(!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load WAV…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav"); char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if(path) { m->load(path); std::free(path); }
        }));
        menu->addChild(createMenuItem("Load included demo","",[m]() { m->load(asset::plugin(pluginInstance,"examples/bright-demo.wav")); }));
        menu->addChild(createMenuItem("Export finished sample as WAV…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav"); char* path=osdialog_file(OSDIALOG_SAVE,nullptr,"otis-sample.wav",filters); osdialog_filters_free(filters);
            if(path) { m->save(path); std::free(path); }
        }));
        menu->addChild(createMenuItem("Stop all voices","",[m]() { std::lock_guard<std::mutex> g(m->engineMutex); m->engine.stopAll(); }));
        menu->addChild(new MenuSeparator);
        menu->addChild(createBoolMenuItem("Gate release stops playback (default: one-shot)","",[m](){return m->gated.load();},[m](bool b){m->gated=b;}));
        menu->addChild(createBoolMenuItem("Declick release/retrigger (1.5 ms fades)","",[m](){return m->declick.load();},[m](bool b){m->declick=b;}));
        menu->addChild(createSubmenuItem("VCA CV response","",[m](Menu* child) {
            child->addChild(createCheckMenuItem("Chip log (10 V = unity, ~6 dB per 0.52 V)","",[m](){return !m->linearLevel.load();},[m](){m->linearLevel=false;}));
            child->addChild(createCheckMenuItem("Linear amplitude (via chip volume steps)","",[m](){return m->linearLevel.load();},[m](){m->linearLevel=true;}));
        }));
        menu->addChild(createSubmenuItem("Chip output clock","",[m](Menu* child) {
            const char* names[3]={"29.76 kHz / up to 16 Rack voices","44.64 kHz / up to 14 voices","78.125 kHz / up to 8 voices"};
            for(int i=0;i<3;i++) child->addChild(createCheckMenuItem(names[i],"",[m,i](){return m->engineMode.load()==i;},[m,i](){m->engineMode=i;}));
        }));
        menu->addChild(createSubmenuItem("Chip filter topology","",[m](Menu* child) {
            const char* names[4]={"2 low-pass + 2 high-pass","3 low-pass + 1 high-pass","4 low-pass (K1 / K1 / K2 / K2)","4 low-pass (K1 / K1 / K1 / K2)"};
            for(int i=0;i<4;i++) child->addChild(createCheckMenuItem(names[i],"",[m,i](){return m->filterMode.load()==i;},[m,i](){m->filterMode=i;}));
        }));
        menu->addChild(createMenuLabel("Samples are embedded in saved patches."));
        menu->addChild(createMenuLabel("Prototype • original ESP effects not included."));
    }
};
Model* modelOtisSampler=createModel<OtisSampler,OtisSamplerWidget>("OtisSampler");
