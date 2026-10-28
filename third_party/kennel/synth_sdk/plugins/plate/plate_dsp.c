/* Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware
 * permission; project-maintainer-authorized Maiko #250 port. See README.md. */
#include "plate_dsp.h"
#include "plate_tables.h"
static int32_t sat(int32_t x){return x>32767?32767:x< -32768?-32768:x;}
static int32_t mul(int32_t a,int32_t b){return (a*b)>>15;}
int32_t plate_sine(uint32_t p){unsigned i=p>>24;int32_t a=plate_sin[i];
 return a+(((plate_sin[i+1]-a)*(int32_t)((p>>8)&65535))>>16);}
void plate_init(plate_state *s){
 s->head=s->phase=0;s->input=s->damp[0]=s->damp[1]=0;
 s->feedback[0]=s->feedback[1]=s->peak=0;s->pre=s->dry=-1;s->wet=0;
 for(unsigned i=0;i<65536;i++)s->ring[i]=0;
}
static int32_t read_at(plate_state *s,unsigned h,unsigned off){return s->ring[(h+off)&65535];}
static void write_at(plate_state *s,unsigned h,unsigned off,int32_t v){s->ring[(h+off)&65535]=(int16_t)v;}
static int32_t ap(plate_state *s,unsigned h,unsigned wr,unsigned rd,int32_t x,int32_t g){
 int32_t d=read_at(s,h,rd),v=sat(x-mul(d,g));write_at(s,h,wr,v);return sat(d+mul(v,g));
}
static int32_t modap(plate_state *s,unsigned h,unsigned wr,unsigned delay,int32_t x){
 unsigned off=wr+(delay>>8);int32_t d=read_at(s,h,off);
 d+=((read_at(s,h,off+1)-d)*(int32_t)(delay&255))>>8;
 int32_t v=sat(x+mul(d,22938));write_at(s,h,wr,v);return sat(d-mul(v,22938));
}
void plate_process(plate_state *s,int16_t *l,int16_t *r,const int32_t q[4]){
 int32_t dec=mul(q[0],27853),gain=dec+1638,diff=dec+6553;
 if(diff<8192)diff=8192;if(diff>16384)diff=16384;
 int32_t damp=32767-mul(32767-q[1],26214),input=32767-(((32767-q[1])*26214)>>16);
 int32_t pre=mul(mul(q[2],q[2]),8191);
 if(s->pre<0)s->pre=pre;else if(pre>s->pre+16)s->pre+=16;else if(pre<s->pre-16)s->pre-=16;else s->pre=pre;
 int32_t dry=q[3]<=16384?32768:(32768-q[3])*2,wet=q[3]<=16384?q[3]*2:32768;
 if(s->dry<0){s->dry=dry;s->wet=wet;}
 int32_t dr=s->dry*64,we=s->wet*64,dd=dry-s->dry,dw=wet-s->wet;
 unsigned dl=(unsigned)(((plate_sine(s->phase)*24)>>7)+254976);
 unsigned rr=(unsigned)(((plate_sine(s->phase+0x40000000u)*24)>>7)+344320);
 s->peak=0;
 for(unsigned n=0;n<64;n++){
  unsigned h=s->head;int32_t a=l[n],b=r?r[n]:a;write_at(s,h,0,(a+b)>>1);
  s->input+=(((read_at(s,h,(unsigned)s->pre)-s->input)>>1)*input)>>14;
  int32_t x=ap(s,h,8192,8402,s->input,24576);
  x=ap(s,h,8403,8562,x,24576);x=ap(s,h,8563,9125,x,20480);x=ap(s,h,9126,9536,x,20480);
  int32_t y=modap(s,h,9537,dl,sat(x+mul(s->feedback[1],gain)));
  write_at(s,h,10559,y);s->damp[0]+=(((read_at(s,h,17157)-s->damp[0])>>1)*damp)>>14;
  y=ap(s,h,17158,19825,mul(s->damp[0],gain),diff);write_at(s,h,19826,y);
  y=modap(s,h,25339,rr,sat(x+mul(s->feedback[0],gain)));
  write_at(s,h,26710,y);s->damp[1]+=(((read_at(s,h,32959)-s->damp[1])>>1)*damp)>>14;
  y=ap(s,h,32960,36896,mul(s->damp[1],gain),diff);write_at(s,h,36897,y);
  s->feedback[0]=read_at(s,h,25338);s->feedback[1]=read_at(s,h,41584);
  int32_t wl=read_at(s,h,27104)+read_at(s,h,31117)-read_at(s,h,35795)+read_at(s,h,39855)-read_at(s,h,13508)-read_at(s,h,17435)-read_at(s,h,21406);
  int32_t wr=read_at(s,h,11082)+read_at(s,h,15934)-read_at(s,h,18978)+read_at(s,h,23787)-read_at(s,h,29838)-read_at(s,h,33456)-read_at(s,h,37076);
  wl=sat(((wl>>3)*19661)>>12);wr=sat(((wr>>3)*19661)>>12);
  int32_t peak=wl<0?-wl:wl;if(peak>s->peak)s->peak=peak;
  dr+=dd;we+=dw;l[n]=(int16_t)sat((a*(dr>>6)+(r?wl:((wl+wr)>>1))*(we>>6))>>15);
  if(r)r[n]=(int16_t)sat((b*(dr>>6)+wr*(we>>6))>>15);
  s->head=(h-1)&65535;
 }
 s->dry=dry;s->wet=wet;s->phase+=0x60ae00u;
}
