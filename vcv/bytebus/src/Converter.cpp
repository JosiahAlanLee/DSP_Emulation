// SPDX-License-Identifier: BSD-3-Clause
#include "plugin.hpp"
#include "engine/Converter.hpp"
#include "Layout.hpp"
#include <atomic>

struct ByteConverter : Module {
    enum Params { ADC_GAIN,ADC_OFFSET,ADC_RANGE,DAC_GAIN,DAC_OFFSET,DAC_RANGE,ADC_CLOCK,DAC_CLOCK,NUM_PARAMS };
    enum Inputs { SIGNAL_IN,CLOCK_IN,BUS_IN,NUM_INPUTS };
    enum Outputs { BUS_OUT,CLOCK_OUT,SIGNAL_OUT,NUM_OUTPUTS };
    enum Lights { CLIP_LIGHT,NUM_LIGHTS };
    dspemu::converter::Engine engine;
    dspemu::bus::Reader reader; dspemu::bus::Writer writer;
    std::atomic<unsigned> displayedAdc{128},displayedDac{128};
    ByteConverter() {
        config(NUM_PARAMS,NUM_INPUTS,NUM_OUTPUTS,NUM_LIGHTS);
        configParam(ADC_GAIN,-4,4,1,"ADC gain","x"); configParam(ADC_OFFSET,-10,10,0,"ADC offset"," V");
        configSwitch(ADC_RANGE,0,1,0,"ADC range",{"±5 V","±10 V"});
        configParam(DAC_GAIN,-4,4,1,"DAC gain","x"); configParam(DAC_OFFSET,-10,10,0,"DAC offset"," V");
        configSwitch(DAC_RANGE,0,1,0,"DAC range",{"±5 V","±10 V"});
        configSwitch(ADC_CLOCK,0,2,0,"ADC clock",{"Auto (CLOCK jack, otherwise engine)","Engine rate","CLOCK jack"});
        configSwitch(DAC_CLOCK,0,2,0,"DAC clock",{"Follow bus CLK","Engine rate (raw lines)","CLOCK jack (raw lines)"});
        configInput(SIGNAL_IN,"ADC signal (first channel)"); configInput(CLOCK_IN,"Acquisition clock, rising at 1 V");
        configInput(BUS_IN,"DAC byte bus; optional CLK on channel 9");
        configOutput(BUS_OUT,"ADC byte bus (8 or 9 channels)"); configOutput(CLOCK_OUT,"ADC acquisition clock");
        configOutput(SIGNAL_OUT,"DAC signal with output gain/offset");
    }
    void process(const ProcessArgs&) override {
        using namespace dspemu::converter;
        Parameters p;
        p.adcGain=params[ADC_GAIN].getValue(); p.adcOffset=params[ADC_OFFSET].getValue();
        p.dacGain=params[DAC_GAIN].getValue(); p.dacOffset=params[DAC_OFFSET].getValue();
        p.adcWide=params[ADC_RANGE].getValue()>=.5f; p.dacWide=params[DAC_RANGE].getValue()>=.5f;
        p.adcClock=AdcClock(int(std::round(trim(params[ADC_CLOCK].getValue(),0,0,2))));
        p.dacClock=DacClock(int(std::round(trim(params[DAC_CLOCK].getValue(),0,0,2))));
        auto out=engine.tick(p,inputs[SIGNAL_IN].getVoltage(),reader.process(inputs[BUS_IN]),
            inputs[CLOCK_IN].isConnected(),inputs[CLOCK_IN].getVoltage());
        writer.write(outputs[BUS_OUT],out.adc,out.clocked,out.clk);
        outputs[CLOCK_OUT].setVoltage(out.clk ? 10.f : 0.f); outputs[SIGNAL_OUT].setVoltage(out.voltage);
        displayedAdc.store(out.adc,std::memory_order_relaxed); displayedDac.store(out.dac,std::memory_order_relaxed);
        lights[CLIP_LIGHT].setBrightness(out.clip ? 1.f : 0.f);
    }
    void onReset(const ResetEvent& e) override {
        Module::onReset(e); engine.reset(); reader.reset(); displayedAdc.store(128); displayedDac.store(128);
    }
};
struct ConverterDisplay : widget::Widget {
    ByteConverter* module=nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,3);
        nvgFillColor(args.vg,nvgRGB(14,20,23)); nvgFill(args.vg);
        auto font=APP->window->uiFont; if(!font) return;
        nvgFontFaceId(args.vg,font->handle); nvgFontSize(args.vg,16); nvgTextAlign(args.vg,NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg,nvgRGB(143,220,196));
        using namespace bytebus_layout::converter;
        nvgText(args.vg,ADC_GAIN_X-WORD_X,box.size.y/2,string::f("%02X",module ? module->displayedAdc.load() : 128).c_str(),nullptr);
        nvgText(args.vg,DAC_GAIN_X-WORD_X,box.size.y/2,string::f("%02X",module ? module->displayedDac.load() : 128).c_str(),nullptr);
    }
};
struct ByteConverterWidget : ModuleWidget {
    ByteConverterWidget(ByteConverter* module) {
        using namespace bytebus_layout::converter;
#define POS(N) Vec(N##_X, N##_Y)
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/Converter.svg")));
        addChild(createWidget<ScrewSilver>(Vec(0,0))); addChild(createWidget<ScrewSilver>(Vec(bytebus_layout::converter::WIDTH-15,365)));
        auto* display=createWidget<ConverterDisplay>(POS(WORD)); display->box.size=Vec(WORD_W,WORD_H); display->module=module; addChild(display);
        const Vec gains[] = {POS(ADC_GAIN),POS(DAC_GAIN)}, offsets[] = {POS(ADC_OFFSET),POS(DAC_OFFSET)}, ranges[] = {POS(ADC_RANGE),POS(DAC_RANGE)};
        for(int side=0;side<2;++side) {
            int base=side ? ByteConverter::DAC_GAIN : ByteConverter::ADC_GAIN;
            addParam(createParamCentered<RoundBlackKnob>(gains[side],module,base));
            addParam(createParamCentered<RoundBlackKnob>(offsets[side],module,base+1));
            addParam(createParamCentered<CKSS>(ranges[side],module,base+2));
        }
        addChild(createLightCentered<SmallLight<RedLight>>(POS(CLIP),module,ByteConverter::CLIP_LIGHT));
        const Vec ins[] = {POS(SIGNAL_IN),POS(CLOCK_IN),POS(BUS_IN)}, outs[] = {POS(BUS_OUT),POS(CLOCK_OUT),POS(SIGNAL_OUT)};
        for(int i=0;i<3;++i) {
            addInput(createInputCentered<PJ301MPort>(ins[i],module,i));
            addOutput(createOutputCentered<DarkPJ301MPort>(outs[i],module,i));
        }
#undef POS
    }
    void appendContextMenu(Menu* menu) override {
        auto* m=getModule<ByteConverter>(); if(!m) return;
        menu->addChild(new MenuSeparator);
        for(int param:{ByteConverter::ADC_CLOCK,ByteConverter::DAC_CLOCK}) menu->addChild(createSubmenuItem(m->getParamQuantity(param)->name,"",[m,param](Menu* child) {
            const char* adc[]={"Auto (CLOCK jack, otherwise engine)","Engine rate","CLOCK jack"};
            const char* dac[]={"Follow bus CLK","Engine rate (raw lines)","CLOCK jack (raw lines)"};
            for(int i=0;i<3;++i) child->addChild(createCheckMenuItem(param==ByteConverter::ADC_CLOCK ? adc[i] : dac[i],"",
                [m,param,i]{return int(m->params[param].getValue())==i;},[m,param,i]{m->params[param].setValue(float(i));}));
        }));
    }
};
Model* modelConverter=createModel<ByteConverter,ByteConverterWidget>("Converter");
