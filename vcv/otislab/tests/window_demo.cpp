// Offline auditions of the same engine, window modes and publication crossfade.
#include "../src/engine/LoopMutator.hpp"
#include "../src/engine/Wav.hpp"
#include <filesystem>
#include <memory>
#include <iostream>
static std::vector<int16_t> frames() {
    std::vector<int16_t> pcm(16*256);
    for(int frame=0;frame<16;++frame) for(int i=0;i<256;++i) {
        double phase=2*otis::pi*i/256, tone=0;
        for(int h=1;h<=12;++h) tone+=std::sin(phase*h+frame*.13*h)*std::exp(-std::abs(h-(1+frame*.6))*.55)/h;
        pcm[frame*256+i]=otis::quantize(float(tone*.55));
    }
    return pcm;
}
int main(int argc,char** argv) {
    if(argc<2) { std::cerr<<"usage: window_demo output_directory\n"; return 1; }
    std::filesystem::create_directories(argv[1]); auto original=frames();
    otis::writeWav(std::string(argv[1])+"/00-frame-source.wav",original.data(),original.size(),otis::captureRate(5));
    for(int mode:{1,2}) {
        auto e=std::make_unique<otis::Engine>(); auto pcm=original; e->install(pcm,otis::captureRate(5));
        e->controls.loop=1; e->controls.windowMode=mode; e->controls.sizeOctaves[0]=8; e->trigger(0);
        std::vector<int16_t> out(48000*6);
        for(size_t i=0;i<out.size();++i) {
            e->controls.position[0]=float(i)/float(out.size()-1);
            float edge=std::min(1.f,float(std::min(i,out.size()-1-i))/480);
            out[i]=otis::quantize(e->process(0)*edge);
        }
        otis::writeWav(std::string(argv[1])+(mode==1?"/01-fixed-window-scan.wav":"/02-transwave-frame-scan.wav"),out.data(),out.size(),48000);
    }
    {
        auto e=std::make_unique<otis::Engine>(); auto pcm=original; e->install(pcm,otis::captureRate(5));
        e->controls.loop=1; e->controls.windowMode=1; e->controls.sizeOctaves[0]=10; e->trigger(0);
        std::vector<int16_t> out; out.reserve(48000*10);
        for(int generation=0;generation<5;++generation) {
            std::vector<int16_t> prepared;
            if(generation) {
                prepared=e->sample.pcm;
                otis::reworkLoop(prepared,0,1024,.2f+generation*.15f,.65f,100+generation);
                auto publication=e->publishPrepared(prepared,e->sequence());
                if(publication!=otis::Engine::Publication::Published) return 1;
            }
            for(int i=0;i<96000;++i) {
                float edge=1;
                if(!generation && i<480) edge=i/480.f;
                if(generation==4 && i>=95520) edge=(95999-i)/480.f;
                out.push_back(otis::quantize(e->process(0)*edge));
            }
            if(e->fadingPcm) return 1;
        }
        otis::writeWav(std::string(argv[1])+"/03-four-rework-generations.wav",out.data(),out.size(),48000);
    }
    std::cout<<"Rendered original source, fixed-window scan, whole-frame scan, and four loop-rework generations\n";
}
