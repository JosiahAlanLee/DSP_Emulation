#include PD_OSC_SOURCE
#include <stdlib.h>
void* getbytes(size_t n) { return calloc(1,n); }
void freebytes(void* p,size_t n) { (void)n; free(p); }
void ref_cos(float* phase,float* out,int n) {
    cos_maketable(); t_int w[]={0,(t_int)phase,(t_int)out,n}; cos_perform(w);
}
void ref_vcf(float* x,float* hz,float* re,float* im,int n,float sr,float q) {
    cos_maketable(); t_vcfctl c={0}; c.c_q=q; c.c_isr=6.28318f/sr;
    t_int w[]={0,(t_int)x,(t_int)hz,(t_int)re,(t_int)im,(t_int)&c,n}; sigvcf_perform(w);
}
