// Test host supplies internal Rack context; plugin translation units use public API.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Register.cpp"
#include "../src/Lookup.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>
Plugin* pluginInstance = nullptr;
namespace lu = dspemu::lookup;
static Module::ProcessArgs args(float sr = 48000) { Module::ProcessArgs a{}; a.sampleRate = sr; a.sampleTime = 1/sr; return a; }
static void outputs(Module& m) { for (auto& p : m.outputs) p.channels = 1; }
static void set(Input& p,uint8_t word,bool clocked = false,bool clk = false) {
    p.channels = clocked ? 9 : 8; dspemu::bus::Writer{}.write(p,word,clocked,clk);
}
static uint8_t word(Output& p) { uint8_t value = 0; for (int k=0;k<8;++k) if (p.getVoltage(k)>=1) value |= 1<<k; return value; }
static void copy(Output& src,Input& dest) {
    dest.channels = src.getChannels();
    for (int k=0;k<16;++k) dest.setVoltage(k<dest.channels ? src.getVoltage(k) : 0,k);
}
static json_t* custom() {
    json_t* root = json_object(); json_object_set_new(root,"format",json_string("ByteBusLab.LookupTable"));
    json_object_set_new(root,"version",json_integer(1)); json_object_set_new(root,"name",json_string("Original test inversion"));
    lu::Table table{}; json_t* values = json_array();
    for (int n=0;n<256;++n) { table[n] = 255-n; json_array_append_new(values,json_integer(table[n])); }
    json_object_set_new(root,"hash",json_string(lookupHash(table).c_str())); json_object_set_new(root,"values",values);
    json_object_set_new(root,"source",json_pack("{s:s}","kind","original-test-formula"));
    return root;
}
static void adapter(std::shared_ptr<const lu::Library> library) {
    ByteLookup m(library); outputs(m); m.model = modelLookup; auto a = args();
    assert(m.params.size()==7 && m.inputs.size()==6 && m.outputs.size()==2);
    m.params[ByteLookup::FAMILY].setValue(lu::Curve);
    for (int n=0;n<256;++n) {
        set(m.inputs[ByteLookup::BUS_IN],n); m.process(a);
        assert(word(m.outputs[ByteLookup::BUS_OUT])==n && m.outputs[ByteLookup::BUS_OUT].getChannels()==8);
        assert(m.outputs[ByteLookup::AUDIO_OUT].getVoltage()==dspemu::bus::toVolts(n));
    }
    m.inputs[ByteLookup::BUS_IN].channels=0; m.inputs[ByteLookup::SIGNAL_IN].channels=1;
    for (int n=0;n<256;++n) {
        m.inputs[ByteLookup::SIGNAL_IN].setVoltage(dspemu::bus::toVolts(n)); m.process(a);
        assert(word(m.outputs[ByteLookup::BUS_OUT])==n);
    }
    m.inputs[ByteLookup::SIGNAL_IN].setVoltage(NAN); m.process(a);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==128);
    // A connected bus owns the address even with an analog source still patched.
    m.inputs[ByteLookup::SIGNAL_IN].setVoltage(5);
    set(m.inputs[ByteLookup::BUS_IN],20,true,true); m.process(a);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==20 && m.outputs[ByteLookup::BUS_OUT].getChannels()==9);
    m.inputs[ByteLookup::SIGNAL_IN].channels=0;
    set(m.inputs[ByteLookup::BUS_IN],30,true,false); m.params[ByteLookup::SHAPE].setValue(32); m.process(a);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==20);
    set(m.inputs[ByteLookup::BUS_IN],30,true,true); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==225);
    m.params[ByteLookup::CLOCK_POLICY].setValue(1); m.process(a); assert(m.outputs[ByteLookup::BUS_OUT].getChannels()==8);
    m.inputs[ByteLookup::PHASE_CV].channels=1; m.inputs[ByteLookup::PHASE_CV].setVoltage(.5);
    m.params[ByteLookup::SHAPE].setValue(0); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==158);
    m.inputs[ByteLookup::PHASE_CV].setVoltage(NAN); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==30);
    m.inputs[ByteLookup::PHASE_CV].channels=0;
    auto* table=custom(); m.loadJson(table,true); m.process(a);
    assert(m.params[ByteLookup::FAMILY].getValue()==lu::Custom && m.params[ByteLookup::SHAPE].getValue()==32);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==225);
    json_t* saved=m.toJson(); ByteLookup restored(library); restored.model=modelLookup; outputs(restored);
    restored.fromJson(saved); json_decref(saved);
    set(restored.inputs[ByteLookup::BUS_IN],0x42); restored.process(a);
    assert(word(restored.outputs[ByteLookup::BUS_OUT])==(255-0x42));
    assert(restored.uiTable && restored.uiTable->source.find("original-test-formula")!=std::string::npos);
    // The shipped example was compiled by the standalone Vastish tool.
    restored.loadFile("../examples/vastish-sine.lookup.json"); restored.process(a);
    assert(restored.tableError.empty() && restored.uiTable->source.find("Vastish.memoryless")!=std::string::npos);
    set(restored.inputs[ByteLookup::BUS_IN],64); restored.process(a);
    assert(word(restored.outputs[ByteLookup::BUS_OUT])==0);
    set(restored.inputs[ByteLookup::BUS_IN],192); restored.process(a);
    assert(word(restored.outputs[ByteLookup::BUS_OUT])==255);
    // Reject damaged custom data without replacing the published table.
    json_object_set_new(table,"hash",json_string("wrong")); bool rejected=false;
    try { m.loadJson(table,true); } catch (const std::runtime_error&) { rejected=true; }
    assert(rejected); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==225);
    json_decref(table);
    // Pending UI updates are coalesced; old data is retired off the process path.
    for (int i=0;i<1000;++i) { auto* t=custom(); m.loadJson(t,false); json_decref(t); if (i%3==0) m.process(a); m.collect(); }
    m.process(a); m.collect(); m.process(a); assert(m.live && !m.pending.load());
    // A different builtin identity is visible as an error and a neutral output.
    json_t* different=json_pack("{s:s}","library_sha256","different"); m.dataFromJson(different); json_decref(different);
    m.params[ByteLookup::FAMILY].setValue(lu::Harmonic); m.process(a);
    assert(m.libraryMismatch.load() && word(m.outputs[ByteLookup::BUS_OUT])==128);
    m.inputs[ByteLookup::BUS_IN].channels=0; restored.inputs[ByteLookup::BUS_IN].channels=0; restored.process(a);
    assert(word(restored.outputs[ByteLookup::BUS_OUT])==128 && restored.outputs[ByteLookup::AUDIO_OUT].getVoltage()==0);
    ByteLookup tracker(library); outputs(tracker); tracker.params[ByteLookup::FAMILY].setValue(lu::Curve);
    tracker.params[ByteLookup::MODE].setValue(1);
    for (int t=0;t<512*5;++t) { set(tracker.inputs[ByteLookup::BUS_IN],uint8_t(32+(t%512)*192/512)); tracker.process(a); }
    assert(tracker.outputs[ByteLookup::BUS_OUT].getChannels()==8 && tracker.lights[ByteLookup::LOCK_LIGHT].getBrightness()==1);
    tracker.inputs[ByteLookup::RESET_IN].channels=1; tracker.inputs[ByteLookup::RESET_IN].setVoltage(10); tracker.process(a);
    assert(tracker.lights[ByteLookup::LOCK_LIGHT].getBrightness()==0);
}
static std::vector<uint8_t> feedback(std::shared_ptr<const lu::Library> library,float sr) {
    ClockedRegister r; ByteLookup l(library); outputs(r); outputs(l); auto a=args(sr); std::mt19937 order(17);
    r.params[ClockedRegister::CLOCK_MODE].setValue(1); r.params[ClockedRegister::RESET_WORD].setValue(0xa5);
    r.inputs[ClockedRegister::CLOCK_IN].channels=r.inputs[ClockedRegister::RESET_IN].channels=1;
    l.params[ByteLookup::FAMILY].setValue(lu::Curve); l.params[ByteLookup::SHAPE].setValue(32);
    std::vector<uint8_t> first,second;
    for (int run=0;run<2;++run) for (int t=0;t<32*12;++t) {
        copy(r.outputs[ClockedRegister::BUS_OUT],l.inputs[ByteLookup::BUS_IN]);
        copy(l.outputs[ByteLookup::BUS_OUT],r.inputs[ClockedRegister::BUS_IN]);
        r.inputs[ClockedRegister::RESET_IN].setVoltage(t==0 ? 10 : 0);
        r.inputs[ClockedRegister::CLOCK_IN].setVoltage(t%32==0 ? 10 : 0);
        if (order()&1) { r.process(a); l.process(a); } else { l.process(a); r.process(a); }
        if (r.outputs[ClockedRegister::CLOCK_OUT].getVoltage()==10) (run ? second : first).push_back(word(r.outputs[ClockedRegister::BUS_OUT]));
        ++a.frame;
    }
    assert(first==second && first.size()==12);
    for (size_t i=0;i<first.size();++i) assert(first[i]==(i&1 ? 0x5a : 0xa5));
    return first;
}
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug="ByteBusLab"; plugin.version="2.1.0"; plugin.path=std::filesystem::absolute("..").string();
    plugin.addModel(modelLookup); plugin.addModel(modelClockedRegister); pluginInstance=&plugin; rack::plugin::plugins.push_back(&plugin);
    auto library=std::make_shared<lu::Library>(lu::Library::load("../res/lookup-v1.bin"));
    { ByteLookup packaged; assert(packaged.library->ready && packaged.libraryError.empty()); }
    adapter(library);
    auto expected=feedback(library,48000); assert(feedback(library,44100)==expected && feedback(library,96000)==expected);
    rack::plugin::plugins.clear();
    std::cout << "PASS: real Lookup ports/DAC, bus clock holds, phase CV, verified custom import, full patch restore, UI handoff, packaged assets, Register feedback/reset at 44.1/48/96 kHz\n";
}
