// HALL as an engine of the reverb category (op1/hall_fx.cc, the core in
// ../src/hall/hall.h): an 8-line feedback delay network at half the effect
// rate.
//   page 1: blue size, green decay (infinite at the top), white damp, orange mix
//   page 2: blue pre-delay, green early, white motion, orange width
//   clicks: green freeze (not saved), white page

#ifndef OP1FX_OP1_HALL_FX_H_
#define OP1FX_OP1_HALL_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the reverb host (spring 0, plate 1, hall 2, clouds 3).
const int32_t kHallEngineId = 2;

extern const Engine kHallEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_HALL_FX_H_
