// CRUSH as an engine of the dirt category (op1/crush_fx.cc, the DSP in
// ../src/crush/crush.h): bit-depth and sample-rate reduction. One page:
// blue rate, green bits, white jitter, orange mix; no clicks of its own.

#ifndef OP1FX_OP1_CRUSH_FX_H_
#define OP1FX_OP1_CRUSH_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the dirt host (punch 0, drive 1, tape 2, fold 3, crush 4).
const int32_t kCrushEngineId = 4;

extern const Engine kCrushEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_CRUSH_FX_H_
