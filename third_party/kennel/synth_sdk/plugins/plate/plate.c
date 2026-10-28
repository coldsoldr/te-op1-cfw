/* Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware
 * permission; project-maintainer-authorized Maiko #250 port. See README.md. */
#include "op1_plugin.h"
#include "plate_dsp.h"
_Static_assert(sizeof(plate_state)<=OP1_FX_SCRATCH_BYTES,"Plate scratch overflow");
volatile uint32_t plate_blocks[3];
volatile int32_t plate_peak[3];
static plate_state *state(int p){return (plate_state *)op1_fx_scratch(p);}
static void init(int p){plate_state *s=state(p);if(s)plate_init(s);}
static void stereo(int p,int16_t *l,int16_t *r,const int16_t *params){
 plate_state *s=state(p);if(!s)return;int32_t q[4];
 for(int i=0;i<4;i++)q[i]=op1_fx_param_q15(params,i);
 plate_process(s,l,r,q);plate_peak[p]=s->peak;plate_blocks[p]++;
}
static void mono(int p,int16_t *b,const int16_t *q){stereo(p,b,0,q);}
const op1_fx_stereo_t FX_plate={{init,mono,{0,0,0,0}},stereo};
