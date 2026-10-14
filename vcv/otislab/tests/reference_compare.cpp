// Optional independent reference comparison, run from the OtisLab root:
// gcc -O2 -c /path/to/Ensoniq-EPS-16-Plus/native/es5505_core.c -o tests/build/reference_core.o
// g++ -std=c++17 -O2 -I/path/to/Ensoniq-EPS-16-Plus/native tests/reference_compare.cpp tests/build/reference_core.o -o tests/build/reference_compare
// tests/build/reference_compare
#include "../src/engine/OtisEngine.hpp"
extern "C" {
#include "es5505_core.h"
}
#include <iostream>
#include <memory>
#include <random>
static uint16_t reader(void* context,unsigned,uint32_t address) {
 auto& s=*static_cast<otis::Sample*>(context);
 if(address<otis::baseAddress || address-otis::baseAddress>=8192) return 0;
 return uint16_t(s.pcm[address-otis::baseAddress]);
}
int main() {
 auto s=std::make_unique<otis::Sample>(); std::mt19937 random(16);
 for(int i=0;i<8192;i++) s->pcm[i]=int16_t(int(random()%40000)-20000);
 unsigned comparisons=0;
 for(int filter=0;filter<4;filter++) for(int reverse=0;reverse<2;reverse++) for(int loop=0;loop<3;loop++) {
  Es5505Core ref; es5505_core_init(&ref,reader,s.get()); ref.active_voice=0;
  otis::Voice v; v.running=true; v.reverse=reverse; v.loop=loop; v.filterMode=filter;
  v.k1=0x9820; v.k2=0xc120; v.start=128<<11; v.end=(128+8191)<<11; v.accumulator=reverse?v.end:v.start; v.frequency=3732;
  auto& r=ref.voices[0]; r.control=uint16_t((filter<<10)|(reverse?64:0)|(loop?8:0)|(loop==2?16:0)); r.left_volume=r.right_volume=0xc0;
  r.k1=v.k1; r.k2=v.k2; r.start=v.start; r.end=v.end; r.accumulator=v.accumulator; r.frequency=v.frequency;
  for(int i=0;i<12000;i++) {
   int32_t l,rr; es5505_core_render(&ref,&l,&rr,1); bool end=false; int32_t x=v.tick(*s,8192,end);
   if(l!=x || rr!=x) { std::cerr<<"Mismatch filter="<<filter<<" reverse="<<reverse<<" loop="<<loop<<" frame="<<i<<" reference="<<l<<" actual="<<x<<"\n"; return 1; }
   comparisons++;
  }
 }
 std::cout<<"PASS: "<<comparisons<<" exact integer output comparisons against independent mardlib ES5505 core (4 filters x 2 directions x 3 loop modes)\n";
}
