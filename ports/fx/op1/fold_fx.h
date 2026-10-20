// FOLD as an engine of the dirt category (op1/fold_fx.cc, the DSP in
// ../src/fold/fold.h): an anti-aliased wavefolder. One page: blue fold,
// green symmetry (centre symmetric), white tone, orange mix; no clicks of
// its own.

#ifndef OP1FX_OP1_FOLD_FX_H_
#define OP1FX_OP1_FOLD_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the dirt host (punch 0, drive 1, tape 2, fold 3, crush 4).
const int32_t kFoldEngineId = 3;

extern const Engine kFoldEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_FOLD_FX_H_
