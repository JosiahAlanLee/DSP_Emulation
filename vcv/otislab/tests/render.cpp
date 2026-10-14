#include "../src/engine/Wav.hpp"
#include <memory>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=6) { std::cerr<<"render INPUT.wav OUTPUT.wav SEMITONES CHIP_MODE HOST_RATE\n"; return 1; }
    try {
        auto wav=otis::loadWav(argv[1]); auto e=std::make_unique<otis::Engine>();
        double host=std::stod(argv[5]); e->setHostRate(host); e->install(wav.pcm,wav.rate);
        e->controls.loop=1; e->controls.engineMode=std::stoi(argv[4]); e->controls.pitch[0]=std::stof(argv[3])/12; e->trigger(0);
        std::vector<int16_t> out(size_t(host*2)); for(auto& s:out) s=otis::quantize(e->process(0));
        otis::writeWav(argv[2],out.data(),out.size(),host);
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
