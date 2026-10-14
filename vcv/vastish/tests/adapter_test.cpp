// Direct adapter test for audio-thread program requests and held-gate Panic.
#include "../src/Vastish.cpp"
#include <cassert>
#include <iostream>
Plugin* pluginInstance=nullptr;
int main() {
    Vastish m; Module::ProcessArgs args; args.sampleRate=48000; args.sampleTime=1.f/48000;
    m.inputs[Vastish::PITCH_IN].channels=m.inputs[Vastish::GATE_IN].channels=1;
    for(auto& o:m.outputs) o.channels=1;
    m.process(args); m.inputs[Vastish::GATE_IN].setVoltage(10);
    for(int i=0;i<1000;++i) m.process(args);
    assert(m.engine.voices[0].sources[11]>.1f);
    m.requestedPanic=true; m.process(args);
    for(int i=0;i<1000;++i) { m.process(args); assert(m.outputs[0].getVoltage()==0); }
    m.inputs[Vastish::GATE_IN].setVoltage(0); m.process(args);
    m.inputs[Vastish::GATE_IN].setVoltage(10); for(int i=0;i<1000;++i) m.process(args);
    assert(m.engine.voices[0].sources[11]>.1f);
    for(int pg=0;pg<6;++pg) { m.requestedProgram=pg; m.process(args); for(int i=0;i<PARAM_COUNT;++i) assert(m.params[i].getValue()==programs()[pg][i]); assert(m.displayedProgram==pg); }
    // Exercise broad editor endpoints across functions with maximal modulation.
    for(int type=0;type<=14;++type) {
        for(int i=0;i<PARAM_COUNT;++i) if(metadata()[i].options.empty()) m.params[i].setValue(metadata()[i].hi);
        m.params[F1_TYPE].setValue(type); m.params[F2_TYPE].setValue(type); m.params[F3_TYPE].setValue(type);
        for(int i=0;i<100;++i) { m.process(args); assert(std::isfinite(m.outputs[0].getVoltage())); }
    }
    std::cout<<"PASS: audio-thread program loads, held-gate Panic, retrigger, editor endpoints\n";
}
