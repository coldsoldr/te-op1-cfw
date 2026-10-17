// Beta distribution sampling from Marbles' inverse-CDF tables, in fixed point
// (upstream: marbles/random/distributions.h). Called once per clock tick per
// channel, so it uses 64-bit intermediate arithmetic freely.
//
// The table lookup follows upstream step by step; the float roundings of the
// index computation (1 - u, u * 20, (u - 0.95) * 20) are emulated so the
// same table cells and positions are used. The interpolation itself is
// Q31 (finer than float), so results differ from upstream by about float's
// own rounding (~1e-7).
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_DISTRIBUTIONS_FX_H_
#define OP1MARBLES_FX_DISTRIBUTIONS_FX_H_

#include <stdint.h>

#include "src/fx.h"
#include "src/tables_fx.h"

namespace op1marbles {
namespace fx {

const int kNumBiasValues = 5;
const int kNumRangeValues = 9;

// a + (b - a) * frac / 2^bits, for |b - a| < 2^32 (truncated toward a).
inline int64_t Lerp64(int64_t a, int64_t b, uint32_t frac, int bits) {
  int64_t d = b - a;
  if (d >= 0) {
    return a + static_cast<int64_t>(Mul64(static_cast<uint32_t>(d), frac) >> bits);
  }
  return a - static_cast<int64_t>(Mul64(static_cast<uint32_t>(-d), frac) >> bits);
}

// Interpolate(table, u, 128) for u given as a Q32 value (64-bit: the
// emulated float can reach 1.0). Table and result Q31 in [0, 2^31].
inline int64_t InterpolateIcdf(const uint32_t* table, uint64_t u) {
  uint64_t index = u << 7;  // x 128
  uint32_t i = static_cast<uint32_t>(index >> 32);
  uint32_t frac = static_cast<uint32_t>(index);
  if (i >= 128) {  // u == 1.0: upstream reads table[128] + 0 * (...)
    return table[128];
  }
  return Lerp64(table[i], table[i + 1], frac, 32);
}

// BetaDistributionSample(uniform, spread, bias). u Q32 (an exact upstream
// float), spread and bias Q16. Result Q31 in [0, 2^31].
inline uint32_t BetaDistributionSample(uint32_t u_in, int32_t spread, int32_t bias) {
  uint64_t u = u_in;
  bool flip = bias > kQ16One / 2;
  if (flip) {
    u = RoundToFloat((1ULL << 32) - u);  // 1.0f - uniform
    bias = kQ16One - bias;
  }
  int32_t b = bias * ((kNumBiasValues - 1) * 2);  // exact, as in float
  int32_t s = spread * (kNumRangeValues - 1);
  int32_t bi = b >> 16;
  uint32_t bf = static_cast<uint32_t>(b & 0xffff);
  int32_t si = s >> 16;
  uint32_t sf = static_cast<uint32_t>(s & 0xffff);
  int cell = bi * (kNumRangeValues + 1) + si;

  int offset = 0;
  if (u <= k005fQ32) {
    offset = 129;
    u = RoundToFloat(u * 20);
  } else if (u >= k095fQ32) {
    offset = 2 * 129;
    u = RoundToFloat((u - k095fQ32) * 20);  // the subtraction is exact
  }
  int64_t x1y1 = InterpolateIcdf(distributions_table[cell] + offset, u);
  int64_t x2y1 = InterpolateIcdf(distributions_table[cell + 1] + offset, u);
  int64_t x1y2 = InterpolateIcdf(distributions_table[cell + kNumRangeValues + 1] + offset, u);
  int64_t x2y2 = InterpolateIcdf(distributions_table[cell + kNumRangeValues + 2] + offset, u);

  int64_t y1 = Lerp64(x1y1, x2y1, sf, 16);
  int64_t y2 = Lerp64(x1y2, x2y2, sf, 16);
  int64_t y = Lerp64(y1, y2, bf, 16);
  if (y < 0) y = 0;
  if (y > (1LL << 31)) y = 1LL << 31;
  if (flip) {
    y = (1LL << 31) - y;
  }
  return static_cast<uint32_t>(y);
}

// Pre-computed beta(3, 3) with a fatter tail (jitter). u Q32, result Q31.
inline uint32_t FastBetaDistributionSample(uint32_t u) {
  return static_cast<uint32_t>(InterpolateIcdf(dist_icdf_4_3, u));
}

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_DISTRIBUTIONS_FX_H_
