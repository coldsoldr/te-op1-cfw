// TAPE as an engine of the dirt category (op1/tape_fx.cc, the DSP in
// ../src/tape/tape.h): tape saturation with wow and flutter. One page:
// blue saturate, green wobble, white age, orange mix; no clicks of its own.

#ifndef OP1FX_OP1_TAPE_FX_H_
#define OP1FX_OP1_TAPE_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the dirt host (punch 0, drive 1, tape 2, fold 3, crush 4).
const int32_t kTapeEngineId = 2;

extern const Engine kTapeEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_TAPE_FX_H_
