// Fixed-point helpers for the Clouds port, on top of Rings' primitives
// (ports/rings/src/fixed.h, ffloat.h; shared by path, unmodified).
//
// Formats:
//   audio:       Q27 int32 (1.0 = 2^27, +-16 of headroom), as in Rings;
//                buffers in memory are int16 (upstream's 16-bit format,
//                sample = Q27 >> 12);
//   parameters:  Q16 (1.0 = 65536); pitch in semitones, Q16;
//   coefficients Q31 unless noted.

#ifndef OP1CLOUDS_CORE_DSP_FIXED_H_
#define OP1CLOUDS_CORE_DSP_FIXED_H_

#include <cstdint>

#include "rings/src/ffloat.h"
#include "rings/src/fixed.h"
#include "rings/src/fixed_tables.h"
#include "src/core/clouds_tables.h"

namespace op1clouds {

using op1rings::FFloat;
using op1rings::MulQ31;
using op1rings::MulQ31Fast;
using op1rings::ShlSat;

const int32_t kQ27One = 1 << 27;
const int32_t kQ31One = 0x7fffffff;  // "1.0" in Q31

// (a * b) >> 28 for a Q28 coefficient b (|b| < 8): the fast product, then
// << 3 (a's low 3 bits are lost: 2^-24 of full scale).
inline int32_t MulQ28(int32_t a, int32_t b) {
  return MulQ31Fast(a, b) << 3;
}

// Clamp to int16. On Blackfin: one saturating shift and one shift.
inline int32_t Sat16(int32_t x) {
  return ShlSat(x, 16) >> 16;
}

// Q27 -> int16 sample, upstream's x * 32768 (floored; upstream truncates
// towards 0: 1 LSB), clipped.
inline int16_t Q27ToS16(int32_t x) {
  return static_cast<int16_t>(ShlSat(x, 4) >> 16);
}

// Q27 -> int16 sample, upstream's x * 32767 (the record buffer's fast write
// path and its crossfade tail), clipped.
inline int16_t Q27ToS16Scaled32767(int32_t x) {
  return static_cast<int16_t>(ShlSat(x - (x >> 15), 4) >> 16);
}

// stmlib::SoftLimit for x in Q27, by Rings' table over [0, 4] (clamped
// beyond, where upstream's curve is within 0.6% of 1).
inline int32_t SoftLimitQ27(int32_t x) {
  int32_t a = x < 0 ? -x : x;
  int32_t y;
  if (a >= (4 << 27)) {
    y = op1rings::kSoftLimitQ30[256];
  } else {
    const int32_t t = a << 2;                      // a / 4 in Q31
    const int32_t i = t >> 23;
    const int32_t frac = (t & 0x7fffff) << 8;
    y = op1rings::kSoftLimitQ30[i] +
        MulQ31Fast(op1rings::kSoftLimitQ30[i + 1] - op1rings::kSoftLimitQ30[i], frac);
  }
  y >>= 3;                                         // Q30 -> Q27
  const int32_t sign = x >> 31;
  return (y ^ sign) - sign;
}

// stmlib::Random: the same LCG and seed, so grains are scheduled from the
// same random sequence as upstream (one generator per Clouds instance;
// upstream has one global).
struct Lcg {
  uint32_t state;
  void Init() { state = 0x21; }
  uint32_t Next() {
    state = state * 1664525u + 1013904223u;
    return state;
  }
};

// stmlib::SemitonesToRatio, from upstream's two tables (high: integer
// semitones + 128; low: 1/256 semitone), semitones in Q16, as an FFloat.
inline FFloat SemitonesToRatio(int32_t semitones_q16) {
  int32_t p = semitones_q16 + (128 << 16);
  if (p < 0) p = 0;
  if (p > (255 << 16) + 0xffff) p = (255 << 16) + 0xffff;
  const int32_t i = p >> 16;
  const int32_t j = (p >> 8) & 255;
  return FFloat::FromParts(kPitchRatioHighM[i], kPitchRatioHighE[i]) *
         FFloat::FromParts(kPitchRatioLowQ30[j], -30);
}

// Linear interpolation in a Q-format table with `bits` index bits over
// [0, 1]: index in Q16 (as upstream's Interpolate(table, x, 2^bits)).
inline int32_t InterpolateQ16(const int32_t* table, int32_t x_q16, int bits) {
  const int32_t scaled = x_q16 << bits;            // Q16 index
  const int32_t i = scaled >> 16;
  const int32_t frac = (scaled & 0xffff) << 15;    // Q31
  return table[i] + MulQ31Fast(table[i + 1] - table[i], frac);
}

inline int32_t ClampQ16(int32_t x, int32_t lo, int32_t hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// Q16 parameter -> FFloat.
inline FFloat Q16ToFFloat(int32_t x) {
  return FFloat::FromQ(x, 16);
}

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_DSP_FIXED_H_
