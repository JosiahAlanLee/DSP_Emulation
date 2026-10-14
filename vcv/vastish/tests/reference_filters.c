#include PD_FILTER_SOURCE
void ref_lop(float* x,float* hz,float* out,int n,float sr) {
    t_siglop lp={0}; lp.x_conversion=(2*3.14159)/sr;
    t_int w[]={0,(t_int)&lp,(t_int)x,(t_int)hz,(t_int)out,n}; siglop_perf_vector(w);
}
void ref_hip(float* x,float* out,int n,float sr) {
    t_hipctl hp={0}; hp.c_coef=1-5*(2*3.14159)/sr;
    t_int w[]={0,(t_int)x,(t_int)out,(t_int)&hp,n}; sighip_perform(w);
}
