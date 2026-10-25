// Compare original mono/stereo audio and source traces before/after bus additions.
#ifndef VASTISH_ENGINE_HEADER
#define VASTISH_ENGINE_HEADER "../src/engine/Engine.hpp"
#endif
#include VASTISH_ENGINE_HEADER
#include <cstring>
#include <iostream>
int main() {
    uint64_t hash=14695981039346656037ull;
    auto add=[&](float x) { uint32_t bits; std::memcpy(&bits,&x,4); hash^=bits; hash*=1099511628211ull; };
    int16_t left[1024],right[1024];
    for(int i=0;i<1024;++i) { left[i]=int16_t(20000*std::sin(i*.071)); right[i]=int16_t(14000*std::cos(i*.037)); }
    for(float sr:{44100.f,48000.f,96000.f}) for(int program=0;program<6;++program) for(int sampleMode=0;sampleMode<3;++sampleMode) {
        vastish::Engine engine; engine.prepare(sr); auto p=vastish::programs()[program];
        if(sampleMode) p[vastish::PITCH_WAVE]=vastish::SAMPLE_WAVE;
        vastish::SampleView sample{left,1024,29761.9,true,sampleMode==2 ? right : nullptr};
        std::array<vastish::VoiceInput,8> inputs{};
        for(int i=0;i<8;++i) { inputs[i].note=42+3*i; inputs[i].velocity=.3f+.08f*i; inputs[i].aux[0]=.5; }
        for(int t=0;t<6000;++t) {
            int used=t<3000 ? 3 : 1;
            for(int i=0;i<8;++i) { inputs[i].gate=i<used && t%2000<1400; inputs[i].modwheel=(t%1000)/1000.f; }
            auto out=engine.tick(p,inputs,used,sampleMode ? &sample : nullptr); add(out[0]); add(out[1]);
            for(int i=0;i<8;++i) for(int source=0;source<18;++source) add(engine.voices[i].sources[source]);
        }
    }
    std::cout<<std::hex<<hash<<"\n";
}
