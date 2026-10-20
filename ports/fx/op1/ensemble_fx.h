// ENSEMBLE as an engine of the modulation category (op1/ensemble_fx.cc,
// the DSP in ../src/ensemble/ensemble.h): chorus to string ensemble at half
// the effect rate. One page: blue rate, green depth, white voices, orange
// mix; no clicks of its own.

#ifndef OP1FX_OP1_ENSEMBLE_FX_H_
#define OP1FX_OP1_ENSEMBLE_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the modulation host (cwo 0, ensemble 1, shift 2).
const int32_t kEnsembleEngineId = 1;

extern const Engine kEnsembleEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_ENSEMBLE_FX_H_
