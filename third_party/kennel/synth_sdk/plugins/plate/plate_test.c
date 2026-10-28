/* Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with OP-1 firmware permission. */
/* cc -O2 plate_test.c plate_dsp.c -o /tmp/plate-test && /tmp/plate-test */
#include "plate_dsp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static plate_state a,b;
static long long run(int decay,int tone,int pre,int blocks){
 int32_t q[4]={decay,tone,pre,32767};plate_init(&a);long long energy=0,late=0;
 int16_t l[64],r[64];
 for(int n=0;n<blocks;n++){
  memset(l,0,sizeof l);memset(r,0,sizeof r);if(!n)l[0]=24000;
  plate_process(&a,l,r,q);
  for(int i=0;i<64;i++){energy+=(long long)l[i]*l[i]+(long long)r[i]*r[i];if(n>blocks-500)late+=(long long)l[i]*l[i]+(long long)r[i]*r[i];}
 }
 printf("impulse decay=%d tone=%d pre=%d energy=%lld last500=%lld\n",decay,tone,pre,energy,late);assert(energy>0);return energy;
}
int main(void){
 assert(sizeof a<176460);int16_t l[64],r[64],original[64];int32_t q[4]={32767,32767,0,0};
 plate_init(&a);for(int i=0;i<64;i++)original[i]=l[i]=(int16_t)(i*1013-32768);
 plate_process(&a,l,0,q);assert(!memcmp(l,original,sizeof l));
 plate_init(&b);memset(l,0,sizeof l);memset(r,0,sizeof r);q[3]=32767;
 for(int n=0;n<2000;n++){plate_process(&b,l,r,q);for(int i=0;i<64;i++)assert(l[i]==0&&r[i]==0);}
 long long shorttail=run(0,32767,0,12000),longtail=run(32767,32767,0,12000);assert(longtail>shorttail);
 run(32767,0,32767,12000);
 plate_init(&a);plate_init(&b);q[0]=16384;q[1]=19660;q[2]=0;q[3]=32767;int differs=0;
 for(int n=0;n<2500;n++){
  memset(l,0,sizeof l);memset(r,0,sizeof r);if(!n)l[0]=20000;
  plate_process(&a,l,r,q);for(int i=0;i<64;i++)differs|=l[i]!=r[i];
 }
 assert(differs);for(unsigned i=0;i<65536;i++)assert(b.ring[i]==0);
 /* Sustained full-scale DC at maximum feedback must remain bounded and decay after release. */
 plate_init(&a);q[0]=q[1]=q[2]=q[3]=32767;
 for(int n=0;n<20000;n++){for(int i=0;i<64;i++)l[i]=r[i]=n<2000?32767:0;plate_process(&a,l,r,q);}
 int peak=0;for(unsigned i=0;i<65536;i++){int v=abs(a.ring[i]);if(v>peak)peak=v;}printf("post-DC ring peak %d\n",peak);assert(peak<100);
 puts("Plate host checks PASS");
}
