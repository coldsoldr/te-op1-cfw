/* Deterministic full-rate Clouds regression input: changing controls/block lengths and sustained rails.
 * Output hashes are compared with the released fixed-point implementation by clouds_cap_check.py.
 * Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with OP-1 firmware permission. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CLOUDS_SR 44100
#define CLOUDS_MAX_GRAINS 24
#define CLOUDS_LINEAR_READ
#define CLOUDS_SKIP_IDLE
#include "../plugins/clouds/clouds_dsp.c"
static clouds_t state[3];static int32_t scratch[3][CLOUDS_MEM_BYTES/4];
static int dynamic(const char *path){FILE*f=fopen(path,"wb");uint32_t rng=1234567;clouds_params_t p={0};int16_t in[64],out[64];for(int k=0;k<3;k++)clouds_init(&state[k],scratch[k],CLOUDS_GRANULAR);
for(int t=0;t<12000;t++){int pos=t%3,n=t%65;if(t%11==0){int32_t *v=&p.position;for(int j=0;j<7;j++){rng=rng*1664525u+1013904223u;v[j]=(int32_t)(rng>>17);}p.feedback=0;rng=rng*1664525u+1013904223u;p.pitch=(int)(rng%12289)-6144;p.freeze=(t/110)%2;p.trigger=t%77==0;}
if(!n)n=64;for(int i=0;i<n;i++){rng=rng*1664525u+1013904223u;in[i]=(int16_t)(rng>>16);}clouds_process(&state[pos],&p,in,out,n);fwrite(out,2,n,f);
if(t%997==0){clouds_init(&state[pos],scratch[pos],CLOUDS_GRANULAR);}}
fclose(f);return 0;}

static int rails(const char *path){FILE*f=fopen(path,"wb");clouds_t c; static int32_t rail_scratch[CLOUDS_MEM_BYTES/4]; clouds_init(&c,rail_scratch,CLOUDS_GRANULAR);clouds_params_t p={32767,32767,32767,32767,32767,0,32767,6144,0,0};int16_t in[64],out[64];
for(int t=0;t<24000;t++){for(int i=0;i<64;i++)in[i]=t<6000?32767:t<12000?-32768:t<18000?((i&1)?32767:-32768):(int16_t)(i*1024-32768);p.freeze=(t/1000)%2;p.pitch=(t/3000)%2?6144:-6144;clouds_process(&c,&p,in,out,64);fwrite(out,2,64,f);}fclose(f);return 0;}

static int edge(const char *path){FILE*f=fopen(path,"wb");clouds_t c; static int32_t edge_scratch[CLOUDS_MEM_BYTES/4]; clouds_init(&c,edge_scratch,CLOUDS_GRANULAR);clouds_params_t p={16384,32767,16000,0,32767,0,32767,0,0,0};int16_t in[64]={0},out[64];
for(int k=0;k<100;k++){c.phasor=INT32_MAX-1023;clouds_process(&c,&p,in,out,64);fwrite(out,2,64,f);fwrite(&c.phasor,4,1,f);}
fclose(f);return 0;}

int main(int argc,char **argv) { if(argc != 3)return 2;if(argv[2][0]=='e')return edge(argv[1]);return argv[2][0]=='r'?rails(argv[1]):dynamic(argv[1]); }
