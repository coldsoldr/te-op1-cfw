/* Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware
 * permission; project-maintainer-authorized Maiko #250 port. See README.md. */
#ifndef PLATE_DSP_H
#define PLATE_DSP_H
#include <stdint.h>
typedef struct {
 uint32_t head,phase;
 int32_t input,damp[2],feedback[2],pre,dry,wet,peak;
 int16_t ring[65536];
} plate_state;
void plate_init(plate_state *s);
void plate_process(plate_state *s,int16_t *l,int16_t *r,const int32_t q[4]);
int32_t plate_sine(uint32_t phase);
#endif
