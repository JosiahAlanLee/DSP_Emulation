#include "../src/engine/Dsp.hpp"
#include <cassert>
#include <iostream>
#include <random>
extern "C" {
void ref_comb(float*,float*,float*,int,float,float);
void ref_bob(float*,float*,float*,float*,int,float);
void ref_lop(float*,float*,float*,int,float);
void ref_hip(float*,float*,int,float);
void ref_cos(float*,float*,int);
void ref_vcf(float*,float*,float*,float*,int,float,float);
}
int main() {
    const int n=8192; std::vector<float> x(n),hz(n),res(n),out(n),im(n),phase(n);
    std::mt19937 rng(1605); std::uniform_real_distribution<float> u(-1,1);
    for(int i=0;i<n;++i) { x[i]=u(rng); hz[i]=50+11950*(.5f+.5f*std::sin(i*.0003f)); res[i]=3.9f*(.5f+.5f*u(rng)); phase[i]=u(rng)*20; }
    for(float sr:{44100.f,48000.f,96000.f}) {
        auto check=[&](const char* name,auto tick,float tol) { float err=0; for(int i=0;i<n;++i) err=std::max(err,std::abs(out[i]-tick(i))); std::cout<<name<<" sr="<<sr<<" max_abs="<<err<<"\n"; assert(err<tol); };
        vastish::Ladder ladder; ref_bob(x.data(),hz.data(),res.data(),out.data(),n,sr);
        check("bob~",[&](int i){return ladder.tick(x[i],hz[i],res[i],sr);},2e-6f);
        vastish::Lowpass lp; ref_lop(x.data(),hz.data(),out.data(),n,sr);
        check("lop~",[&](int i){return lp.tick(x[i],hz[i],sr);},2e-6f);
        vastish::Highpass hp; ref_hip(x.data(),out.data(),n,sr);
        check("hip~",[&](int i){return hp.tick(x[i],5,sr);},2e-5f);
        ref_cos(phase.data(),out.data(),n);
        check("cos~",[&](int i){return vastish::cosine(phase[i]);},2e-5f);
        vastish::Comb comb; comb.prepare(sr); ref_comb(x.data(),hz.data(),out.data(),n,sr,.98f);
        check("delread4~ feedback",[&](int i){return comb.tick(x[i],hz[i],.98f,sr);},5e-5f);
        for(float q:{.5f,2.5f,40.f}) {
            vastish::Vcf v; ref_vcf(x.data(),hz.data(),out.data(),im.data(),n,sr,q); float err=0;
            for(int i=0;i<n;++i) { auto a=v.tick(x[i],hz[i],q,sr); err=std::max(err,std::max(std::abs(a[0]-out[i]),std::abs(a[1]-im[i]))); }
            std::cout<<"vcf~ sr="<<sr<<" Q="<<q<<" max_abs="<<err<<"\n"; assert(err<2e-5f);
        }
    }
    std::cout<<"PASS unmodified Pd 0.55-2 primitive comparison\n";
}
