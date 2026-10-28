// Byte codec and scheduling properties, with independent silence/polarity vectors.
#include "../src/engine/Adapter.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

static size_t allocations=0;
void* operator new(size_t n) { ++allocations; if (auto* p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }

using namespace dspemu;
using adapter::Format;
struct Port {
    int channels=0;
    float volts[16]{};
    int getChannels() { return channels; }
    float getVoltage(int k) { return volts[k]; }
    void setChannels(int n) { channels=n; }
    void setVoltage(float v,int k) { volts[k]=v; }
};
struct Rig {
    adapter::Engine engine;
    adapter::Parameters p;
    bus::Reader reader;
    Port port;
    double sr=48000;
    adapter::Output tick(int word=0,int channels=9,bool clk=false,bool jack=false,float clock=0,float reset=0) {
        port.channels=channels;
        for (int bit=0;bit<8;++bit) port.volts[bit]=word & (1<<bit) ? 10.f : 0.f;
        port.volts[8]=clk ? 10.f : 0.f;
        clock_owner::Inputs in; in.data=reader.process(port); in.clockConnected=jack; in.clock=clock; in.reset=reset;
        return engine.tick(p,in,sr);
    }
};
static void codecs() {
    const Format all[]={Format::Offset,Format::Signed,Format::Casio,Format::Yamaha};
    for (int n=0;n<256;++n) {
        for (auto f:all) assert(adapter::convert(n,f,f)==n);
        assert(adapter::convert(n,Format::Signed,Format::Offset)==(n^128));
        assert(adapter::convert(n,Format::Offset,Format::Signed)==(n^128));
        for (auto from:all) for (auto to:all) {
            int result=adapter::convert(n,from,to);
            assert(result>=0 && result<256);
        }
    }
    struct Vector { Format f; uint8_t encoded,offset; };
    const Vector vectors[]={
        {Format::Casio,0x00,0x80},{Format::Casio,0x10,0x90},{Format::Casio,0x30,0x70},
        {Format::Casio,0x50,0xa0},{Format::Casio,0x60,0x40},{Format::Casio,0xc0,0x80},
        {Format::Yamaha,0x30,0x80},{Format::Yamaha,0xc0,0x40},{Format::Yamaha,0xd8,0xa0},
        {Format::Signed,0x00,0x80},{Format::Signed,0xc0,0x40},{Format::Signed,0x20,0xa0}};
    for (auto v:vectors) assert(adapter::convert(v.encoded,v.f,Format::Offset)==v.offset);
    assert(adapter::convert(0x80,Format::Offset,Format::Casio)==0);
    assert(adapter::convert(0x80,Format::Offset,Format::Yamaha)==0x30);
    assert(adapter::convert(0xa0,Format::Offset,Format::Casio)==0x50);
    assert(adapter::convert(0xa0,Format::Offset,Format::Yamaha)==0xd8);
    // A truncated device word has a bounded, explicitly lossy reconstruction.
    for (int n=0;n<256;++n) for (auto f:{Format::Casio,Format::Yamaha}) {
        int pcm=(n-128)*256;
        int decoded=adapter::decode(adapter::encode(int16_t(pcm),f),f);
        assert(std::abs(decoded-pcm)<=(f==Format::Casio ? 1024 : 2048));
    }
    assert(adapter::decode(0x50,Format::Casio,.5f)==12288);
    assert(adapter::decode(0x50,Format::Casio,-.5f)==4096);
    assert(adapter::decode(0x50,Format::Casio,NAN)==8192);
}
static void routingAndPresets() {
    adapter::Parameters p;
    for (int n=0;n<256;++n) {
        assert(adapter::transform(n,p)==n);
        for (int to=0;to<8;++to) for (int from=-1;from<8;++from) {
            auto routed=p; routed.route.fill(-1); routed.route[to]=from;
            assert(adapter::transform(n,routed)==(from>=0 && (n & (1<<from)) ? 1<<to : 0));
        }
        auto hits=adapter::preset(adapter::Preset::GorgonHits);
        assert(adapter::transform(n,hits)==(n&63));
        auto mutes=adapter::preset(adapter::Preset::GorgonMutes);
        assert(adapter::transform(n,mutes)==((n&127)^63));
        auto centered=adapter::preset(adapter::Preset::CenteredMask);
        assert(adapter::transform(n,centered)==(n^128));
    }
    p.route.fill(-1); p.keep=0; p.flip=0xa5;
    for (int n=0;n<256;++n) assert(adapter::transform(n,p)==0xa5);
    assert(adapter::preset(adapter::Preset::CasioAudio).idle==128);
    assert(adapter::preset(adapter::Preset::YamahaAudio).idle==128);
    assert(adapter::preset(adapter::Preset::Slow).clock==adapter::Clock::Internal);
}
static void clocked(double sr) {
    Rig r; r.sr=sr; r.p=adapter::preset(adapter::Preset::CasioAudio);
    auto x=r.tick(0x50); assert(x.loaded && x.word==128 && x.clk && x.triggers==0);
    x=r.tick(0x50); assert(!x.clk && !x.acquired);
    for (int i=0;i<32;++i) { x=r.tick(0x50); assert(x.word==128 && !x.clk); }
    x=r.tick(0x50,9,true); assert(x.word==160 && x.acquired && x.clk && x.triggers==160 && x.volts==1.25f);
    // Data movement and a held high clock cannot update the captured word.
    for (int i=0;i<8;++i) { x=r.tick(0x60,9,true); assert(x.word==160 && !x.clk && !x.triggers); }
    r.tick(0x50);
    x=r.tick(0x50,9,true); assert(x.word==160 && x.clk && x.triggers==160);
    r.tick(0x50);
    r.p.input=Format::Signed;
    x=r.tick(0x50); assert(x.word==160); // Format edits wait for acquisition.
    x=r.tick(0x50,9,true); assert(x.word==208 && x.clk);
    // Simultaneous reset and acquisition loads the seed; mandatory low first.
    r.p.idle=17;
    x=r.tick(0x50,9,false,false,0,10); assert(x.word==17 && !x.clk && !x.triggers);
    x=r.tick(0x50,9,true,false,0,10); assert(x.loaded && x.clk && x.word==17 && !x.triggers);
    x=r.tick(0x50,9,false,false,0,10); assert(!x.clk);
    x=r.tick(0x50,9,true,false,0,10); assert(x.word==208 && x.acquired && x.triggers==208);
    // Disconnection publishes numeric IDLE and silences trigger outputs.
    r.p.idle=128; x=r.tick(0,0); assert(x.word==128 && !x.clocked && !x.clk && !x.triggers);
}
static void engineRate(double sr) {
    Rig r; r.sr=sr;
    auto x=r.tick(0xa5,8); assert(x.loaded && !x.clocked && !x.triggers);
    x=r.tick(0xa5,8); assert(x.word==0xa5 && !x.triggers && !x.clocked);
    x=r.tick(0xa5,8); assert(!x.triggers);
    x=r.tick(0,8); assert(!x.triggers);
    x=r.tick(0xa5,8); assert(x.triggers==0xa5);
    // A change of cable mode starts a seed publication, not a hot-plug hit.
    x=r.tick(0xff,9,true); assert(x.loaded && x.word==0 && x.clk && !x.triggers);
    r.tick(0xff,9,true); r.tick(0xff,9,false);
    x=r.tick(0xff,9,true); assert(x.acquired && x.triggers==255);
    r.tick(0,0);
    x=r.tick(0xa5,8); assert(x.loaded && !x.triggers);
    x=r.tick(0xa5,8); assert(x.word==0xa5 && !x.triggers);
}
static void reclocking(double sr) {
    Rig r; r.sr=sr; r.p.clock=adapter::Clock::Jack;
    r.tick(0); r.tick(0);
    auto x=r.tick(0xa5,9,false,true,10); assert(x.word==0xa5 && x.clk && x.triggers==0xa5);
    r.tick(0x5a,9,false,true,0);
    x=r.tick(0x5a,9,false,true,10); assert(x.word==0x5a && x.clk);
    for (int i=0;i<30;++i) { x=r.tick(0xff,9,false,true,10); assert(x.word==0x5a && !x.clk); }
    // Track a high CLOCK while it is unselected; selecting it invents no edge.
    r.p.clock=adapter::Clock::Follow;
    r.tick(1,9,false,true,10);
    r.p.clock=adapter::Clock::Jack;
    x=r.tick(1,9,false,true,10); assert(!x.acquired && !x.clk);
    r.tick(1,9,false,true,0); x=r.tick(1,9,false,true,10); assert(x.word==1 && x.acquired);
    Rig slow; slow.sr=sr; slow.p=adapter::preset(adapter::Preset::Slow);
    int captured=0,last=-1; bool high=false;
    for (int n=0;n<int(sr);++n) {
        int value=(n*17)&255;
        x=slow.tick(value,9,false); // Upstream CLK is stopped; local clock reads raw bits.
        assert(!(high && x.clk)); high=x.clk;
        if (x.acquired) {
            assert(x.word==value && x.clk);
            if (last>=0) assert(std::abs((n-last)-sr/8)<=1);
            last=n; ++captured;
        }
    }
    assert(captured>=7 && captured<=8);
    slow.p.rateHz=NAN;
    for (int n=0;n<100;++n) { x=slow.tick(0xa5); assert(std::isfinite(x.volts)); }
}
static void realtime() {
    Rig r; r.p=adapter::preset(adapter::Preset::YamahaAudio);
    r.tick(); r.tick();
    size_t before=allocations;
    for (int n=0;n<100000;++n) {
        auto x=r.tick(n&255,9,(n&3)==0);
        assert(std::isfinite(x.level) && std::isfinite(x.volts));
    }
    assert(allocations==before);
}
int main() {
    codecs(); routingAndPresets();
    for (double sr:{44100.,48000.,96000.}) { clocked(sr); engineRate(sr); reclocking(sr); }
    realtime();
    std::cout << "PASS: all byte codecs/routes, independent silence/polarity, loss bounds, repeated triggers, raw reclocking, stopped clocks, reset/hot-plug and zero processing allocations at 44.1/48/96 kHz\n";
}
