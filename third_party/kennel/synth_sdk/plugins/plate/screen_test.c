/* Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with OP-1 firmware permission. */
/* Native text remains a real-firmware capture gate. */
#define main unused_screen_harness_main
#include "../../harness/screen_harness.c"
#undef main
#include <assert.h>
extern volatile uint32_t plate_blocks[3];
int main(void){
 SCREEN.init(0);
 /* No render callbacks: explicitly exercise the activity timeout/dim branch. */
 for(unsigned n=0;n<8;n++)SCREEN.update(0,1);
 unsigned dim=0;
 for(int i=0;i<nlines;i++){
  int h=lines[i].w/2;
  if(lines[i].c[0]==0&&lines[i].c[1]==0&&lines[i].c[2]==0&&lines[i].c[3]==90){
   /* Full-screen veil spans the first/last pixel centres. Its outer half stroke
    * is intentionally clipped at the canvas edge, like a filled rectangle. */
   assert(lines[i].x0==0&&lines[i].x1==319*1024);dim++;continue;
  }
  assert(lines[i].x0-h>=0&&lines[i].x0+h<320*1024);
  assert(lines[i].x1-h>=0&&lines[i].x1+h<320*1024);
  assert(lines[i].y0-h>=0&&lines[i].y0+h<160*1024);
  assert(lines[i].y1-h>=0&&lines[i].y1+h<160*1024);
 }
 assert(dim==160);puts("Plate idle/bypass veil bounds PASS; border half-stroke deliberately clipped");
}
