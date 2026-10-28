// Test host supplies internal Rack context; plugin translation units use public API.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/Register.cpp"
#include "../src/Lookup.cpp"
#include "../src/Lookup2.cpp"
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
    assert(m.params.size()==8 && m.inputs.size()==7 && m.outputs.size()==2 && m.lights.size()==10);
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
// JUMP through the module: a step applies on its own sample, a gate steps on each rising edge,
// the selected bit is used, RESET clears and wins, and the steps survive save and reload.
// Library v2 through the module: the packaged plugin loads both libraries; the new families are
// FAMILY 7-9 and FAMILY CV reaches them; each library's identity is saved and checked on its own.
static void libraryV2Module() {
    ByteLookup m; outputs(m); m.model=modelLookup; auto a=args(); auto& lib=*m.library;
    assert(m.getParamQuantity(ByteLookup::FAMILY)->maxValue==10);
    m.params[ByteLookup::FAMILY].setValue(lu::Density); m.params[ByteLookup::SHAPE].setValue(20);
    for (int n=0;n<256;n+=3) { set(m.inputs[ByteLookup::BUS_IN],n); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==lib.table(lu::Density,20)[n]); }
    m.params[ByteLookup::FAMILY].setValue(lu::Rhythm); m.inputs[ByteLookup::FAMILY_CV].channels=1; m.inputs[ByteLookup::FAMILY_CV].setVoltage(10);
    set(m.inputs[ByteLookup::BUS_IN],0); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==lib.table(lu::Ratio,20)[0]); // clamps at 10
    m.inputs[ByteLookup::FAMILY_CV].setVoltage(2); m.process(a);
    m.params[ByteLookup::FAMILY].setValue(lu::Custom); m.inputs[ByteLookup::FAMILY_CV].setVoltage(1);                     // Custom + 1 V = Envelope
    set(m.inputs[ByteLookup::BUS_IN],100); m.process(a); assert(word(m.outputs[ByteLookup::BUS_OUT])==lib.table(lu::Envelope,20)[100]);
    m.inputs[ByteLookup::FAMILY_CV].channels=0;
    m.params[ByteLookup::FAMILY].setValue(lu::Envelope); m.params[ByteLookup::SHAPE].setValue(0); m.process(a);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==lib.table(lu::Envelope,0)[100]);
    json_t* saved=m.toJson(); json_t* data=json_object_get(saved,"data");
    assert(std::string(json_string_value(json_object_get(data,"library_v2_sha256")))==lu::LIBRARY_V2_SHA256);
    assert(std::string(json_string_value(json_object_get(data,"library_v3_sha256")))==lu::LIBRARY_V3_SHA256);
    {   // A patch naming another v3 library silences Ratio only.
        json_t* other=json_deep_copy(saved); json_object_set_new(json_object_get(other,"data"),"library_v3_sha256",json_string("another"));
        ByteLookup q; outputs(q); q.model=modelLookup; q.fromJson(other); json_decref(other);
        q.params[ByteLookup::FAMILY].setValue(lu::Ratio); set(q.inputs[ByteLookup::BUS_IN],100); q.process(a);
        assert(q.libraryV3Mismatch.load() && word(q.outputs[ByteLookup::BUS_OUT])==128);
        q.params[ByteLookup::FAMILY].setValue(lu::Envelope); q.process(a); assert(word(q.outputs[ByteLookup::BUS_OUT])==lib.table(lu::Envelope,0)[100]);
    }
    // A patch naming another v2 library: only Envelope, Sequence and Density go neutral.
    json_object_set_new(data,"library_v2_sha256",json_string("another"));
    ByteLookup r; outputs(r); r.model=modelLookup; r.fromJson(saved);
    set(r.inputs[ByteLookup::BUS_IN],100); r.process(a);
    assert(r.libraryV2Mismatch.load() && !r.libraryMismatch.load() && word(r.outputs[ByteLookup::BUS_OUT])==128);
    r.params[ByteLookup::FAMILY].setValue(lu::Curve); r.params[ByteLookup::SHAPE].setValue(32); r.process(a);
    assert(word(r.outputs[ByteLookup::BUS_OUT])==255-100);
    // A v1 mismatch leaves the v2 families playing; a patch from before 2.7 names no v2 library.
    json_object_set_new(data,"library_v2_sha256",json_string(lu::LIBRARY_V2_SHA256));
    json_object_set_new(data,"library_sha256",json_string("another")); r.fromJson(saved);
    r.params[ByteLookup::FAMILY].setValue(lu::Sequence); r.params[ByteLookup::SHAPE].setValue(5); r.process(a);
    assert(r.libraryMismatch.load() && word(r.outputs[ByteLookup::BUS_OUT])==lib.table(lu::Sequence,5)[100]);
    r.params[ByteLookup::FAMILY].setValue(lu::Curve); r.process(a); assert(word(r.outputs[ByteLookup::BUS_OUT])==128);
    json_object_del(data,"library_v2_sha256"); json_object_set_new(data,"library_sha256",json_string(lu::LIBRARY_SHA256));
    r.fromJson(saved); assert(!r.libraryV2Mismatch.load() && !r.libraryMismatch.load());
    json_decref(saved);
}

// Lookup2 through the module: A passes by default; B WORD, FAMILY CV and SHAPE CV; JUMP steps
// through the Logic functions; RESET clears them; the steps survive save; Initialize.
static void lookup2Module() {
    namespace l2 = dspemu::lookup2; using M = ByteLookup2;
    M m; outputs(m); m.model=modelLookup2; auto a=args();
    assert(m.params.size()==5 && m.inputs.size()==6 && m.outputs.size()==2 && m.lights.size()==9);
    for (int n=0;n<256;n+=7) { set(m.inputs[M::A_IN],n); m.process(a); assert(word(m.outputs[M::BUS_OUT])==n && m.outputs[M::AUDIO_OUT].getVoltage()==dspemu::bus::toVolts(n)); }
    m.params[M::SHAPE].setValue(1); m.params[M::B_WORD].setValue(0x0f); set(m.inputs[M::A_IN],0x3c); m.process(a);
    assert(word(m.outputs[M::BUS_OUT])==0x0c);                                                     // AND with B WORD
    set(m.inputs[M::B_IN],0xf0); m.process(a); assert(word(m.outputs[M::BUS_OUT])==0x30);           // B replaces the word
    m.inputs[M::FAMILY_CV].channels=1; m.inputs[M::FAMILY_CV].setVoltage(1); m.process(a);          // Arithmetic 1: mix
    assert(word(m.outputs[M::BUS_OUT])==l2::arithmetic(1,0x3c,0xf0));
    m.inputs[M::FAMILY_CV].setVoltage(9); m.process(a); assert(word(m.outputs[M::BUS_OUT])==m.engine.evaluate(l2::Walsh,1,0x3c,0xf0));
    m.inputs[M::FAMILY_CV].channels=0; m.inputs[M::SHAPE_CV].channels=1; m.inputs[M::SHAPE_CV].setVoltage(10./64*2); m.process(a);
    assert(word(m.outputs[M::BUS_OUT])==(0x3c^0xf0));                                               // Logic 1 + 2 = XOR
    m.inputs[M::SHAPE_CV].channels=0; m.params[M::SHAPE].setValue(0);
    // A gate into JUMP walks the sixteen functions; a clocked A holds between its edges.
    m.inputs[M::JUMP_IN].channels=1; m.inputs[M::JUMP_IN].setVoltage(0); m.process(a);     // connected low: no step
    for (int f=1;f<=20;++f) {
        m.inputs[M::JUMP_IN].setVoltage(10); set(m.inputs[M::A_IN],0x3c,true,true); m.process(a);
        assert(word(m.outputs[M::BUS_OUT])==l2::logic(f,0x3c,0xf0) && m.outputs[M::BUS_OUT].getChannels()==9);
        m.inputs[M::JUMP_IN].setVoltage(0); set(m.inputs[M::A_IN],0x00,true,false); m.process(a);
        assert(word(m.outputs[M::BUS_OUT])==l2::logic(f,0x3c,0xf0));
    }
    assert(m.jumpSteps.load()==20);
    json_t* saved=m.toJson(); M r; outputs(r); r.model=modelLookup2; r.fromJson(saved); json_decref(saved);
    assert(r.jumpSteps.load()==20 && r.params[M::B_WORD].getValue()==0x0f);
    m.inputs[M::RESET_IN].channels=1; m.inputs[M::RESET_IN].setVoltage(10); m.process(a); assert(m.jumpSteps.load()==0);
    m.onReset(Module::ResetEvent{}); assert(m.params[M::FAMILY].getValue()==0 && m.params[M::B_WORD].getValue()==0);
}

static void jump(std::shared_ptr<const lu::Library> library,float sr) {
    ByteLookup m(library); outputs(m); m.model=modelLookup; auto a=args(sr);
    m.params[ByteLookup::FAMILY].setValue(lu::Walsh); m.params[ByteLookup::SHAPE].setValue(5);
    auto walsh=[&](int shape,int address) { return library->table(lu::Walsh,shape&63)[address]; };
    // BUS IN and JUMP clocked together, as from one source. JUMP's first edge only arms.
    int steps=0; bool armed=false;
    for (int k=0;k<80;++k) {
        int address=(k*37)&255, jumpWord=(k%3==0 ? 0x04 : 0x00)|(k%5==0 ? 0x01 : 0x00);
        for (bool clk : {true,false}) {
            set(m.inputs[ByteLookup::BUS_IN],address,true,clk); set(m.inputs[ByteLookup::JUMP_IN],jumpWord,true,clk);
            m.params[ByteLookup::JUMP_BIT].setValue(2); m.process(a);
            if (clk) {
                bool step=armed && (jumpWord & 0x04);
                steps+=step; armed=true;
                assert(word(m.outputs[ByteLookup::BUS_OUT])==walsh(5+steps,address));
                if (step) assert(m.lights[ByteLookup::JUMP_LIGHT].getBrightness()>0);
            }
        }
    }
    assert(steps==26 && m.jumpSteps.load()==26);
    // A gate (one channel) is bit 0: each rising edge steps, a held gate does not.
    m.inputs[ByteLookup::JUMP_IN].channels=1; m.inputs[ByteLookup::JUMP_IN].setVoltage(0); m.params[ByteLookup::JUMP_BIT].setValue(0);
    m.process(a);
    for (int k=0;k<10;++k) {
        m.inputs[ByteLookup::JUMP_IN].setVoltage(10); for (int t=0;t<3;++t) m.process(a);
        m.inputs[ByteLookup::JUMP_IN].setVoltage(0); m.process(a);
    }
    assert(m.jumpSteps.load()==36);
    set(m.inputs[ByteLookup::BUS_IN],77,true,true); m.process(a); set(m.inputs[ByteLookup::BUS_IN],77,true,false); m.process(a);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==walsh(5+36,77));
    // Saved and restored; a patch from before 2.5 has none; saved values are bounded.
    json_t* saved=m.toJson(); assert(json_integer_value(json_object_get(json_object_get(saved,"data"),"jump"))==36);
    ByteLookup r(library); outputs(r); r.model=modelLookup; r.fromJson(saved);
    set(r.inputs[ByteLookup::BUS_IN],77,true,true); r.process(a); assert(word(r.outputs[ByteLookup::BUS_OUT])==walsh(5+36,77));
    json_object_del(json_object_get(saved,"data"),"jump"); r.fromJson(saved); assert(r.jumpSteps.load()==0);
    json_object_set_new(json_object_get(saved,"data"),"jump",json_integer(999)); r.fromJson(saved); assert(r.jumpSteps.load()==63);
    json_object_set_new(json_object_get(saved,"data"),"jump",json_integer(-4)); r.fromJson(saved); assert(r.jumpSteps.load()==0);
    json_decref(saved);
    // RESET clears the steps, and wins over a step on the same sample. Initialize clears them.
    m.inputs[ByteLookup::RESET_IN].channels=1;
    m.inputs[ByteLookup::JUMP_IN].setVoltage(10); m.inputs[ByteLookup::RESET_IN].setVoltage(10); m.process(a);
    assert(m.jumpSteps.load()==0);
    m.inputs[ByteLookup::JUMP_IN].setVoltage(0); m.inputs[ByteLookup::RESET_IN].setVoltage(0); m.process(a);
    m.inputs[ByteLookup::JUMP_IN].setVoltage(10); m.process(a); assert(m.jumpSteps.load()==1);
    m.onReset(Module::ResetEvent{}); assert(m.jumpSteps.load()==0 && m.params[ByteLookup::JUMP_BIT].getValue()==0);
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
// Review 2026-10-27 #26: a patch whose embedded table fails to load plays Custom as Identity,
// keeps the error for the menu, and saves the identity table.
static void damagedTableRegression(std::shared_ptr<const lu::Library> library) {
    auto a = args();
    auto damaged = [] {
        json_t* table = custom(); json_array_set_new(json_object_get(table,"values"),0,json_integer(7));
        json_t* root = json_object(); json_object_set_new(root,"custom",table); return root;
    };
    // A fresh module (patch load).
    ByteLookup m(library); outputs(m); m.model = modelLookup;
    json_t* patch = damaged(); m.dataFromJson(patch); json_decref(patch);
    assert(!m.tableError.empty() && m.uiTable && m.uiTable->name == "Identity");
    m.params[ByteLookup::FAMILY].setValue(lu::Custom); m.params[ByteLookup::SHAPE].setValue(32);
    set(m.inputs[ByteLookup::BUS_IN],0x42); m.process(a);
    assert(word(m.outputs[ByteLookup::BUS_OUT])==0x42);
    json_t* saved = m.dataToJson(); json_t* table = json_object_get(saved,"custom");
    assert(table && json_integer_value(json_array_get(json_object_get(table,"values"),0x42))==0x42);
    ByteLookup reopened(library); outputs(reopened); reopened.model = modelLookup;
    reopened.dataFromJson(saved); json_decref(saved);
    assert(reopened.tableError.empty() && reopened.uiTable && reopened.uiTable->name == "Identity");
    // A live module playing another table (preset load or undo) does not keep playing it.
    ByteLookup live(library); outputs(live); live.model = modelLookup;
    auto* good = custom(); live.loadJson(good,true); json_decref(good); live.process(a);
    set(live.inputs[ByteLookup::BUS_IN],0x42); live.process(a);
    assert(word(live.outputs[ByteLookup::BUS_OUT])==255-0x42);
    patch = damaged(); live.dataFromJson(patch); json_decref(patch);
    live.process(a); live.collect(); live.process(a);
    assert(!live.tableError.empty() && word(live.outputs[ByteLookup::BUS_OUT])==0x42);
}

static void firstLookupSamples(std::shared_ptr<const lu::Library> library) {
    for (int lifecycle = 0; lifecycle < 3; ++lifecycle) for (int busMode = 0; busMode < 3; ++busMode) {
        ByteLookup m(library); ByteLookup2 n; m.model = modelLookup; n.model = modelLookup2;
        outputs(m); outputs(n); auto a = args();
        if (lifecycle == 1) { m.onReset(Module::ResetEvent{}); n.onReset(Module::ResetEvent{}); }
        if (lifecycle == 2) {
            json_t* j = json_pack("{s:i}", "jump", 7);
            m.dataFromJson(j); n.dataFromJson(j); json_decref(j);
        }
        m.params[m.FAMILY].setValue(lu::Curve); m.params[m.SHAPE].setValue(0);
        set(m.inputs[m.BUS_IN], 0x42, busMode != 0, busMode == 2);
        set(n.inputs[n.A_IN], 0x42, busMode != 0, busMode == 2);
        set(n.inputs[n.B_IN], 0xa5, true, false);
        set(m.inputs[m.JUMP_IN], 1, busMode != 0, busMode == 2);
        set(n.inputs[n.JUMP_IN], 1, busMode != 0, busMode == 2);
        m.process(a); n.process(a);
        assert(m.jump.steps == (lifecycle == 2 ? 7 : 0));
        assert(n.jump.steps == (lifecycle == 2 ? 7 : 0));
        // Offset affects the operation, so compare against each module's actual evaluator.
        assert(word(m.outputs[m.BUS_OUT]) == (busMode == 1 ? 128 :
            lu::evaluate(*library, lu::Parameters{lu::Curve, double(m.jump.steps)}, 0x42, m.jump.steps)));
        assert(word(n.outputs[n.BUS_OUT]) == dspemu::lookup2::logic(n.jump.steps, 0x42, 0xa5));
        assert(m.outputs[m.BUS_OUT].getVoltage(8) == (busMode == 2 ? 10 : 0));
        assert(n.outputs[n.BUS_OUT].getVoltage(8) == (busMode == 2 ? 10 : 0));
    }
}

// Restoring into a running module must rearm RESET just as a fresh patch load does.
// Otherwise a pre-restore high RESET leaves the saved JUMP offset uncleared.
template<class M> static void restoreReset(M& m, bool button = false) {
    outputs(m); auto a = args();
    m.inputs[M::RESET_IN].channels = 1;
    m.inputs[M::RESET_IN].setVoltage(button ? 0 : 10);
    m.params[M::RESET_BUTTON].setValue(button ? 1 : 0);
    m.process(a); // detector is high before loading a preset/undo snapshot
    json_t* saved = json_pack("{s:i}", "jump", 7);
    m.dataFromJson(saved); json_decref(saved);
    m.process(a);
    assert(m.jump.steps == 0); // same as loading this snapshot into a fresh module
    // A held RESET is still edge-triggered: a later JUMP is allowed.
    set(m.inputs[M::JUMP_IN], 0); m.process(a);
    set(m.inputs[M::JUMP_IN], 1); m.process(a);
    assert(m.jump.steps == 1);
}

int main(int argc, char** argv) {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin plugin; plugin.slug="ByteBusLab"; plugin.version="2.1.0"; plugin.path=std::filesystem::absolute("..").string();
    plugin.addModel(modelLookup); plugin.addModel(modelLookup2); plugin.addModel(modelClockedRegister); pluginInstance=&plugin; rack::plugin::plugins.push_back(&plugin);
    auto library=std::make_shared<lu::Library>(lu::Library::load("../res/lookup-v1.bin"));
    if (argc == 2) {
        if (std::string(argv[1]) == "restore-reset-lookup") { ByteLookup m(library); restoreReset(m); }
        else { ByteLookup2 m; restoreReset(m); }
        return 0;
    }
    { ByteLookup m(library); restoreReset(m); restoreReset(m, true); ByteLookup2 n; restoreReset(n); restoreReset(n, true); }
    { ByteLookup packaged; assert(packaged.library->ready && packaged.library->readyV2 && packaged.library->readyV3 && packaged.libraryError.empty()); }
    firstLookupSamples(library);
    libraryV2Module();
    damagedTableRegression(library);
    adapter(library);
    lookup2Module();
    for (float sr : {44100.f,48000.f,96000.f}) jump(library,sr);
    auto expected=feedback(library,48000); assert(feedback(library,44100)==expected && feedback(library,96000)==expected);
    rack::plugin::plugins.clear();
    std::cout << "PASS: real Lookup ports/DAC, bus clock holds, phase CV, verified custom import, full patch restore, UI handoff, packaged assets, Register feedback/reset at 44.1/48/96 kHz, JUMP steps on the same sample, gates, bit choice, RESET priority and persistence, library v2 and v3 families, FAMILY CV and per-library identities, Lookup2 ports, words, CVs, JUMP walk and persistence\n";
}
