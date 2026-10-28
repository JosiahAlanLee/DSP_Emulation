// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Adapter.hpp"
#include "Layout.hpp"
#include "../../shared/PanelWidgets.hpp"
#include <atomic>

struct ByteAdapter : Module {
    enum Params { INPUT_FORMAT, OUTPUT_FORMAT, CLOCK_MODE, RATE, KEEP, XOR, IDLE, CZ_TRACK,
        MAP0, MAP1, MAP2, MAP3, MAP4, MAP5, MAP6, MAP7, NUM_PARAMS };
    enum Inputs { BUS_IN, CLOCK_IN, RESET_IN, NUM_INPUTS };
    enum Outputs { BUS_OUT, CLOCK_OUT, GATES_OUT, TRIG_OUT, BIPOLAR_OUT, LEVEL_OUT, NUM_OUTPUTS };
    enum Lights { BIT0, BIT1, BIT2, BIT3, BIT4, BIT5, BIT6, BIT7, NUM_LIGHTS };
    dspemu::adapter::Engine engine;
    dspemu::bus::Reader reader;
    dspemu::bus::Writer writer;
    std::atomic<unsigned> displayed{0};
    std::atomic<int> requestedPreset{-1};
    // LOAD is a panel button rather than a parameter; the request is consumed once.
    std::atomic<bool> loadRequested{false};

    ByteAdapter() {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        const std::vector<std::string> formats={"Offset / raw","Signed linear","CZ encoded high byte","Yamaha encoded high byte"};
        configSwitch(INPUT_FORMAT,0,3,0,"Input byte format",formats);
        configSwitch(OUTPUT_FORMAT,0,3,0,"Output byte format",formats);
        configSwitch(CLOCK_MODE,0,3,0,"Acquisition clock",{"Follow bus","CLOCK jack (raw data)","Internal (raw data)","Engine rate (raw data)"});
        configParam(RATE,-3,11,3,"Internal rate"," Hz",2.f);
        configParam(KEEP,0,255,255,"Keep mask (AND)");
        configParam(XOR,0,255,0,"Invert mask (XOR)");
        configParam(IDLE,0,255,0,"Idle / reset word");
        configParam(CZ_TRACK,-.5f,.5f,0,"CZ decoding tracking"," %",0,100);
        for (int id : {KEEP,XOR,IDLE}) getParamQuantity(id)->snapEnabled=true;
        for (int bit=0;bit<8;++bit)
            configSwitch(MAP0+bit,-1,7,float(bit),"Output bit "+std::to_string(bit)+" reads",
                {"Off","Bit 0","Bit 1","Bit 2","Bit 3","Bit 4","Bit 5","Bit 6","Bit 7"});
        configInput(BUS_IN,"Byte bus; select its encoded or numeric format explicitly");
        configInput(CLOCK_IN,"Local acquisition clock (1 V / 0.1 V Schmitt)");
        configInput(RESET_IN,"Load IDLE and restart internal clock");
        configOutput(BUS_OUT,"Converted/routed byte, 8 channels or 9 with publication CLK");
        configOutput(CLOCK_OUT,"Publication clock; one-sample strobes, zero at engine rate");
        configOutput(GATES_OUT,"Eight held 0/10 V bit levels (no ninth clock)");
        configOutput(TRIG_OUT,"Eight per-publication triggers; per-bit rises at engine rate");
        configOutput(BIPOLAR_OUT,"Final byte as offset-binary +/-5 V; use Offset output for decoded audio");
        configOutput(LEVEL_OUT,"Final byte as 0-10 V (word / 255)");
    }
    int integer(int id,int lo,int hi,int fallback=0) {
        float v=params[id].getValue();
        return std::isfinite(v) ? int(std::round(std::clamp(v,float(lo),float(hi)))) : fallback;
    }
    void applyPreset(dspemu::adapter::Preset choice) {
        auto p=dspemu::adapter::preset(choice);
        params[INPUT_FORMAT].setValue(float(p.input)); params[OUTPUT_FORMAT].setValue(float(p.output));
        params[CLOCK_MODE].setValue(float(p.clock)); params[RATE].setValue(float(std::log2(p.rateHz)));
        params[KEEP].setValue(p.keep); params[XOR].setValue(p.flip); params[IDLE].setValue(p.idle);
        params[CZ_TRACK].setValue(p.tracking);
        for (int bit=0;bit<8;++bit) params[MAP0+bit].setValue(float(p.route[bit]));
        reader.reset(); engine.clear(p.idle); loadRequested=false;
    }
    void clear() {
        reader.reset(); engine.clear(uint8_t(integer(IDLE,0,255)));
        requestedPreset=-1; loadRequested=false; displayed=0;
    }
    void onReset(const ResetEvent& e) override { Module::onReset(e); clear(); }
    void onSampleRateChange(const SampleRateChangeEvent& e) override { clear(); }
    json_t* dataToJson() override { return json_object(); }
    void dataFromJson(json_t*) override { clear(); }
    void process(const ProcessArgs& args) override {
        int selected=requestedPreset.exchange(-1,std::memory_order_acq_rel);
        if (selected>=0 && selected<int(dspemu::adapter::Preset::Count))
            applyPreset(dspemu::adapter::Preset(selected));
        dspemu::adapter::Parameters p;
        p.input=dspemu::adapter::Format(integer(INPUT_FORMAT,0,3));
        p.output=dspemu::adapter::Format(integer(OUTPUT_FORMAT,0,3));
        p.clock=dspemu::adapter::Clock(integer(CLOCK_MODE,0,3));
        float rate=params[RATE].getValue(), tracking=params[CZ_TRACK].getValue();
        p.rateHz=std::exp2(std::isfinite(rate) ? std::clamp(double(rate),-3.,11.) : 3.);
        p.tracking=std::isfinite(tracking) ? std::clamp(tracking,-.5f,.5f) : 0.f;
        p.keep=uint8_t(integer(KEEP,0,255,255)); p.flip=uint8_t(integer(XOR,0,255));
        p.idle=uint8_t(integer(IDLE,0,255));
        for (int bit=0;bit<8;++bit) p.route[bit]=integer(MAP0+bit,-1,7,bit);
        dspemu::clock_owner::Inputs in;
        in.data=reader.process(inputs[BUS_IN]);
        in.clockConnected=inputs[CLOCK_IN].isConnected(); in.clock=inputs[CLOCK_IN].getVoltage();
        in.reset=inputs[RESET_IN].getVoltage(); in.resetButton=loadRequested.exchange(false);
        auto out=engine.tick(p,in,args.sampleRate);
        writer.write(outputs[BUS_OUT],out.word,out.clocked,out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? 10.f : 0.f);
        outputs[GATES_OUT].setChannels(8); outputs[TRIG_OUT].setChannels(8);
        for (int bit=0;bit<8;++bit) {
            bool high=(out.word & (1u<<bit))!=0;
            outputs[GATES_OUT].setVoltage(high ? 10.f : 0.f,bit);
            outputs[TRIG_OUT].setVoltage(out.triggers & (1u<<bit) ? 10.f : 0.f,bit);
            lights[BIT0+bit].setBrightness(high ? 1.f : 0.f);
        }
        outputs[BIPOLAR_OUT].setVoltage(out.volts); outputs[LEVEL_OUT].setVoltage(out.level);
        displayed.store(unsigned(in.data.rawWord) | unsigned(out.word)<<8,std::memory_order_relaxed);
    }
};

struct AdapterDisplay : widget::Widget {
    ByteAdapter* module=nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font=APP->window->uiFont; if (!font) return;
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,16);
        nvgTextAlign(args.vg,NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(143,220,196));
        unsigned shown=module ? module->displayed.load(std::memory_order_relaxed) : 0;
        nvgText(args.vg,box.size.x/2,box.size.y/2,string::f("IN %02X    OUT %02X",shown&255,shown>>8).c_str(),nullptr);
    }
};
struct AdapterLoad : app::SvgButton {
    ByteAdapter* module=nullptr;
    AdapterLoad() { addFrame(Svg::load(asset::system("res/ComponentLibrary/TL1105_0.svg"))); }
    void onAction(const event::Action& e) override { if (module) module->loadRequested=true; }
};
struct ByteAdapterWidget : ModuleWidget {
    ByteAdapterWidget(ByteAdapter* module) {
        using namespace bytebus_layout::adapter;
#define POS(N) Vec(N##_X,N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/Adapter.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0)));
        addChild(createWidget<ScrewSilver>(Vec(WIDTH-15,365)));
        auto* display=createWidget<AdapterDisplay>(POS(WORD)); display->box.size=Vec(WORD_W,WORD_H);
        display->module=module; addChild(display);
        const int ids[]={ByteAdapter::INPUT_FORMAT,ByteAdapter::OUTPUT_FORMAT,ByteAdapter::CLOCK_MODE};
        const Vec positions[]={POS(INPUT_FORMAT),POS(OUTPUT_FORMAT),POS(SOURCE)};
        const Vec sizes[]={Vec(INPUT_FORMAT_W,INPUT_FORMAT_H),Vec(OUTPUT_FORMAT_W,OUTPUT_FORMAT_H),Vec(SOURCE_W,SOURCE_H)};
        for (int i=0;i<3;++i) {
            auto* choice=createParam<dspemu::panel::Choice>(positions[i],module,ids[i]); choice->box.size=sizes[i];
            choice->labels=i==2 ? std::vector<std::string>{"FOLLOW BUS","CLOCK JACK","INTERNAL","ENGINE"} :
                std::vector<std::string>{"OFFSET / RAW","SIGNED","CZ ENCODED","YM ENCODED"};
            addParam(choice);
        }
        const Vec knobs[]={POS(RATE),POS(KEEP),POS(XOR),POS(IDLE)};
        const int knobIds[]={ByteAdapter::RATE,ByteAdapter::KEEP,ByteAdapter::XOR,ByteAdapter::IDLE};
        for (int i=0;i<4;++i) addParam(createParamCentered<RoundBlackKnob>(knobs[i],module,knobIds[i]));
        auto* load=createWidgetCentered<AdapterLoad>(POS(LOAD)); load->module=module; addChild(load);
        const Vec ins[]={POS(BUS_IN),POS(CLOCK_IN),POS(RESET_IN)};
        const Vec outs[]={POS(BUS_OUT),POS(CLOCK_OUT),POS(GATES_OUT),POS(TRIG_OUT),POS(BIPOLAR_OUT),POS(LEVEL_OUT)};
        for (int i=0;i<3;++i) addInput(createInputCentered<PJ301MPort>(ins[i],module,i));
        for (int i=0;i<6;++i) addOutput(createOutputCentered<DarkPJ301MPort>(outs[i],module,i));
        const Vec bits[]={POS(BIT0),POS(BIT1),POS(BIT2),POS(BIT3),POS(BIT4),POS(BIT5),POS(BIT6),POS(BIT7)};
        for (int bit=0;bit<8;++bit) addChild(createLightCentered<SmallLight<GreenLight>>(bits[bit],module,ByteAdapter::BIT0+bit));
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* m=getModule<ByteAdapter>(); if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createSubmenuItem("Adapter preset","",[m](Menu* child) {
            const char* labels[]={"Raw byte relay","CZish / CZ DAC -> standard audio","Yamaha DAC -> standard audio",
                "Linear DAC -> standard audio","Standard -> CZ encoded word","Standard -> Yamaha encoded word",
                "Gorgon hits -> drum triggers","Active lanes -> Gorgon mutes","Slow address / mask sampler",
                "Centered control -> XOR mask"};
            for (int i=0;i<int(dspemu::adapter::Preset::Count);++i)
                child->addChild(createMenuItem(labels[i],"",[m,i]{m->requestedPreset.store(i,std::memory_order_release);}));
        }));
        menu->addChild(createSubmenuItem("Bit routing (after format conversion)","",[m](Menu* child) {
            for (int bit=0;bit<8;++bit) child->addChild(createSubmenuItem("Output bit "+std::to_string(bit),"",[m,bit](Menu* sources) {
                for (int from=-1;from<8;++from) sources->addChild(createCheckMenuItem(from<0 ? "Off" : "Input bit "+std::to_string(from),"",
                    [m,bit,from]{return m->integer(ByteAdapter::MAP0+bit,-1,7,bit)==from;},
                    [m,bit,from]{m->params[ByteAdapter::MAP0+bit].setValue(float(from));}));
            }));
        }));
        menu->addChild(createSubmenuItem("CZ decoding tracking","",[m](Menu* child) {
            auto* slider=new ui::Slider; slider->box.size.x=220;
            slider->quantity=m->getParamQuantity(ByteAdapter::CZ_TRACK); child->addChild(slider);
        }));
    }
};
Model* modelAdapter=createModel<ByteAdapter,ByteAdapterWidget>("Adapter");
