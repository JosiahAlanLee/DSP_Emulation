#include "plugin.hpp"
#include "engine/Engine.hpp"
#include <atomic>
using namespace vastish;
struct Vastish : Module {
    enum { PITCH_IN, GATE_IN, VEL_IN, MOD_IN, INPUTS };
    enum { LEFT_OUT, RIGHT_OUT, AMP_OUT, ENV2_OUT, ENV3_OUT, LFO1_OUT, LFO2_OUT, FUN1_OUT, FUN2_OUT, OUTPUTS };
    enum { GATE_LIGHT, FAULT_LIGHT, LIGHTS };
    vastish::Engine engine;
    dsp::SchmittTrigger triggers[8];
    std::array<VoiceInput,8> in{};
    std::atomic<int> requestedProgram{-1}, displayedProgram{1};
    std::atomic<bool> requestedPanic{false};
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
        const char* outNames[]={"Left audio","Right audio","Amp envelope","Envelope 2","Envelope 3","LFO 1","LFO 2","FUN 1","FUN 2"};
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
            if(!triggers[i].isHigh()) in[i].gate=false;
            active|=in[i].gate;
        }
        auto y=engine.tick(p,in,std::max(1,channels));
        outputs[LEFT_OUT].setVoltage(y[0]*5); outputs[RIGHT_OUT].setVoltage(y[1]*5);
        const int source[]={11,1,2,3,4,5,6};
        int n=std::max(1,channels);
        for(int o=AMP_OUT;o<OUTPUTS;++o) {
            outputs[o].setChannels(n);
            for(int i=0;i<n;++i) outputs[o].setVoltage(engine.voices[i].sources[source[o-AMP_OUT]]*10,i);
        }
        lights[GATE_LIGHT].setBrightness(active?1:0);
        lights[FAULT_LIGHT].setBrightness(engine.faults?1:0);
        (void)args;
    }
    json_t* dataToJson() override { json_t* j=json_object(); json_object_set_new(j,"program",json_integer(displayedProgram.load())); return j; }
    void dataFromJson(json_t* j) override {
        json_t* v=json_object_get(j,"program"); if(json_is_integer(v)) displayedProgram=int(vastish::clamp(float(json_integer_value(v)),0,5));
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
        addChild(createWidget<ScrewSilver>(Vec(2,0))); addChild(createWidget<ScrewSilver>(Vec(702,0)));
        addChild(createWidget<ScrewSilver>(Vec(2,365))); addChild(createWidget<ScrewSilver>(Vec(702,365)));
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
        for(int f=0;f<2;++f) for(int k=0;k<3;++k)
            select(FUN1_A+f*3+k,610+f*50,260+k*24,47);
        for(int i=0;i<Vastish::INPUTS;++i) addInput(createInputCentered<PJ301MPort>(Vec(30+i*48,348),m,i));
        for(int i=0;i<Vastish::OUTPUTS;++i) addOutput(createOutputCentered<PJ301MPort>(Vec(245+i*40,348),m,i));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(180,14),m,Vastish::GATE_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(Vec(180,27),m,Vastish::FAULT_LIGHT));
    }
    void appendContextMenu(Menu* menu) override {
        auto* m=dynamic_cast<Vastish*>(module); if(!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Load Pd program","",[m](Menu* child) {
            for(int k=0;k<6;++k) child->addChild(createMenuItem(programNames[k],"",[m,k]{m->requestedProgram=k;}));
        }));
        menu->addChild(createMenuItem("Panic / silence held gates","",[m]{m->requestedPanic=true;}));
        menu->addChild(createMenuLabel("Eight voices • 0 V = C4 • gate > 1 V"));
        menu->addChild(createMenuLabel("Audio: ±1 DSP = ±5 V. Envelopes: 0–10 V."));
        menu->addChild(createMenuLabel("Programs are starting points; all edits save in the Rack patch."));
    }
};
Model* modelVastish=createModel<Vastish,VastishWidget>("Vastish");
