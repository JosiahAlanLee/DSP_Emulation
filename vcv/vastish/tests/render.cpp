// Render the same qlist events as pd/vastish/tools/mkseq.py, without a Pd runtime.
#include "../src/engine/Engine.hpp"
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <limits>
struct Wav {
    std::ofstream file; std::vector<float> samples; uint32_t frames=0,rate=44100;
    void u16(uint16_t v) { file.put(char(v)); file.put(char(v>>8)); }
    void u32(uint32_t v) { u16(v&65535); u16(v>>16); }
    void close() { if(!file.is_open()) return; file.write(reinterpret_cast<const char*>(samples.data()),samples.size()*sizeof(float)); if(!file) throw std::runtime_error("WAV write failed"); file.seekp(4); u32(36+frames*8); file.seekp(40); u32(frames*8); file.close(); }
    void open(const std::string& path,uint32_t sr) { close(); rate=sr; frames=0; samples.clear(); file.open(path,std::ios::binary); if(!file) throw std::runtime_error("Cannot open WAV"); file.write("RIFF",4); u32(0); file.write("WAVEfmt ",8); u32(16); u16(3); u16(2); u32(rate); u32(rate*8); u16(8); u16(32); file.write("data",4); u32(0); }
    void write(std::array<float,2> y) { samples.insert(samples.end(),y.begin(),y.end()); ++frames; }
    ~Wav() { close(); }
};
int main(int argc,char** argv) {
    if(argc<3) { std::cerr<<"usage: render sequence.txt output_directory [sample_rate]\n"; return 1; }
    float sr=argc>3?std::stof(argv[3]):44100; std::filesystem::create_directories(argv[2]);
    vastish::Engine engine; engine.prepare(sr); auto params=vastish::programs()[1]; std::array<vastish::VoiceInput,8> inputs{};
    std::array<uint64_t,8> age{}; uint64_t serial=0; Wav wav; bool recording=false;
    auto advance=[&](float ms) { int count=int(std::round(ms*.001*sr)); for(int i=0;i<count;++i) { auto y=engine.tick(params,inputs); if(recording) wav.write(y); } };
    advance(200);
    std::ifstream sequence(argv[1]); if(!sequence) return 1; std::string line;
    while(std::getline(sequence,line)) {
        for(char& c:line) if(c==';') c=' ';
        std::istringstream event(line); float delay; std::string target; event>>delay>>target; advance(delay);
        if(target=="vast-prog") { int pg; event>>pg; params=vastish::programs().at(pg); }
        else if(target=="vast-mw") { float v; event>>v; for(auto& in:inputs) in.modwheel=v; }
        else if(target=="vast-note") {
            float note,vel; event>>note>>vel; int selected=-1; uint64_t oldest=std::numeric_limits<uint64_t>::max();
            for(int i=0;i<8;++i) if((vel>0?!inputs[i].gate:inputs[i].gate && inputs[i].note==note) && age[i]<oldest) { oldest=age[i]; selected=i; }
            if(selected<0 && vel>0) { selected=0; for(int i=1;i<8;++i) if(age[i]<age[selected]) selected=i; }
            if(selected>=0) { inputs[selected].note=note; inputs[selected].velocity=vel/127; inputs[selected].gate=vel>0; age[selected]=serial++; }
        }
        else if(target=="vast-rec") {
            std::string command; event>>command;
            if(command=="open") { std::string path; event>>path; auto output=std::filesystem::path(argv[2])/std::filesystem::path(path).filename(); wav.open(output.string(),uint32_t(sr)); }
            else if(command=="start") recording=true;
            else if(command=="stop") { recording=false; wav.close(); }
        }
    }
    std::cout<<"Rendered programs 1–5 at "<<sr<<" Hz; faults="<<engine.faults<<"\n"; return engine.faults?1:0;
}
