// SHIFT as an engine of the modulation category (op1/shift_fx.cc, the DSP
// in ../src/shift/shift.h): a frequency shifter with barber-pole feedback
// and stereo spread, or a ring modulator. One page: blue shift (centre
// 0 Hz), green feedback, white spread, orange mix; blue click: frequency
// shift <-> ring mod (saved: values[4] bit 0).

#ifndef OP1FX_OP1_SHIFT_FX_H_
#define OP1FX_OP1_SHIFT_FX_H_

#include "fx/op1/fx_host.h"

namespace op1fx {

// Its id in the modulation host (cwo 0, ensemble 1, shift 2).
const int32_t kShiftEngineId = 2;

extern const Engine kShiftEngine;

}  // namespace op1fx

#endif  // OP1FX_OP1_SHIFT_FX_H_
