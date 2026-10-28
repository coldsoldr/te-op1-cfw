/* Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware
 * permission; project-maintainer-authorized Maiko #250 port. See README.md. */
#include "op1_plugin.h"
#include "op1_synth.h"
#include "plate_dsp.h"
#include "plate_art.h"
/* OLED correction: total lift 12px; lower 40px readouts get another 4px. */
#define SCREEN_LIFT 12
static unsigned frame,idle;
static int32_t envelope;
extern volatile uint32_t plate_blocks[3];
extern volatile int32_t plate_peak[3];
static uint32_t previous;
#ifndef OP1_HOST
static uint8_t texts[9][OP1_TEXT_SIZE] __attribute__((aligned(4)));
static uint8_t made[9];
static char last[9][12];
#endif
static void line(void *r,int x,int y,int xx,int yy,const uint8_t *c){
 op1_pt a={x*64,(y-SCREEN_LIFT*16)*64},b={xx*64,(yy-SCREEN_LIFT*16)*64};op1_raster_line(r,&a,&b,768,c);
}
static void box(void *r,int x,int y,int xx,int yy,const uint8_t *c){
 line(r,x,y,xx,y,c);line(r,xx,y,xx,yy,c);line(r,xx,yy,x,yy,c);line(r,x,yy,x,y,c);
}
static void label(void *r,unsigned n,int x,int y,int size,const char *s,const uint8_t *c){
#ifndef OP1_HOST
 static const int32_t ident[9]={1024,0,0,0,1024,0,0,0,1024};
 if(!made[n]){op1_text_ctor(texts[n],OP1_Q10(x),y-SCREEN_LIFT,size,c);made[n]=1;}
 unsigned i=0;int changed=0;while(i<11){if(last[n][i]!=s[i])changed=1;if(!s[i])break;i++;}
 if(changed){i=0;while(i<11&&s[i]){last[n][i]=s[i];i++;}last[n][i]=0;op1_text_set(texts[n],last[n]);}
 op1_text_draw(texts[n],r,ident);
#else
 (void)r;(void)n;(void)x;(void)y;(void)size;(void)s;(void)c;
#endif
}
static void init(void *s){(void)s;frame=idle=0;envelope=0;previous=plate_blocks[(unsigned)op1_fx_screen_pos()%3];}
static void update(void *s,int dt){
 int16_t k[4];op1_fx_knobs_live(k);op1_screen_draw(s,dt);void *r=op1_raster();
 /* R246 gives every SVG rectangle its own 2 KB raster chunk. Draw the same
  * filled quad through the canvas's shared path, exactly as raster_line does. */
 for(unsigned n=0;n<sizeof plate_art/sizeof plate_art[0];n++){
#ifdef OP1_HOST
  op1_pt a={plate_art[n].x*1024,(plate_art[n].y+plate_art[n].yy-2*SCREEN_LIFT)*512};
  op1_pt b={plate_art[n].xx*1024,a.y};
  uint8_t c[4]={plate_art[n].r,plate_art[n].g,plate_art[n].b,255};
  op1_raster_line(r,&a,&b,(plate_art[n].yy-plate_art[n].y)*1024,c);
#else
  uint8_t *path=(uint8_t *)r+0x30;path[0x24]=1;
  for(unsigned corner=0;corner<5;corner++){
   op1_pt a={(corner==1||corner==2?plate_art[n].xx:plate_art[n].x)*1024,
             ((corner==2||corner==3?plate_art[n].yy:plate_art[n].y)-SCREEN_LIFT)*1024},b;
   ((void (*)(op1_pt *,const void *,const op1_pt *))0x01A231B0)(&b,(const void *)0x01DE5E68,&a);
   if(!corner)((void (*)(void *,int,int))0x019149C4)(path,(int16_t)(b.x>>5),(int16_t)(b.y>>5));
   else ((void (*)(void *,int,int))0x01914904)(path,(int16_t)(b.x>>5),(int16_t)(b.y>>5));
  }
  const uint8_t bgra[4]={plate_art[n].b,plate_art[n].g,plate_art[n].r,255};
  ((void (*)(void *,void *,const uint8_t *))0xFFA055B4)(path,(uint8_t *)r+0x28,bgra);
#endif
 }
 const uint8_t colors[4][4]={{107,142,255,255},{0,239,148,255},{255,255,255,255},{255,0,0,255}};
 const char *names[4]={"decay","tone","pre","mix"};
 for(unsigned i=0;i<4;i++){
  int x=i<2?14:246,y=(i&1)?92:16;char v[3];unsigned value=((unsigned)k[i]*99+16383)>>15;
  v[0]=(char)('0'+value/10);v[1]=(char)('0'+value%10);v[2]=0;
  label(r,i*2,x,y,14,names[i],colors[i]);const uint8_t white[4]={255,255,255,255};label(r,i*2+1,x,y+18,40,v,white);
 }
 /* Recovered driver ripple rectangles, clipped to the plate's interior. */
 unsigned shown=(unsigned)op1_fx_screen_pos()%3;int32_t peak=plate_peak[shown];
 envelope=peak>envelope?peak:envelope-(envelope>>4);int brightness=envelope>>3;if(brightness>195)brightness=195;
 int max=((k[0]*60>>15)+30)*16;
 for(int n=0;n<90*16;n+=15*16){
  int radius=(n+(int)(frame*12))%(90*16);if(radius<=32||radius>=max)continue;
  int x=132*16-radius,xx=132*16+radius,y=80*16-(radius*13>>4),yy=80*16+(radius*13>>4);
  if(x<105*16)x=105*16;if(xx>215*16)xx=215*16;if(y<35*16)y=35*16;if(yy>125*16)yy=125*16;
  uint8_t c[4]={107,142,255,(uint8_t)((brightness+60)*(max-radius)/max)};box(r,x,y,xx,yy,c);
 }
 int cover=(32767-k[1])*64>>15;
 if(cover>2){
  int left=214-cover;const uint8_t black[4]={0,0,0,255};
  for(int y=40;y<=120;y++)line(r,left*16,y*16,212*16,y*16,black);
  box(r,left*16,40*16,212*16,120*16,colors[1]);
  for(int x=left-80;x<212;x+=8){
   int a=x,b=x+80,ya=120,yb=40;
   if(a<left){ya-=left-a;a=left;}if(b>212){yb+=b-212;b=212;}
   if(a<=b)line(r,a*16,ya*16,b*16,yb*16,colors[1]);
  }
 }
 uint32_t ph=0,step=(uint32_t)(((k[2]*8>>15)+1)*0x5f42b80u);int py=80*16;
 for(int x=84;x<=128;x++){
  int amp=x<88?(x-84)*12:x>124?(128-x)*12:48;
  int yy=80*16+(plate_sine(ph)*amp>>15);if(x>84)line(r,(x-1)*16,py,x*16,yy,colors[2]);py=yy;ph+=step;
 }
 unsigned pos=(unsigned)op1_fx_screen_pos()%3;uint32_t blocks=plate_blocks[pos];
 if(blocks==previous)idle++;else idle=0;previous=blocks;
 if(idle>3){const uint8_t dim[4]={0,0,0,90};for(int y=0;y<160;y++)line(r,0,(y+SCREEN_LIFT)*16,319*16,(y+SCREEN_LIFT)*16,dim);label(r,8,110,76,20,"fx off",colors[0]);}
 frame++;
}
const op1_screen_t SCREEN_plate={init,update};
