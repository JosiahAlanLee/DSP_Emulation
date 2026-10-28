// SPDX-License-Identifier: AGPL-3.0-only
// Monomodule: the Monomachine's six tracks in Rack, on shnolk's Monomodule engine and the shared
// emulated-instrument adapter (vcv/adapter). A Gorgon on its left plays it through the sound link: each
// lane a track, with its note, machine and SYN locks; a TRIG cable plays it instead.
#include "plugin.hpp"
#include "engine/Worker.hpp"
#include "engine/Monomachine.hpp"
#include "Layout.hpp"
#include "../../adapter/src/RackLink.hpp"
#include <osdialog.h>
#include <cstdlib>

struct Monomodule : Module {
    enum Params { MACHINE, TEMPO, GAIN, P1, NUM_PARAMS=P1+8 };
    enum Inputs { TRIG, VOCT, MACHINE_CV, RESET, P1_CV, INP_A=P1_CV+8, INP_B, NUM_INPUTS };
    enum Outputs { LEFT, RIGHT, TRACKS_L, TRACKS_R, NUM_OUTPUTS };
    enum Lights { READY, LINK, NUM_LIGHTS };
    mnm_rack::Worker worker;
    gorgon_sound::Packet messages[2];
    mnm_rack::InputBlock block;
    mnm_rack::LinkState link;
    std::array<mnm_rack::Edge,mnm_rack::tracks> gates;
    mnm_rack::Edge reset;
    uint64_t generation=0,lastLoaded=0;
    int64_t frame=0,missedBlock=-1;
    double previousRate=0;
    bool external=false,active=false;
    std::atomic<uint64_t> underruns{0},lostEvents{0};
    std::mutex pathMutex;
    std::string firmwarePath;
    // Each FX track's input (mnm_rack::FxSource), set from the menu. Track 1 has no neighbour, so it
    // starts on INP A+B, as Monomodule Six's does.
    std::array<std::atomic<int>,mnm_rack::tracks> fxSource;

    explicit Monomodule(mnm_rack::Worker::Factory factory=[](const std::string& path,const std::string&){return std::make_unique<mnm_rack::Monomachine>(path);}):worker(std::move(factory)) {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        configSwitch(MACHINE,0,21,4,"Machine (when neither the hit nor MACHINE CV picks one)",[]{
            std::vector<std::string> names;for(auto m:mnm_rack::machineOrder)names.push_back(mnm::host::machineDef(m)->name);return names;}());
        configParam(TEMPO,30,300,120,"Engine tempo for synced LFOs and delays"," BPM");
        configParam(GAIN,0,2,1,"Output gain","%",0,100);
        configInput(TRIG,"Gates · 6 polyphonic tracks; each gate is a note, held until it falls · cable overrides Gorgon link");
        configInput(VOCT,"V/OCT · the note at each TRIG gate, C4 = 0 V · mono reaches every track");
        configInput(MACHINE_CV,"Machine · 0–10 V spans the 22 machines · overrides the selector when the hit has no machine");
        configInput(RESET,"Panic · rising edge ends every note before simultaneous hits · machines and FX tracks stay");
        for(int i=0;i<8;++i){configParam(P1+i,-127,127,0,"P"+std::to_string(i+1)+" raw offset (SYN page)");configInput(P1_CV+i,"P"+std::to_string(i+1)+" offset · 10 V = 127 · sampled at each note");}
        configInput(INP_A,"INP A · audio for FX machines · 5 V = full scale");
        configInput(INP_B,"INP B · audio for FX machines · 5 V = full scale");
        for(int t=0;t<mnm_rack::tracks;++t)fxSource[size_t(t)]=t?mnm_rack::NEIBOR:mnm_rack::INP_AB;
        configOutput(LEFT,"Main left (OUT BUS AB) · 5 V per full scale");configOutput(RIGHT,"Main right (OUT BUS AB) · 5 V per full scale");
        configOutput(TRACKS_L,"Tracks 1–6, left · polyphonic audio");configOutput(TRACKS_R,"Tracks 1–6, right · polyphonic audio");
        leftExpander.producerMessage=&messages[0];leftExpander.consumerMessage=&messages[1];
    }
    float cv(int input,int track) {return emu_rack::trackCv(inputs[input],track);}
    void loadFirmware(const std::string& path) {
        if(path.empty())return;
        std::lock_guard<std::mutex> guard(pathMutex);firmwarePath=path;worker.load(firmwarePath);
    }
    json_t* dataToJson() override {
        auto* root=json_object();std::lock_guard<std::mutex> guard(pathMutex);
        json_object_set_new(root,"firmwarePath",json_string(firmwarePath.c_str()));
        auto* sources=json_array();for(auto& s:fxSource)json_array_append_new(sources,json_integer(s.load()));
        json_object_set_new(root,"fxInputs",sources);return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* sources=json_object_get(root,"fxInputs");
        for(int t=0;t<mnm_rack::tracks;++t){
            json_t* s=json_array_get(sources,size_t(t));
            int v=json_is_integer(s)?int(json_integer_value(s)):-1;
            fxSource[size_t(t)]=v>=mnm_rack::NEIBOR&&v<=mnm_rack::INP_AB?v:(t?mnm_rack::NEIBOR:mnm_rack::INP_AB);
        }
        if(const char* path=json_string_value(json_object_get(root,"firmwarePath")))loadFirmware(path);
    }
    void restart(double rate) {
        ++generation;worker.generation(generation);frame=0;missedBlock=-1;previousRate=rate;
        // A reload, a rate change or a dropped block may skip whole gates: reconnect from the current
        // levels, never replay a held high.
        link=mnm_rack::LinkState();external=false;
        for(auto& edge:gates)edge.high=false;
        reset.high=inputs[RESET].getVoltage()>.1f;
        block=mnm_rack::InputBlock();block.generation=generation;block.rate=rate;
        mnm_rack::Event event;event.panic=true;block.append(event);
    }
    void appendPanic(int offset){mnm_rack::Event event;event.offset=uint16_t(offset);event.panic=true;block.append(event);}
    void appendRelease(int track,int offset){mnm_rack::Event event;event.offset=uint16_t(offset);event.release=true;event.hit.track=uint8_t(track);block.append(event);}
    void appendNote(int track,float velocity,const gorgon_sound::Sound& sound,int note,int offset) {
        float knobs[8],voltages[8];for(int i=0;i<8;++i){knobs[i]=params[P1+i].getValue();voltages[i]=cv(P1_CV+i,track);}
        int selected=mnm_rack::machineIdAt(int(std::lround(params[MACHINE].getValue())));
        if(inputs[MACHINE_CV].isConnected())
            selected=mnm_rack::machineIdAt(int(std::lround(std::clamp(cv(MACHINE_CV,track),0.f,10.f)*2.1f)));
        mnm_rack::Event event;event.offset=uint16_t(offset);
        event.hit=mnm_rack::capture(track,sound,selected,velocity,knobs,voltages,params[TEMPO].getValue(),note);
        if(event.hit.velocity)block.append(event);
    }
    void process(const ProcessArgs& args) override {
        outputs[TRACKS_L].setChannels(mnm_rack::tracks);outputs[TRACKS_R].setChannels(mnm_rack::tracks);
        bool ready=worker.ready.load(std::memory_order_acquire);
        lights[READY].setBrightness(ready?1.f:0.f);
        if(!ready){
            active=false;lights[LINK].setBrightness(0);
            for(auto& out:outputs)for(int c=0;c<out.getChannels();++c)out.setVoltage(0,c);
            return;
        }
        auto loaded=worker.loaded.load(std::memory_order_acquire);
        if(!active||args.sampleRate!=previousRate||loaded!=lastLoaded){restart(args.sampleRate);lastLoaded=loaded;active=true;}
        int offset=int(frame%mnm_rack::frames);
        bool patched=inputs[TRIG].isConnected();
        // The TRIG cable was pulled out (or the link takes over): its held gates fall here, so their notes
        // end rather than sustain until the track's next note.
        if(external&&!patched)
            for(int t=0;t<mnm_rack::tracks;++t)if(gates[size_t(t)].high){gates[size_t(t)].high=false;appendRelease(t,offset);}
        const auto* packet=patched?nullptr:emu_rack::soundPacket(leftExpander.module,leftExpander.consumerMessage,args.frame);
        auto linked=link.update(packet,args.frame);
        lights[LINK].setBrightness(link.connected?1.f:0.f);
        // Gorgon pulses RESET at every loop wrap. For drums that is a panic (Machinemodule); here it would
        // end every held note each loop, so only a seek, a stop or a disconnect is. A panic ends notes
        // only: every track keeps its machine and an FX track keeps processing (Tracks::panic).
        if(linked.panic&&!linked.resetOnly)appendPanic(offset);
        if(reset.process(inputs[RESET].getVoltage()))appendPanic(offset);
        if(patched){
            // A gate is a note: it rises with V/OCT's note and ends when it falls. A fresh cable seeds the
            // current levels, so a gate already high doesn't play.
            int n=std::min(inputs[TRIG].getChannels(),mnm_rack::tracks);
            gorgon_sound::Sound sound;
            if(!external)for(int t=0;t<mnm_rack::tracks;++t)gates[size_t(t)].high=t<n&&inputs[TRIG].getVoltage(t)>.1f;
            for(int t=0;t<mnm_rack::tracks;++t){
                bool was=gates[size_t(t)].high;
                if(gates[size_t(t)].process(t<n?inputs[TRIG].getVoltage(t):0.f))appendNote(t,1.f,sound,mnm_rack::noteOf(cv(VOCT,t)),offset);
                else if(was&&!gates[size_t(t)].high)appendRelease(t,offset);
            }
        }else if(packet){
            // The link's pitch is C4 = 0 V (Gorgon's sound-locks.md), so it carries the note.
            for(int i=0;i<gorgon_sound::LANES;++i){
                if(linked.releases[size_t(i)])appendRelease(i,offset);
                if(linked.hits[size_t(i)])appendNote(i,packet->lanes[i].velocity,packet->lanes[i].sound,mnm_rack::noteOf(packet->lanes[i].pitch),offset);
            }
        }
        external=patched;
        // INP A/B travel with the block, sample by sample; the worker converts them to the engine's rate.
        block.audio.samples[offset][0]=mnm_rack::finite(inputs[INP_A].getVoltage(),-10,10)/5.f;
        block.audio.samples[offset][1]=mnm_rack::finite(inputs[INP_B].getVoltage(),-10,10)/5.f;
        for(int t=0;t<mnm_rack::tracks;++t)block.settings[t]=uint8_t(fxSource[size_t(t)].load(std::memory_order_relaxed));
        float sample[mnm_rack::channels]={};
        int64_t wanted=frame-mnm_rack::queueLatency;
        const auto* output=worker.front();
        while(output&&(output->generation!=generation||output->start+mnm_rack::frames<=wanted)){worker.pop();output=worker.front();}
        if(wanted>=0){
            auto start=wanted-wanted%mnm_rack::frames;
            bool available=output&&output->start==start;
            if(wanted%mnm_rack::frames==0&&!available){++underruns;missedBlock=start;}
            // A missed block is dropped in full even if the worker finishes it later.
            if(available&&missedBlock!=start)std::copy(output->samples[wanted-start],output->samples[wanted-start]+mnm_rack::channels,sample);
        }
        float gain=5.f*mnm_rack::finite(params[GAIN].getValue(),0,2,1);
        outputs[LEFT].setVoltage(mnm_rack::finite(sample[0],-4,4)*gain);
        outputs[RIGHT].setVoltage(mnm_rack::finite(sample[1],-4,4)*gain);
        for(int t=0;t<mnm_rack::tracks;++t){
            outputs[TRACKS_L].setVoltage(mnm_rack::finite(sample[mnm_rack::trackLeft+t],-4,4)*gain,t);
            outputs[TRACKS_R].setVoltage(mnm_rack::finite(sample[mnm_rack::trackRight+t],-4,4)*gain,t);
        }
        ++frame;
        if(frame%mnm_rack::frames==0){
            if(block.overflow){++lostEvents;restart(args.sampleRate);}
            else if(!worker.submit(block)){++lostEvents;restart(args.sampleRate);}
            else{block.start=frame;block.count=0;block.overflow=false;}
        }
    }
};

struct MachineNameDisplay : widget::Widget {
    Monomodule* module = nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font = APP->window->uiFont; if (!font) return;
        int index = module ? int(std::lround(module->params[Monomodule::MACHINE].getValue())) : 4;
        int id = mnm_rack::machineIdAt(index);
        std::string label = string::f("LOCAL %02d / ",id)+mnm_rack::machineDef(id)->name;
        if (module && !module->worker.ready.load(std::memory_order_acquire)) label = "LOAD MONOMACHINE OS FROM MENU";
        nvgSave(args.vg); nvgScissor(args.vg,4,0,box.size.x-8,box.size.y);
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,9);
        nvgTextAlign(args.vg,NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(143,220,196));
        nvgText(args.vg,box.size.x/2,box.size.y/2,label.c_str(),nullptr);
        nvgRestore(args.vg);
    }
};

struct MonomoduleWidget:ModuleWidget {
    explicit MonomoduleWidget(Monomodule* module) {
        using namespace monomodule_layout::voice;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module);setPanel(createPanel(asset::plugin(pluginInstance,"res/Monomodule.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(WIDTH-15,365)));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(MACHINE),module,Monomodule::MACHINE));
        addParam(createParamCentered<RoundBlackKnob>(POS(TEMPO),module,Monomodule::TEMPO));
        addParam(createParamCentered<RoundBlackKnob>(POS(GAIN),module,Monomodule::GAIN));
        auto* name = createWidget<MachineNameDisplay>(POS(MACHINE_NAME));
        name->box.size = Vec(MACHINE_NAME_W,MACHINE_NAME_H); name->module = module; addChild(name);
        const Vec ins[] = {POS(TRIG),POS(VOCT),POS(MACHINE_CV),POS(RESET)};
        addInput(createInputCentered<PJ301MPort>(POS(INP_A),module,Monomodule::INP_A));
        addInput(createInputCentered<PJ301MPort>(POS(INP_B),module,Monomodule::INP_B));
        const Vec knobs[] = {POS(P1),POS(P2),POS(P3),POS(P4),POS(P5),POS(P6),POS(P7),POS(P8)};
        const Vec cvs[] = {POS(P1_CV),POS(P2_CV),POS(P3_CV),POS(P4_CV),POS(P5_CV),POS(P6_CV),POS(P7_CV),POS(P8_CV)};
        const Vec outs[] = {POS(LEFT),POS(RIGHT),POS(TRACKS_L),POS(TRACKS_R)};
        for(int i=0;i<4;++i)addInput(createInputCentered<PJ301MPort>(ins[i],module,Monomodule::TRIG+i));
        for(int i=0;i<8;++i){
            addParam(createParamCentered<RoundSmallBlackKnob>(knobs[i],module,Monomodule::P1+i));
            addInput(createInputCentered<PJ301MPort>(cvs[i],module,Monomodule::P1_CV+i));
        }
        for(int i=0;i<4;++i)addOutput(createOutputCentered<DarkPJ301MPort>(outs[i],module,Monomodule::LEFT+i));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(READY),module,Monomodule::READY));
        addChild(createLightCentered<SmallLight<BlueLight>>(POS(LINK),module,Monomodule::LINK));
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* module=dynamic_cast<Monomodule*>(this->module);if(!module)return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load Monomachine OS 1.32B (.syx)…","",[module]{
            auto* filters=osdialog_filters_parse("Monomachine OS:syx");
            char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters);osdialog_filters_free(filters);
            if(path){module->loadFirmware(path);std::free(path);}
        }));
        int id=mnm_rack::machineIdAt(int(std::lround(module->params[Monomodule::MACHINE].getValue())));
        if(const auto* def=mnm_rack::machineDef(id)){
            std::string labels=std::string(def->name)+" · ";for(const char* label:def->labels)labels+=std::string(label)+" ";
            menu->addChild(createMenuLabel(labels));
        }
        menu->addChild(createMenuLabel(module->worker.status()));
        menu->addChild(createMenuLabel("Latency: "+std::to_string(mnm_rack::queueLatency+module->worker.converterDelay.load())+" host samples"));
        menu->addChild(createMenuLabel("Through an FX machine: one engine block more, 15–16 samples at 44.1 kHz (about 0.35 ms)"));
        menu->addChild(createMenuLabel("Missed audio blocks: "+std::to_string(module->underruns.load())+" · dropped input blocks: "+std::to_string(module->lostEvents.load())));
        menu->addChild(createMenuLabel("Queue overflows: "+std::to_string(module->worker.overflows.load())+" · coalesced notes: "+std::to_string(module->worker.coalesced.load())));
        menu->addChild(createMenuLabel("Unavailable machine notes: "+std::to_string(module->worker.unavailable.load())));
        menu->addChild(createSubmenuItem("FX inputs","",[module](Menu* submenu){
            const char* names[]={"NEIBOR (the track before)","INP A","INP B","INP A+B"};
            for(int t=0;t<mnm_rack::tracks;++t)
                submenu->addChild(createSubmenuItem(string::f("Track %d",t+1),names[module->fxSource[size_t(t)].load()],[module,t,names](Menu* sources){
                    for(int s=mnm_rack::NEIBOR;s<=mnm_rack::INP_AB;++s)
                        sources->addChild(createCheckMenuItem(names[s],"",[module,t,s]{return module->fxSource[size_t(t)].load()==s;},
                            [module,t,s]{module->fxSource[size_t(t)]=s;}));
                }));
        }));
        menu->addChild(createMenuLabel("An FX machine hears its FX input. P1–P8 + CV latch at each note."));
    }
};
Model* modelMonomodule=createModel<Monomodule,MonomoduleWidget>("Monomodule");
