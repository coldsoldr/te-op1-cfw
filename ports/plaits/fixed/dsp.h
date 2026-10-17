// Shared fixed-point DSP pieces: interpolation, one-poles, soft clipping.

#ifndef OP1PLAITS_FIXED_DSP_H_
#define OP1PLAITS_FIXED_DSP_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

// stmlib::InterpolateHermite on a Q30 table, from x[-1] = t[0]; f Q31.
// Intermediate terms reach ~+-6 next to a table's discontinuity (lut_fold
// has one), so the arithmetic is Q28. Q28 result.
inline int32_t InterpolateHermiteQ28(const int32_t* t, int32_t f) {
  const int32_t xm1 = t[0] >> 2;
  const int32_t x0 = t[1] >> 2;
  const int32_t x1 = t[2] >> 2;
  const int32_t x2 = t[3] >> 2;
  const int32_t c = (x1 - xm1) >> 1;
  const int32_t v = x0 - x1;
  const int32_t w = c + v;
  const int32_t a = w + v + ((x2 - x0) >> 1);
  const int32_t b_neg = w + a;
  return Mul(Mul(Mul(a, f) - b_neg, f) + c, f) + x0;
}

// ONE_POLE(state, in, coefficient) with Q31 coefficient (any common format
// for state and in, |in - state| < 2^31).
inline void OnePole(int32_t& state, int32_t in, int32_t coefficient) {
  state += Mul(in - state, coefficient);
}

// The same for control-rate parameter smoothing: snaps to the target once
// the step underflows, as float does (upstream's lp reaches the target
// exactly; a truncating fixed-point one-pole stalls a few LSB short, which
// matters where the value is then quantised, e.g. the chord inversion).
inline void OnePoleSnap(int32_t& state, int32_t in, int32_t coefficient) {
  const int32_t step = Mul(in - state, coefficient);
  state = step == 0 ? in : state + step;
}

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_DSP_H_
