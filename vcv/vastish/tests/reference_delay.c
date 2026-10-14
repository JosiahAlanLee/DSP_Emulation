#include PD_DELAY_SOURCE
#include <stdlib.h>
#include <math.h>
void ref_comb(float* input,float* hz,float* output,int n,float sr,float feedback) {
    t_delwritectl c={0}; t_sigvd reader={0}; int offset,i;
    c.c_n=(int)(sr*.3)+64; c.c_vec=calloc(c.c_n+XTRASAMPS,sizeof(float)); c.c_phase=XTRASAMPS;
    reader.x_sr=sr*.001; reader.x_zerodel=16;
    for(offset=0;offset<n;offset+=16) {
        float delay[16],read[16],write[16];
        for(i=0;i<16;++i) delay[i]=1000/fmaxf(hz[offset+i],5);
        t_int rw[]={0,(t_int)delay,(t_int)read,(t_int)&c,(t_int)&reader,16}; sigvd_perform(rw);
        for(i=0;i<16;++i) output[offset+i]=.5f*(write[i]=input[offset+i]+feedback*read[i]);
        t_int ww[]={0,(t_int)write,(t_int)&c,16}; sigdelwrite_perform(ww);
    }
    free(c.c_vec);
}
