// SPDX-License-Identifier: AGPL-3.0-only
#include "plugin.hpp"
#include "engine/Worker.hpp"
#include "engine/Machinedrum.hpp"
#include "Layout.hpp"
#include "../../shared/ByteBus.hpp"
#include "../../adapter/src/RackLink.hpp"
#include <osdialog.h>
#include <cstdlib>

struct Machinemodule : Module {
    enum Params { MACHINE, TEMPO, GAIN, P1, NUM_PARAMS=P1+8 };
    enum Inputs { TRIG, VELOCITY, MACHINE_CV, RESET, P1_CV, NUM_INPUTS=P1_CV+8 };
    enum Outputs { LEFT, RIGHT, TRACKS, NUM_OUTPUTS };
    enum Lights { READY, LINK, NUM_LIGHTS };
    // What TRIG reads: one track per channel, or a byte bus (docs/BUS_TRIG.md).
    enum TrigMode { TRIG_TRACKS, TRIG_BUS };
    md_rack::Worker worker;
    gorgon_sound::Packet messages[2];
    md_rack::InputBlock block;
    md_rack::LinkState link;
    std::array<md_rack::Edge,16> gates;
    md_rack::Edge reset;
    std::atomic<int> trigMode{TRIG_TRACKS};
    int lastTrigMode=TRIG_TRACKS;
    dspemu::bus::Reader busReader;
    bool busClocked=false,busArmed=false;
    uint64_t generation=0,lastLoaded=0;
    int64_t frame=0,missedBlock=-1;
    double previousRate=0;
    bool external=false,active=false;
    std::atomic<uint64_t> underruns{0},lostEvents{0};
    std::mutex pathMutex;
    std::string firmwarePath,samplePath;

    explicit Machinemodule(md_rack::Worker::Factory factory=[](const std::string& path,const std::string& samples){return std::make_unique<md_rack::Machinedrum>(path,samples);}):worker(std::move(factory)) {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        configParam(MACHINE,0,191,16,"Machine ID (unavailable IDs are silent)");getParamQuantity(MACHINE)->snapEnabled=true;
        configParam(TEMPO,30,300,120,"Engine tempo for synced retrigs/LFOs"," BPM");
        configParam(GAIN,0,2,1,"Output gain","%",0,100);
        configInput(TRIG,"Triggers · 16 polyphonic tracks, or a byte bus (menu) · cable overrides Gorgon link");
        configInput(VELOCITY,"Velocity · 0–10 V · unpatched = 127");
        configInput(MACHINE_CV,"Absolute machine · 0–10 V = IDs 0–191 · overrides local selector");
        configInput(RESET,"Panic · rising edge silences voices before simultaneous hits");
        for(int i=0;i<8;++i){configParam(P1+i,-127,127,0,"P"+std::to_string(i+1)+" raw offset");configInput(P1_CV+i,"P"+std::to_string(i+1)+" offset · 10 V = 127 · sampled at hit");}
        configOutput(LEFT,"Main left · 5 V per full scale");configOutput(RIGHT,"Main right · 5 V per full scale");
        configOutput(TRACKS,"16 individual tracks after track effects · polyphonic audio");
        leftExpander.producerMessage=&messages[0];leftExpander.consumerMessage=&messages[1];
    }
    float cv(int input,int lane) {return emu_rack::trackCv(inputs[input],lane);}
    void loadFirmware(const std::string& path,bool samples=false) {
        if(path.empty())return;
        {std::lock_guard<std::mutex> guard(pathMutex);if(samples)samplePath=path;else firmwarePath=path;worker.load(firmwarePath,samplePath);}
    }
    json_t* dataToJson() override {
        auto* root=json_object();std::lock_guard<std::mutex> guard(pathMutex);
        json_object_set_new(root,"firmwarePath",json_string(firmwarePath.c_str()));json_object_set_new(root,"samplePath",json_string(samplePath.c_str()));
        json_object_set_new(root,"trigMode",json_string(trigMode.load()==TRIG_BUS?"bus":"tracks"));return root;
    }
    void dataFromJson(json_t* root) override {
        if(const char* samples=json_string_value(json_object_get(root,"samplePath")))samplePath=samples;
        const char* mode=json_string_value(json_object_get(root,"trigMode"));trigMode=mode&&std::string(mode)=="bus"?TRIG_BUS:TRIG_TRACKS;
        if(const char* path=json_string_value(json_object_get(root,"firmwarePath")))loadFirmware(path);
    }
    void restart(double rate) {
        ++generation;worker.generation(generation);frame=0;missedBlock=-1;previousRate=rate;
        // Reloads, rate changes and dropped input jobs may skip whole gate
        // pulses. Reconnect by seeding current levels, never replay a held high.
        link=md_rack::LinkState();external=false;
        for(auto& edge:gates)edge.high=false;
        reset.high=inputs[RESET].getVoltage()>.1f;
        block=md_rack::InputBlock();block.generation=generation;block.rate=rate;
        // The first sample starts a new epoch and cuts the previous voices.
        md_rack::Event event;event.panic=true;block.append(event);
    }
    void appendPanic(int offset){md_rack::Event event;event.offset=uint16_t(offset);event.panic=true;block.append(event);}
    void appendHit(int lane,float velocity,const gorgon_sound::Sound& sound,int offset) {
        float knobs[8],voltages[8];for(int i=0;i<8;++i){knobs[i]=params[P1+i].getValue();voltages[i]=cv(P1_CV+i,lane);}
        int selected=int(std::lround(md_rack::finite(params[MACHINE].getValue(),0,191,16)));
        if(inputs[MACHINE_CV].isConnected())selected=int(std::lround(std::clamp(cv(MACHINE_CV,lane),0.f,10.f)*19.1f));
        md_rack::Event event;event.offset=uint16_t(offset);
        event.hit=md_rack::capture(lane,sound,selected,velocity,knobs,voltages,params[TEMPO].getValue());
        if(event.hit.velocity)block.append(event);
    }
    const gorgon_sound::Packet* soundPacket(int64_t now) {return emu_rack::soundPacket(leftExpander.module,leftExpander.consumerMessage,now);}
    void process(const ProcessArgs& args) override {
        outputs[TRACKS].setChannels(16);
        bool ready=worker.ready.load(std::memory_order_acquire);
        lights[READY].setBrightness(ready?1.f:0.f);
        if(!ready){
            active=false;lights[LINK].setBrightness(0);
            for(auto& out:outputs)for(int c=0;c<out.getChannels();++c)out.setVoltage(0,c);
            return;
        }
        auto loaded=worker.loaded.load(std::memory_order_acquire);
        if(!active||args.sampleRate!=previousRate||loaded!=lastLoaded){restart(args.sampleRate);lastLoaded=loaded;active=true;}
        int offset=int(frame%md_rack::frames);
        bool patched=inputs[TRIG].isConnected();
        const auto* packet=patched?nullptr:soundPacket(args.frame);
        auto linked=link.update(packet,args.frame);
        lights[LINK].setBrightness(link.connected?1.f:0.f);
        if(linked.panic)appendPanic(offset);
        bool resetHit=reset.process(inputs[RESET].getVoltage());if(resetHit)appendPanic(offset);
        if(patched){
            int mode=trigMode.load(std::memory_order_relaxed);
            // A source switch, a TRIG mode switch or a restart starts over, so a cable hot-plug
            // never replays a high gate.
            bool seed=!external||mode!=lastTrigMode;lastTrigMode=mode;
            auto velocity=[&](int lane){return inputs[VELOCITY].isConnected()?std::clamp(cv(VELOCITY,lane)/10.f,0.f,1.f):1.f;};
            gorgon_sound::Sound sound;
            if(seed)busReader.reset();
            auto data=mode==TRIG_BUS?busReader.process(inputs[TRIG]):dspemu::bus::Sample{};
            bool clocked=mode==TRIG_BUS&&data.clocked;
            bool fresh=seed||clocked!=busClocked;busClocked=clocked;
            if(clocked){
                // On each CLK rising edge, track k+1 hits iff bit k is high on that sample. The
                // first edge after a fresh start only arms, since a reader counts a CLK that is
                // already high as an edge.
                if(fresh)busArmed=false;
                if(data.rising){
                    if(busArmed)for(int k=0;k<8;++k)if(data.rawWord>>k&1)appendHit(k,velocity(k),sound,offset);
                    busArmed=true;
                }
            }else{
                // Tracks; an unclocked bus is the same on its eight bit lines.
                int n=std::min(inputs[TRIG].getChannels(),mode==TRIG_BUS?8:16);
                if(fresh)for(int i=0;i<16;++i)gates[i].high=i<n&&inputs[TRIG].getVoltage(i)>.1f;
                for(int i=0;i<16;++i)if(gates[i].process(i<n?inputs[TRIG].getVoltage(i):0.f))appendHit(i,velocity(i),sound,offset);
            }
        }else if(packet){for(int i=0;i<gorgon_sound::LANES;++i)if(linked.hits[i])appendHit(i,packet->lanes[i].velocity,packet->lanes[i].sound,offset);}
        external=patched;
        float sample[md_rack::channels]={};
        int64_t wanted=frame-md_rack::queueLatency;
        const auto* output=worker.front();
        while(output&&(output->generation!=generation||output->start+md_rack::frames<=wanted)){worker.pop();output=worker.front();}
        if(wanted>=0){
            auto start=wanted-wanted%md_rack::frames;
            bool available=output&&output->start==start;
            if(wanted%md_rack::frames==0&&!available){++underruns;missedBlock=start;}
            // A missed block is dropped in full even if the worker finishes it later.
            if(available&&missedBlock!=start)std::copy(output->samples[wanted-start],output->samples[wanted-start]+md_rack::channels,sample);
        }
        float gain=5.f*md_rack::finite(params[GAIN].getValue(),0,2,1);
        outputs[LEFT].setVoltage(md_rack::finite(sample[0],-4,4)*gain);
        outputs[RIGHT].setVoltage(md_rack::finite(sample[1],-4,4)*gain);
        for(int i=0;i<16;++i)outputs[TRACKS].setVoltage(md_rack::finite(sample[2+i],-4,4)*gain,i);
        ++frame;
        if(frame%md_rack::frames==0){
            if(block.overflow){++lostEvents;restart(args.sampleRate);}
            else if(!worker.submit(block)){++lostEvents;restart(args.sampleRate);}
            else{block.start=frame;block.count=0;block.overflow=false;}
        }
    }
};

// UI thread only. Refresh the name list when the worker publishes a loaded OS,
// rather than copying the firmware's machine metadata on every draw.
struct MachineNameDisplay : widget::Widget {
    Machinemodule* module = nullptr;
    uint64_t loaded = ~uint64_t(0);
    std::vector<md_rack::Machine> machines;
    void step() override {
        if (module) {
            uint64_t current = module->worker.loaded.load(std::memory_order_acquire);
            if (current != loaded) { machines = module->worker.machines(); loaded = current; }
        }
        Widget::step();
    }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font = APP->window->uiFont; if (!font) return;
        int selected = module ? int(std::lround(md_rack::finite(module->params[Machinemodule::MACHINE].getValue(),0,191,16))) : 16;
        std::string name = "LOAD OS FROM MENU";
        if (module && module->worker.ready.load(std::memory_order_acquire)) {
            name = "UNAVAILABLE ID";
            for (const auto& machine : machines) if (machine.id == selected) { name = machine.name; break; }
        }
        std::string label = string::f("LOCAL %03d / ",selected)+name;
        nvgSave(args.vg); nvgScissor(args.vg,4,0,box.size.x-8,box.size.y);
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,9);
        nvgTextAlign(args.vg,NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(143,220,196));
        nvgText(args.vg,box.size.x/2,box.size.y/2,label.c_str(),nullptr);
        nvgRestore(args.vg);
    }
};

struct MachinemoduleWidget:ModuleWidget {
    explicit MachinemoduleWidget(Machinemodule* module) {
        using namespace machinemodule_layout::voice;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module);setPanel(createPanel(asset::plugin(pluginInstance,"res/Machinemodule.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(machinemodule_layout::voice::WIDTH-15,365)));
        addParam(createParamCentered<RoundBlackSnapKnob>(POS(MACHINE),module,Machinemodule::MACHINE));
        addParam(createParamCentered<RoundBlackKnob>(POS(TEMPO),module,Machinemodule::TEMPO));
        addParam(createParamCentered<RoundBlackKnob>(POS(GAIN),module,Machinemodule::GAIN));
        auto* name = createWidget<MachineNameDisplay>(POS(MACHINE_NAME));
        name->box.size = Vec(MACHINE_NAME_W,MACHINE_NAME_H); name->module = module; addChild(name);
        const Vec ins[] = {POS(TRIG),POS(VELOCITY),POS(MACHINE_CV),POS(RESET)};
        const Vec knobs[] = {POS(P1),POS(P2),POS(P3),POS(P4),POS(P5),POS(P6),POS(P7),POS(P8)};
        const Vec cvs[] = {POS(P1_CV),POS(P2_CV),POS(P3_CV),POS(P4_CV),POS(P5_CV),POS(P6_CV),POS(P7_CV),POS(P8_CV)};
        const Vec outs[] = {POS(LEFT),POS(RIGHT),POS(TRACKS)};
        for(int i=0;i<4;++i)addInput(createInputCentered<PJ301MPort>(ins[i],module,Machinemodule::TRIG+i));
        for(int i=0;i<8;++i){
            addParam(createParamCentered<RoundSmallBlackKnob>(knobs[i],module,Machinemodule::P1+i));
            addInput(createInputCentered<PJ301MPort>(cvs[i],module,Machinemodule::P1_CV+i));
        }
        for(int i=0;i<3;++i)addOutput(createOutputCentered<DarkPJ301MPort>(outs[i],module,Machinemodule::LEFT+i));
        addChild(createLightCentered<SmallLight<GreenLight>>(POS(READY),module,Machinemodule::READY));
        addChild(createLightCentered<SmallLight<BlueLight>>(POS(LINK),module,Machinemodule::LINK));
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* module=dynamic_cast<Machinemodule*>(this->module);if(!module)return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem("Load Machinedrum OS (.syx)…","",[module]{
            auto* filters=osdialog_filters_parse("Machinedrum OS:syx");
            char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters);osdialog_filters_free(filters);
            if(path){module->loadFirmware(path);std::free(path);}
        }));
        menu->addChild(createMenuItem("Load ROM sample bank (MDS1 .bin)…","",[module]{
            auto* filters=osdialog_filters_parse("Machinemodule sample bank:bin");
            char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,filters);osdialog_filters_free(filters);
            if(path){module->loadFirmware(path,true);std::free(path);}
        }));
        menu->addChild(createMenuItem("Clear ROM sample bank","",[module]{std::lock_guard<std::mutex> guard(module->pathMutex);module->samplePath.clear();module->worker.load(module->firmwarePath);}));
        menu->addChild(createSubmenuItem("TRIG reads","",[module](Menu* submenu){
            for(int mode:{Machinemodule::TRIG_TRACKS,Machinemodule::TRIG_BUS})
                submenu->addChild(createCheckMenuItem(mode==Machinemodule::TRIG_BUS?"Bus: bits 0–7 hit tracks 1–8 on each CLK":"Tracks: channel = track","",
                    [module,mode]{return module->trigMode.load()==mode;},[module,mode]{module->trigMode=mode;}));
        }));
        menu->addChild(createSubmenuItem("Select machine from loaded OS","",[module](Menu* submenu){
            for(const auto& machine:module->worker.machines())submenu->addChild(createMenuItem(std::to_string(machine.id)+" · "+machine.name,"",[module,machine]{module->params[Machinemodule::MACHINE].setValue(float(machine.id));}));
        }));
        for(const auto& machine:module->worker.machines())if(machine.id==int(std::lround(module->params[Machinemodule::MACHINE].getValue()))) {
            std::string labels=machine.name+" · ";for(const auto& label:machine.labels)labels+=label+" ";menu->addChild(createMenuLabel(labels));break;
        }
        menu->addChild(createMenuLabel(module->worker.status()));
        menu->addChild(createMenuLabel("Latency: "+std::to_string(md_rack::queueLatency+module->worker.converterDelay.load())+" host samples"));
        menu->addChild(createMenuLabel("Missed audio blocks: "+std::to_string(module->underruns.load())+" · dropped input blocks: "+std::to_string(module->lostEvents.load())));
        menu->addChild(createMenuLabel("Queue overflows: "+std::to_string(module->worker.overflows.load())+" · coalesced hits: "+std::to_string(module->worker.coalesced.load())));
        menu->addChild(createMenuLabel("Unavailable machine hits: "+std::to_string(module->worker.unavailable.load())));
        menu->addChild(createMenuLabel("P1–P8 offsets + CV latch at each hit. External TRIG overrides link."));
    }
};
Model* modelMachinemodule=createModel<Machinemodule,MachinemoduleWidget>("Machinemodule");
