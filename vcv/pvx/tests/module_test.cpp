// SPDX-License-Identifier: BSD-3-Clause
// Real SDK wrapper checks, without a Rack window or audio device.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/plugin.cpp"
#include <cassert>
#include <cstdio>
int main(){
    PVX m;m.model=modelPVX;
    for(auto& output:m.outputs)output.channels=1;
    rack::engine::Module::ProcessArgs args{};args.sampleRate=48000;args.sampleTime=1/48000.f;
    assert(m.params.size()==26&&m.inputs.size()==5&&m.outputs.size()==5);
    m.inputs[PVX::PITCH].channels=8;m.inputs[PVX::GATE].channels=1;
    m.inputs[PVX::GATE].setVoltage(10);
    for(int c=0;c<8;++c)m.inputs[PVX::PITCH].setVoltage(c*.25f,c);
    float peak=0;
    for(int i=0;i<4800;++i){m.process(args);for(int c=0;c<8;++c){float x=m.outputs[PVX::AUDIO].getVoltage(c);assert(std::isfinite(x));peak=std::max(peak,std::abs(x));}}
    assert(peak>.01f);for(auto& output:m.outputs)assert(output.getChannels()==8);
    m.inputs[PVX::PITCH].channels=16;m.process(args);assert(m.outputs[PVX::AUDIO].getChannels()==8);
    m.inputs[PVX::PITCH].channels=1;for(int i=0;i<1000;++i)m.process(args);
    assert(m.outputs[PVX::AUDIO].getChannels()==1);
    m.inputs[PVX::PITCH].channels=8;m.params[PVX::RESONANCE].setValue(1);
    m.params[PVX::CUTOFF].setValue(std::log2(4000.f));
    for(int i=0;i<4800;++i){m.process(args);for(int c=0;c<8;++c){float x=m.outputs[PVX::AUDIO].getVoltage(c);assert(std::isfinite(x)&&std::abs(x)<14.f);}}
    m.inputs[PVX::CUTOFF_CV].channels=1;m.inputs[PVX::CUTOFF_CV].setVoltage(std::nanf(""));
    m.inputs[PVX::RESONANCE_CV].channels=1;m.inputs[PVX::RESONANCE_CV].setVoltage(1e30f);
    for(int i=0;i<100;++i)m.process(args);
    assert(std::isfinite(m.outputs[PVX::AUDIO].getVoltage()));
    m.engine.reset();assert(m.engine.deviceFrames==0);
    std::puts("PASS real Rack SDK: 8 poly voices, mono-gate broadcast, 16-channel cap, shrink/reexpand, full resonance, invalid CV, reset");
}
