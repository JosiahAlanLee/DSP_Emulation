#include "../src/engine/Wav.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <memory>
#include <chrono>

int main() {
    using namespace otis;
    assert(Voice::pitchIncrement(1)==2048);
    assert(Voice::pitchIncrement(.5)==1024);
    assert(Voice::pitchIncrement(2)==4096);
    assert(Voice::pitchIncrement(1e8)==131068);
    assert(Voice::pitchIncrement(0)==4);
    assert(shiftFloor(-1,11)==-1 && shiftFloor(-2048,11)==-1);
    std::vector<int16_t> edge={-32768,-1,0,1,32767};
    assert(decodePcm(encodePcm(edge.data(),edge.size()))==edge);
    for(const char* invalid:{"A","!!!!","AA=A","AAA=AAAA","===="}) {
        bool rejected=false; try { decodePcm(invalid); } catch(...) { rejected=true; } assert(rejected);
    }
    writeWav("tests/build/roundtrip.wav",edge.data(),edge.size(),44100);
    auto w=loadWav("tests/build/roundtrip.wav"); assert(w.pcm==edge && w.rate==44100);
    auto e=std::make_unique<Engine>();
    std::vector<int16_t> sine(16000); for(size_t i=0;i<sine.size();i++) sine[i]=quantize(float(.5*std::sin(2*pi*1000*i/16000)));
    e->install(sine,16000); e->controls.loop=1; e->trigger(0);
    double energy=0; for(int i=0;i<48000;i++) { float x=e->process(0); assert(std::isfinite(x)); energy+=x*x; }
    assert(energy>1000 && e->playing());
    e->controls.gated=true; e->release(0); assert(!e->playing());
    e->controls.gated=false; e->trigger(0); e->release(0); assert(e->playing());
    e->stopAll();
    // Reverse one-shot reaches its lower boundary and terminates without wrapping the address.
    e->controls.reverse=true; e->controls.loop=0; e->trigger(0);
    for(int i=0;i<100000 && e->playing();i++) e->process(0);
    assert(!e->playing());
    // Tiny loops + maximum increment used to break naïve one-boundary folding.
    std::vector<int16_t> tiny={1000,-1000}; e->install(tiny,32000);
    std::mt19937 rng(1234);
    for(int loop=1;loop<=2;loop++) {
        e->controls.loop=loop;
        for(int reverse=0;reverse<2;reverse++) {
            e->controls.reverse=reverse; e->controls.pitch[0]=6; e->trigger(0);
            for(int i=0;i<10000;i++) {
                float x=e->process(0); assert(std::isfinite(x));
                const auto& v=e->voices[0]; assert(v.accumulator>=v.start && v.accumulator<=v.end);
            }
            assert(e->playing()); e->stopAll();
        }
    }
    for(int mode=0;mode<3;mode++) {
        e->controls.engineMode=mode; e->controls.loop=1; e->controls.reverse=false;
        for(int i=0;i<16;i++) e->trigger(i);
        for(int i=voiceLimits[mode];i<16;i++) assert(!e->voices[i].running);
        e->stopAll();
    }
    // Capture duration follows the selected conversion clock, independent of host sample rate.
    for(double host:{44100.,48000.,96000.}) for(int rate=0;rate<7;rate++) {
        e->setHostRate(host); e->controls.captureIndex=rate; e->startRecord();
        for(int i=0;i<int(host*.1);i++) e->process(.1f);
        assert(std::abs(double(e->recordFrames)-captureRate(rate)*.1)<2);
        e->stopRecord(); assert(e->frames>1000 && std::abs(e->sample.rate-captureRate(rate))<1e-6);
    }
    // Saving PCM round-trips exactly, including a live capture.
    auto saved=decodePcm(encodePcm(e->sample.pcm.data(),e->frames));
    assert(std::equal(saved.begin(),saved.end(),e->sample.pcm.begin()));
    // Capacity is finite and capture stops without reallocating sample memory.
    auto* buffer=e->recordBuffer.pcm.data(); e->controls.captureIndex=6; e->startRecord();
    e->recordFrames=memoryWords-1; e->recordPhase=.99;
    e->process(0); assert(!e->recording && e->recordFull && e->frames==memoryWords);
    assert(e->sample.pcm.data()==buffer);
    std::cout<<"PASS: pitch registers, PCM/WAV persistence, reverse, tiny loops, voice limits, 21 capture-clock cases, capacity\n";
}
