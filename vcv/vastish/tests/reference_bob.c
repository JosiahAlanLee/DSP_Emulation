// Compile the unmodified Pd source supplied via PD_BOB_SOURCE.
#include PD_BOB_SOURCE
int pd_compatibilitylevel=55;
void ref_bob(float* x,float* hz,float* res,float* out,int n,float sr) {
    t_bob b={0}; b.x_sr=sr; b.x_oversample=2; b.x_params.p_saturation=3;
    t_int w[]={0,(t_int)&b,(t_int)x,(t_int)hz,(t_int)res,(t_int)out,n}; bob_perform(w);
}
