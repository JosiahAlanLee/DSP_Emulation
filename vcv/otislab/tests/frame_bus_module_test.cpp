// The real Rack adapters, including Register -> Lookup -> stereo OTIS.
#include <engine/Engine.hpp>
#include <context.hpp>
#include <plugin.hpp>
#undef PRIVATE
#include "../src/OtisSampler.cpp"
#include "../../bytebus/src/Register.cpp"
#include "../../bytebus/src/Lookup.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <random>
Plugin* pluginInstance=nullptr;
namespace ft=otis::frame_table;
namespace lu=dspemu::lookup;
static Module::ProcessArgs args(float sr=48000) { Module::ProcessArgs a{}; a.sampleRate=sr; a.sampleTime=1/sr; return a; }
static void set(Input& p,uint8_t word,bool clocked=false,bool clk=false) {
    p.channels=clocked ? 9 : 8; dspemu::bus::Writer{}.write(p,word,clocked,clk);
}
static void copy(Output& src,Input& dst) {
    dst.channels=src.getChannels();
    for(int i=0;i<16;++i) dst.setVoltage(i<dst.channels ? src.getVoltage(i) : 0,i);
}
static void outputs(Module& m) { for(auto& p:m.outputs) p.channels=1; }
static void sample(OtisSampler& m,size_t count=256) {
    std::vector<int16_t> left(count*256),right(left.size());
    for(size_t i=0;i<left.size();++i) { left[i]=int16_t((int(i%256)-128)*256); right[i]=-16384; }
    auto image=otis::stereoImage(left.data(),right.data(),left.size());
    m.engine.install(image,otis::captureRate(5),left.size(),true);
    m.params[OtisSampler::WINDOW_MODE].setValue(2); m.params[OtisSampler::SIZE].setValue(8);
}
static void adapter(std::shared_ptr<const lu::Library> library) {
    auto m=std::make_unique<OtisSampler>(); sample(*m); outputs(*m); m->model=modelOtisSampler;
    auto a=args(); assert(m->inputs.size()==17 && m->params.size()==20 && m->outputs.size()==4);
    m->params[OtisSampler::POSITION].setValue(1);
    m->inputs[OtisSampler::POSITION_IN].channels=1; m->inputs[OtisSampler::POSITION_IN].setVoltage(10);
    for(int n=0;n<256;++n) {
        set(m->inputs[OtisSampler::FRAME_BUS_IN],n); m->process(a);
        auto w=m->engine.windowFor(0); assert(w.begin==size_t(n)*256 && w.end-w.begin==256);
        assert(m->engine.controls.channels==1); // bit lanes and CLK do not establish voices
    }
    set(m->inputs[OtisSampler::FRAME_BUS_IN],0xa5,true,true); m->process(a);
    set(m->inputs[OtisSampler::FRAME_BUS_IN],0x5a,true,false); m->process(a);
    for(int t=0;t<1000;++t) m->process(a);
    assert(m->engine.windowFor(0).begin==0xa5*256 && m->engine.controls.channels==1);
    set(m->inputs[OtisSampler::FRAME_BUS_IN],0x5a,true,true); m->process(a);
    assert(m->engine.windowFor(0).begin==0x5a*256);
    m->inputs[OtisSampler::FRAME_BUS_IN].channels=1;
    m->inputs[OtisSampler::FRAME_BUS_IN].setVoltage(10); m->process(a); assert(m->engine.windowFor(0).begin==256);
    m->inputs[OtisSampler::FRAME_BUS_IN].setVoltage(.5); m->process(a); assert(m->engine.windowFor(0).begin==256);
    m->inputs[OtisSampler::FRAME_BUS_IN].setVoltage(NAN); m->process(a); assert(m->engine.windowFor(0).begin==0);
    m->inputs[OtisSampler::FRAME_BUS_IN].channels=0; m->process(a); assert(m->engine.windowFor(0).begin==255*256);
    set(m->inputs[OtisSampler::FRAME_BUS_IN],0); m->params[OtisSampler::WINDOW_MODE].setValue(1); m->process(a);
    assert(m->engine.windowFor(0).begin==255*256); // SCAN uses POSITION
    m->params[OtisSampler::WINDOW_MODE].setValue(2); set(m->inputs[OtisSampler::FRAME_BUS_IN],0x42); m->process(a);
    auto* table=m->frameTableJson(ft::Channel::Left);
    assert(std::string(json_string_value(json_object_get(table,"hash")))=="fnv1a64:4242dc5249c33625");
    auto* source=json_object_get(table,"source");
    assert(json_integer_value(json_object_get(source,"begin"))==0x42*256);
    assert(json_integer_value(json_object_get(source,"length"))==256);
    ByteLookup l(library); l.model=modelLookup; outputs(l); l.loadJson(table,true); l.process(a); json_decref(table);
    for(int n=0;n<256;++n) {
        set(l.inputs[ByteLookup::BUS_IN],n); l.process(a);
        assert(l.outputs[ByteLookup::AUDIO_OUT].getVoltage()==dspemu::bus::toVolts(n));
    }
    table=m->frameTableJson(ft::Channel::Right); l.loadJson(table,true); json_decref(table); l.process(a);
    assert(l.outputs[ByteLookup::AUDIO_OUT].getVoltage()==-2.5);
    table=m->frameTableJson(ft::Channel::Mix);
    assert(json_integer_value(json_array_get(json_object_get(table,"values"),128))==96); json_decref(table);
    auto path=std::filesystem::temp_directory_path()/"otis-frame-bus-test.lookup.json";
    m->saveFrameTable(path.string(),ft::Channel::Left); l.loadFile(path.string()); std::filesystem::remove(path);
    assert(m->lastError.empty() && l.tableError.empty()); l.process(a);
    auto* lookupPatch=l.toJson(); ByteLookup restored(library); restored.model=modelLookup; outputs(restored);
    restored.fromJson(lookupPatch); json_decref(lookupPatch); set(restored.inputs[ByteLookup::BUS_IN],57); restored.process(a);
    assert(restored.uiTable && restored.uiTable->source.find("OTIS.frame")!=std::string::npos);
    assert(restored.outputs[ByteLookup::AUDIO_OUT].getVoltage()==dspemu::bus::toVolts(57));
    m->smoothFrameBus=false; auto* patch=m->toJson(); auto again=std::make_unique<OtisSampler>(); again->model=modelOtisSampler;
    again->fromJson(patch); json_decref(patch); assert(!again->smoothFrameBus && again->engine.sample.stereo);
    auto* old=json_object(); again->dataFromJson(old); json_decref(old); assert(again->smoothFrameBus);
    // No partial output is written when the selection is invalid.
    m->params[OtisSampler::WINDOW_MODE].setValue(0); m->process(a); bool rejected=false;
    try { table=m->frameTableJson(ft::Channel::Left); json_decref(table); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected);
    m->params[OtisSampler::WINDOW_MODE].setValue(2); m->process(a); m->engine.startRecord(); rejected=false;
    try { table=m->frameTableJson(ft::Channel::Left); json_decref(table); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected); m->engine.stopRecord();
}
static std::vector<size_t> chain(std::shared_ptr<const lu::Library> library,float sr) {
    auto o=std::make_unique<OtisSampler>(); sample(*o,8); outputs(*o); o->smoothFrameBus=false;
    o->engine.setHostRate(sr); o->params[OtisSampler::LOOP].setValue(1); o->engine.trigger(0);
    ClockedRegister r; ByteLookup l(library); outputs(r); outputs(l); auto a=args(sr); std::mt19937 order(17);
    r.params[ClockedRegister::CLOCK_MODE].setValue(1); r.params[ClockedRegister::RESET_WORD].setValue(0xa5);
    r.inputs[ClockedRegister::CLOCK_IN].channels=r.inputs[ClockedRegister::RESET_IN].channels=1;
    l.params[ByteLookup::FAMILY].setValue(lu::Curve); l.params[ByteLookup::SHAPE].setValue(32);
    std::vector<size_t> first,second; bool previousClock=false;
    for(int run=0;run<2;++run) for(int t=0;t<32*12;++t) {
        copy(r.outputs[ClockedRegister::BUS_OUT],l.inputs[ByteLookup::BUS_IN]);
        copy(l.outputs[ByteLookup::BUS_OUT],r.inputs[ClockedRegister::BUS_IN]);
        copy(r.outputs[ClockedRegister::BUS_OUT],o->inputs[OtisSampler::FRAME_BUS_IN]);
        bool clock=o->inputs[OtisSampler::FRAME_BUS_IN].getVoltage(8)>=1;
        bool edge=clock && !previousClock; previousClock=clock;
        r.inputs[ClockedRegister::RESET_IN].setVoltage(t==0 ? 10 : 0);
        r.inputs[ClockedRegister::CLOCK_IN].setVoltage(t%32==0 ? 10 : 0);
        Module* modules[]={&r,&l,o.get()}; std::shuffle(std::begin(modules),std::end(modules),order);
        for(auto* m:modules) m->process(a);
        if(edge) (run ? second : first).push_back(o->engine.windowFor(0).begin/256);
        assert(std::isfinite(o->outputs[0].getVoltage()) && std::isfinite(o->outputs[1].getVoltage()));
        ++a.frame;
    }
    assert(first==second && first.size()==12);
    for(size_t i=0;i<first.size();++i) assert(first[i]==(i&1 ? 2u : 5u));
    return first;
}
static void example(std::shared_ptr<const lu::Library> library) {
    json_t* root=json_load_file("examples/OTIS-byte-frames.vcv",JSON_REJECT_DUPLICATES,nullptr); assert(root);
    auto* modules=json_object_get(root,"modules"); assert(json_array_size(modules)==4);
    ClockedRegister r; r.model=modelClockedRegister; ByteLookup l(library); l.model=modelLookup;
    auto o=std::make_unique<OtisSampler>(); o->model=modelOtisSampler;
    r.fromJson(json_array_get(modules,0)); l.fromJson(json_array_get(modules,1)); o->fromJson(json_array_get(modules,2));
    json_decref(root); outputs(r); outputs(l); outputs(*o);
    assert(o->engine.frames==8*256 && o->engine.sample.stereo && o->smoothFrameBus);
    auto a=args(); double energy=0,difference=0; bool seen2=false,seen5=false;
    for(int t=0;t<60000;++t) {
        copy(r.outputs[ClockedRegister::BUS_OUT],l.inputs[ByteLookup::BUS_IN]);
        copy(l.outputs[ByteLookup::BUS_OUT],r.inputs[ClockedRegister::BUS_IN]);
        copy(r.outputs[ClockedRegister::BUS_OUT],o->inputs[OtisSampler::FRAME_BUS_IN]);
        // Rack's trigger must first observe a low button, as it does before a user click.
        o->params[OtisSampler::PLAY].setValue(t==16 ? 1 : 0);
        r.process(a); l.process(a); o->process(a); ++a.frame;
        if(t>2) {
            auto frame=o->engine.windowFor(0).begin/256;
            assert(frame==2 || frame==5); seen2|=frame==2; seen5|=frame==5;
        }
        float left=o->outputs[0].getVoltage(),right=o->outputs[1].getVoltage();
        assert(std::isfinite(left) && std::isfinite(right));
        energy+=left*left+right*right; difference+=(left-right)*(left-right);
    }
    if(!(seen2 && seen5 && energy>1 && difference>.01*energy))
        std::cerr<<"Example: frame2="<<seen2<<" frame5="<<seen5<<" energy="<<energy<<" stereo difference="<<difference<<"\n";
    assert(seen2 && seen5 && energy>1 && difference>.01*energy);
}
int main() {
    Context context; context.engine=new engine::Engine; contextSet(&context);
    Plugin otisPlugin; otisPlugin.slug="OtisLab"; otisPlugin.version="2.7.0"; otisPlugin.addModel(modelOtisSampler);
    Plugin bytePlugin; bytePlugin.slug="ByteBusLab"; bytePlugin.version="2.1.0";
    bytePlugin.addModel(modelLookup); bytePlugin.addModel(modelClockedRegister);
    rack::plugin::plugins.push_back(&otisPlugin); rack::plugin::plugins.push_back(&bytePlugin);
    auto library=std::make_shared<lu::Library>(lu::Library::load("../bytebus/res/lookup-v1.bin"));
    adapter(library); auto expected=chain(library,48000); assert(chain(library,44100)==expected && chain(library,96000)==expected);
    example(library);
    rack::plugin::plugins.clear();
    std::cout<<"PASS: actual OTIS frame port, bus hold/width/CV priority, stereo JSON export -> Lookup import/patch, original cable IDs, delayed randomized Register/Lookup/OTIS chain and reset replay at 44.1/48/96 kHz\n";
}
