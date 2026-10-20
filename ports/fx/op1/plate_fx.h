// PLATE as an engine of the reverb category (op1/plate_fx.cc, the core in
// ../src/plate/plate.h): a Dattorro plate at half the effect rate.
//   page 1: blue decay, green damp, white pre-delay, orange mix
//   page 2: blue size (8 steps), green diffusion, white motion, orange low cut
//   clicks: green freeze (not saved), white page

#ifndef OP1FX_OP1_PLATE_FX_H_
#define OP1FX_OP1_PLATE_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the reverb host (spring 0, plate 1, hall 2, clouds 3).
const int32_t kPlateEngineId = 1;

extern const Engine kPlateEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_PLATE_FX_H_
