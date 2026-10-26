// SPDX-License-Identifier: AGPL-3.0-only
#include "plugin.hpp"
#include "engine/Worker.hpp"
#include "engine/Machinedrum.hpp"
#include <osdialog.h>
#include <cstdlib>

struct Machinemodule : Module {
    enum Params { MACHINE, TEMPO, GAIN, P1, NUM_PARAMS=P1+8 };
    enum Inputs { TRIG, VELOCITY, MACHINE_CV, RESET, P1_CV, NUM_INPUTS=P1_CV+8 };
    enum Outputs { LEFT, RIGHT, TRACKS, NUM_OUTPUTS };
    enum Lights { READY, LINK, NUM_LIGHTS };
    md_rack::Worker worker;
    gorgon_sound::Packet messages[2];
    md_rack::InputBlock block;
    md_rack::LinkState link;
    std::array<md_rack::Edge,16> gates;
    md_rack::Edge reset;
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
        configInput(TRIG,"Triggers · 16 polyphonic tracks · cable overrides Gorgon link");
        configInput(VELOCITY,"Velocity · 0–10 V · unpatched = 127");
        configInput(MACHINE_CV,"Absolute machine · 0–10 V = IDs 0–191 · overrides local selector");
        configInput(RESET,"Panic · rising edge silences voices before simultaneous hits");
        for(int i=0;i<8;++i){configParam(P1+i,-127,127,0,"P"+std::to_string(i+1)+" raw offset");configInput(P1_CV+i,"P"+std::to_string(i+1)+" offset · 10 V = 127 · sampled at hit");}
        configOutput(LEFT,"Main left · 5 V per full scale");configOutput(RIGHT,"Main right · 5 V per full scale");
        configOutput(TRACKS,"16 individual tracks after track effects · polyphonic audio");
        leftExpander.producerMessage=&messages[0];leftExpander.consumerMessage=&messages[1];
    }
    float cv(int input,int lane) {
        int n=inputs[input].getChannels();if(!n||(n>1&&lane>=n))return 0;
        return md_rack::finite(inputs[input].getVoltage(n==1?0:lane),-100,100);
    }
    void loadFirmware(const std::string& path,bool samples=false) {
        if(path.empty())return;
        {std::lock_guard<std::mutex> guard(pathMutex);if(samples)samplePath=path;else firmwarePath=path;worker.load(firmwarePath,samplePath);}
    }
    json_t* dataToJson() override {
        auto* root=json_object();std::lock_guard<std::mutex> guard(pathMutex);
        json_object_set_new(root,"firmwarePath",json_string(firmwarePath.c_str()));json_object_set_new(root,"samplePath",json_string(samplePath.c_str()));return root;
    }
    void dataFromJson(json_t* root) override {
        if(const char* samples=json_string_value(json_object_get(root,"samplePath")))samplePath=samples;
        if(const char* path=json_string_value(json_object_get(root,"firmwarePath")))loadFirmware(path);
    }
    void restart(double rate) {
        ++generation;worker.generation(generation);frame=0;missedBlock=-1;previousRate=rate;
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
    const gorgon_sound::Packet* soundPacket(int64_t now) {
        Module* provider=leftExpander.module;
        if(!provider||!provider->model||!provider->model->plugin||provider->model->plugin->slug!="GorgonBridge"||provider->model->slug!="Gorgon")return nullptr;
        // Exact sender family: older Gorgon has no protocol, future majors need negotiation.
        const auto& version=provider->model->plugin->version;
        if(version.rfind("2.",0)!=0)return nullptr;
        const auto* packet=static_cast<const gorgon_sound::Packet*>(leftExpander.consumerMessage);
        return packet&&gorgon_sound::valid(*packet,now)?packet:nullptr;
    }
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
            int n=std::min(inputs[TRIG].getChannels(),16);
            // Seed edges on a source switch to prevent a cable hot-plug from replaying a high gate.
            if(!external)for(int i=0;i<16;++i)gates[i].high=i<n&&inputs[TRIG].getVoltage(i)>.1f;
            for(int i=0;i<16;++i){
                bool edge=gates[i].process(i<n?inputs[TRIG].getVoltage(i):0.f);
                if(edge){gorgon_sound::Sound sound;appendHit(i,inputs[VELOCITY].isConnected()?std::clamp(cv(VELOCITY,i)/10.f,0.f,1.f):1.f,sound,offset);}
            }
        }else if(packet){for(int i=0;i<4;++i)if(linked.hits[i])appendHit(i,packet->lanes[i].velocity,packet->lanes[i].sound,offset);}
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

struct MachinemoduleWidget:ModuleWidget {
    explicit MachinemoduleWidget(Machinemodule* module) {
        setModule(module);setPanel(createPanel(asset::plugin(pluginInstance,"res/Machinemodule.svg")));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(17,19)),module,Machinemodule::MACHINE));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(45,19)),module,Machinemodule::TEMPO));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(73,19)),module,Machinemodule::GAIN));
        for(int i=0;i<4;++i)addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12+20*i,36)),module,Machinemodule::TRIG+i));
        for(int i=0;i<8;++i){int row=i/2,col=i%2;float x=col?53:13,y=53+17*row;addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x,y)),module,Machinemodule::P1+i));addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x+19,y)),module,Machinemodule::P1_CV+i));}
        for(int i=0;i<3;++i)addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(15+30*i,121)),module,Machinemodule::LEFT+i));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(6,6)),module,Machinemodule::READY));
        addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(85,6)),module,Machinemodule::LINK));
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
