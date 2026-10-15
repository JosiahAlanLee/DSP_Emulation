#include "plugin.hpp"
#include "engine/Engine.hpp"
#include "../../shared/SampleFile.hpp"
#include "../../shared/SampleShare.hpp"
#include <osdialog.h>
#include <atomic>
#include <cctype>
// A loaded sample. Immutable once published; owned by the handoff below.
struct SampleSlot {
    dspemu::MonoSample data; vastish::SampleView view;
    uint64_t generation=0; // set on publish; identifies the data for the save cache
    explicit SampleSlot(dspemu::MonoSample s={}) : data(std::move(s)) {
        view.pcm=data.pcm.empty() ? nullptr : data.pcm.data(); view.frames=data.pcm.size(); view.rate=data.rate;
    }
};
using namespace vastish;
struct Vastish : Module {
    // AUX and FUN3/4 are appended so existing patches keep their cable ids.
    enum { PITCH_IN, GATE_IN, VEL_IN, MOD_IN, AUX1_IN, AUX2_IN, AUX3_IN, AUX4_IN, INPUTS };
    enum { LEFT_OUT, RIGHT_OUT, AMP_OUT, ENV2_OUT, ENV3_OUT, LFO1_OUT, LFO2_OUT, FUN1_OUT, FUN2_OUT, FUN3_OUT, FUN4_OUT, OUTPUTS };
    enum { GATE_LIGHT, FAULT_LIGHT, LIGHTS };
    vastish::Engine engine;
    dsp::SchmittTrigger triggers[8];
    std::array<VoiceInput,8> in{};
    std::atomic<int> requestedProgram{-1}, displayedProgram{1};
    std::atomic<bool> requestedPanic{false};
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
    uint64_t publishedGeneration=0, encodedGeneration=~uint64_t(0); std::string encodedPcm; // no published slot has ~0
    void collectSamples() { if(auto* r=retiredSample.exchange(nullptr,std::memory_order_acq_rel)) delete r; }
    void publishSample(SampleSlot* s) {
        collectSamples(); s->generation=++publishedGeneration; uiSample=s;
        if(auto* stale=pendingSample.exchange(s,std::memory_order_acq_rel)) delete stale; // never reached audio
    }
    void loadSampleFile(const std::string& path) {
        try { publishSample(new SampleSlot(dspemu::loadWavMono(path,MAX_SAMPLE_FRAMES))); sampleError.clear(); }
        catch(const std::exception& e) { sampleError=e.what(); WARN("Vastish: %s",e.what()); }
    }
    // OTIS link (UI thread only): with followOtis on and Vastish directly to the
    // right of an OTIS Sampler, the SAMPLE wave tracks OTIS's finished sample,
    // including new recordings and REWORK generations.
    bool followOtis=false;
    enum LinkState { LINK_OFF, LINK_NOT_FOUND, LINK_CONNECTED } linkState=LINK_OFF;
    const dspemu::SampleShareV1* followedShare=nullptr; uint32_t followedRevision=0; bool followedAny=false;
    void followShare(const dspemu::SampleShareV1* share) {
        if(!followOtis) { linkState=LINK_OFF; return; }
        if(!share) { linkState=LINK_NOT_FOUND; followedShare=nullptr; return; }
        linkState=LINK_CONNECTED;
        if(share!=followedShare) { followedShare=share; followedAny=false; }
        uint32_t rev=share->revision(share->context);
        if((rev&1) || (followedAny && rev==followedRevision)) return; // mid-change, or nothing new
        double rate=0; uint32_t got=0;
        size_t total=share->copy(share->context,nullptr,0,&rate,&got);
        dspemu::MonoSample m;
        if(total>=2) {
            m.pcm.resize(std::min(total,MAX_SAMPLE_FRAMES));
            size_t again=share->copy(share->context,m.pcm.data(),m.pcm.size(),&rate,&got);
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
    void setFollowOtis(bool on) { followOtis=on; followedShare=nullptr; followedAny=false; if(!on) linkState=LINK_OFF; }
    // Compact form for the panel selector: no "OTIS: " prefix or ".wav" extension;
    // a left arrow marks a sample that comes from OTIS.
    std::string sampleShortLabel() const {
        if(!uiSample || !uiSample->view.valid()) return "NO SAMPLE";
        std::string n=uiSample->data.name; bool fromOtis=false;
        if(n.rfind("OTIS: ",0)==0) { n=n.substr(6); fromOtis=true; }
        if(n.size()>4) { std::string ext=n.substr(n.size()-4); for(auto& c:ext) c=char(std::tolower(uint8_t(c))); if(ext==".wav") n.resize(n.size()-4); }
        if(n.empty()) n="SAMPLE";
        return fromOtis ? "\xe2\x97\x80 "+n : n;
    }
    std::string sampleLabel() const {
        if(!uiSample || !uiSample->view.valid()) return "NO SAMPLE";
        return uiSample->data.name+(uiSample->data.truncated ? " [TRUNCATED]" : "");
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
        const char* outNames[]={"Left audio","Right audio","Amp envelope","Envelope 2","Envelope 3","LFO 1","LFO 2","FUN 1","FUN 2","FUN 3","FUN 4"};
        for(int i=0;i<Vastish::OUTPUTS;++i) configOutput(i,outNames[i]);
    }
    void onSampleRateChange(const SampleRateChangeEvent& e) override { engine.prepare(e.sampleRate); }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); engine.panic(); for(auto& t:triggers) t.reset(); in={}; displayedProgram=1;
    }
    void process(const ProcessArgs& args) override {
        if(requestedPanic.exchange(false)) {
            engine.panic();
            // Suppress held gates until they fall, so Panic also works during sustained CV.
            for(int i=0;i<8;++i) in[i].gate=false;
        }
        int pg=requestedProgram.exchange(-1);
        if(pg>=0 && pg<6) { for(int i=0;i<PARAM_COUNT;++i) params[i].setValue(programs()[pg][i]); displayedProgram=pg; }
        Parameters p;
        for(int i=0;i<PARAM_COUNT;++i) {
            float x=params[i].getValue(); p[i]=std::isfinite(x)?vastish::clamp(x,metadata()[i].lo,metadata()[i].hi):programs()[0][i];
        }
        int channels=std::min(8,std::max(inputs[PITCH_IN].getChannels(),inputs[GATE_IN].getChannels()));
        bool active=false;
        for(int i=0;i<8;++i) {
            auto voltage=[&](int port) {
                float x=inputs[port].getPolyVoltage(i); return std::isfinite(x)?x:0.f;
            };
            bool was=triggers[i].isHigh();
            triggers[i].process(i<channels?voltage(GATE_IN):0.f);
            bool rise=!was && triggers[i].isHigh();
            if(i<channels) {
                // Pitch is held after note-off, matching the Pd voice's stored MIDI key.
                if(triggers[i].isHigh()) in[i].note=vastish::clamp(60+12*voltage(PITCH_IN),-20,135);
                if(rise) { in[i].velocity=inputs[VEL_IN].isConnected()?vastish::clamp(voltage(VEL_IN)*.1f,0,1):1; in[i].gate=true; }
                in[i].modwheel=vastish::clamp(voltage(MOD_IN)*.1f,0,1);
            }
            // AUX: per voice from a poly cable; a mono cable reaches every voice.
            for(int k=0;k<4;++k) in[i].aux[k]=vastish::clamp(voltage(AUX1_IN+k)*.1f,-1,1);
            if(!triggers[i].isHigh()) in[i].gate=false;
            active|=in[i].gate;
        }
        if(!retiredSample.load(std::memory_order_acquire))
            if(auto* s=pendingSample.exchange(nullptr,std::memory_order_acq_rel)) {
                if(liveSample) retiredSample.store(liveSample,std::memory_order_release);
                liveSample=s;
            }
        SampleView view; if(liveSample) { view=liveSample->view; view.loop=sampleLoop.load(std::memory_order_relaxed); }
        auto y=engine.tick(p,in,std::max(1,channels),liveSample ? &view : nullptr);
        outputs[LEFT_OUT].setVoltage(y[0]*5); outputs[RIGHT_OUT].setVoltage(y[1]*5);
        const int source[]={SRC_AMPENV,1,2,3,4,SRC_FUN1,SRC_FUN2,SRC_FUN3,SRC_FUN4};
        int n=std::max(1,channels);
        for(int o=AMP_OUT;o<OUTPUTS;++o) {
            outputs[o].setChannels(n);
            for(int i=0;i<n;++i) outputs[o].setVoltage(engine.voices[i].sources[source[o-AMP_OUT]]*10,i);
        }
        lights[GATE_LIGHT].setBrightness(active?1:0);
        lights[FAULT_LIGHT].setBrightness(engine.faults?1:0);
        (void)args;
    }
    json_t* dataToJson() override {
        json_t* j=json_object(); json_object_set_new(j,"program",json_integer(displayedProgram.load()));
        json_object_set_new(j,"sampleLoop",json_boolean(sampleLoop.load()));
        json_object_set_new(j,"followOtis",json_boolean(followOtis));
        collectSamples();
        if(uiSample && uiSample->view.valid()) {
            // Rack autosaves often; encode each sample once.
            if(encodedGeneration!=uiSample->generation) { encodedPcm=dspemu::encodePcm16(uiSample->data.pcm.data(),uiSample->data.pcm.size()); encodedGeneration=uiSample->generation; }
            json_t* s=json_object();
            json_object_set_new(s,"name",json_string(uiSample->data.name.c_str()));
            json_object_set_new(s,"rate",json_real(uiSample->data.rate));
            json_object_set_new(s,"pcm16le",json_string(encodedPcm.c_str()));
            json_object_set_new(j,"sample",s);
        }
        return j;
    }
    void dataFromJson(json_t* j) override {
        json_t* v=json_object_get(j,"program"); if(json_is_integer(v)) displayedProgram=int(vastish::clamp(float(json_integer_value(v)),0,5));
        sampleLoop=json_is_true(json_object_get(j,"sampleLoop"));
        setFollowOtis(json_is_true(json_object_get(j,"followOtis")));
        json_t* s=json_object_get(j,"sample");
        // A full restore defines the sample too: no saved sample means none, not
        // "keep whatever this instance had".
        if(!s) { if(uiSample && uiSample->view.valid()) publishSample(new SampleSlot()); sampleError.clear(); return; }
        try {
            dspemu::MonoSample m;
            const char* text=json_string_value(json_object_get(s,"pcm16le"));
            if(!text) throw std::runtime_error("Saved sample has no PCM");
            m.pcm=dspemu::decodePcm16(text,MAX_SAMPLE_FRAMES); m.rate=json_number_value(json_object_get(s,"rate"));
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
        if(q && q->paramId==PITCH_WAVE && choice(q->getValue(),SAMPLE_WAVE)==SAMPLE_WAVE)
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
        for(int i=0;i<Vastish::OUTPUTS;++i) addOutput(createOutputCentered<PJ301MPort>(Vec(245+i*40,348),m,i));
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
            m->followShare(Vastish::findOtisShare(id>=0 ? APP->engine->getModule(id) : nullptr));
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
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Sample: "+m->sampleLabel()));
        if(!m->sampleError.empty()) menu->addChild(createMenuLabel("Error: "+m->sampleError));
        menu->addChild(createMenuItem("Load sample WAV…","",[m]() {
            auto filters=osdialog_filters_parse("WAV:wav,WAV"); char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters); osdialog_filters_free(filters);
            if(!path) return;
            m->setFollowOtis(false); m->loadSampleFile(path); std::free(path);
            if(m->sampleError.empty()) m->params[PITCH_WAVE].setValue(SAMPLE_WAVE);
        }));
        menu->addChild(createBoolMenuItem("Loop sample","",[m]{return m->sampleLoop.load();},[m](bool on){m->sampleLoop=on;}));
        menu->addChild(createMenuItem("Clear sample","",[m]{ m->setFollowOtis(false); m->publishSample(new SampleSlot()); }));
        menu->addChild(createBoolMenuItem("Follow OTIS on the left","",[m]{return m->followOtis;},
            [m](bool on){ m->setFollowOtis(on); if(on) m->params[PITCH_WAVE].setValue(SAMPLE_WAVE); }));
        if(m->followOtis) menu->addChild(createMenuLabel(m->linkState==Vastish::LINK_CONNECTED ? "OTIS link: connected" :
            "OTIS link: place Vastish directly right of OTIS"));
        menu->addChild(createMenuLabel("SAMPLE wave: 0 V / C4 plays at recorded pitch"));
        menu->addChild(createMenuLabel("Eight voices • 0 V = C4 • gate > 1 V"));
        menu->addChild(createMenuLabel("Audio: ±1 DSP = ±5 V. Envelopes: 0–10 V."));
        menu->addChild(createMenuLabel("Programs are starting points; all edits save in the Rack patch."));
    }
};
Model* modelVastish=createModel<Vastish,VastishWidget>("Vastish");
