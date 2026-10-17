// Fixed-point helpers for the Marbles port. Everything is portable integer
// arithmetic (no Blackfin builtins, no 64-bit multiplies, which GCC turns
// into __muldi3 calls on Blackfin), so host and Blackfin builds compute
// bit-identical results.
//
// Formats used in the port:
//   Q32 (uint32):  unit fractions: uniforms, phases, ramp values, x / 2^32
//   Q31 (uint32):  unit values that must reach 1.0 (distribution samples)
//   Q16 (int32):   controls (bias, spread, steps, ...), 1.0 = 65536
//   Q16 volts:     outputs, 1 V = 65536 (1 semitone = 5461.33)
//   Q8 semitones:  the T rate
//   Q16.16:        ratios (jitter multiplier, exp2 results)
//
// Copyright 2026 (port). Based on Mutable Instruments Marbles (MIT,
// (c) 2015 Emilie Gillet). MIT licence, as upstream.

#ifndef OP1MARBLES_FX_FX_H_
#define OP1MARBLES_FX_FX_H_

#include <stdint.h>

namespace op1marbles {
namespace fx {

const int32_t kQ16One = 65536;

// Full 32 x 32 -> 64-bit unsigned product from four 16 x 16 products.
inline uint64_t Mul64(uint32_t a, uint32_t b) {
  uint32_t ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
  uint64_t r = static_cast<uint64_t>(ah * bh) << 32;
  r += static_cast<uint64_t>(ah * bl) << 16;
  r += static_cast<uint64_t>(al * bh) << 16;
  r += al * bl;
  return r;
}

// floor(a * b / 2^32).
inline uint32_t MulU32(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>(Mul64(a, b) >> 32);
}

// Signed a times unsigned b, shifted right by `shift` (truncated toward
// zero, same on every platform).
inline int64_t MulS64Shift(int32_t a, uint32_t b, int shift) {
  if (a >= 0) {
    return static_cast<int64_t>(Mul64(static_cast<uint32_t>(a), b) >> shift);
  }
  return -static_cast<int64_t>(Mul64(static_cast<uint32_t>(-static_cast<int64_t>(a)), b) >> shift);
}

// a + (b - a) * t for Q31 t in [0, 2^31] (a, b any int32 with |b - a| < 2^31).
inline int32_t CrossfadeQ31(int32_t a, int32_t b, uint32_t t) {
  return a + static_cast<int32_t>(MulS64Shift(b - a, t, 31));
}

// a + (b - a) * t for Q16 t in [0, 65536].
inline int32_t CrossfadeQ16(int32_t a, int32_t b, int32_t t) {
  return a + static_cast<int32_t>(MulS64Shift(b - a, static_cast<uint32_t>(t), 16));
}

inline int32_t Clamp(int32_t x, int32_t lo, int32_t hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// --- Cheap 32-bit products for per-sample code -------------------------------

// About a * b / 2^32: three 16 x 16 products, the low x low one dropped
// (result low by at most 2).
inline uint32_t MulHiU32(uint32_t a, uint32_t b) {
  uint32_t ah = a >> 16, al = a & 0xffff, bh = b >> 16, bl = b & 0xffff;
  return ah * bh + ((ah * bl) >> 16) + ((al * bh) >> 16);
}

// The same for a signed a (rounded toward zero).
inline int32_t MulHiS32(int32_t a, uint32_t b) {
  return a >= 0 ? static_cast<int32_t>(MulHiU32(static_cast<uint32_t>(a), b))
                : -static_cast<int32_t>(MulHiU32(static_cast<uint32_t>(-a), b));
}

// 64-bit shifts by a variable amount (0..63) without libgcc calls.
inline uint64_t Shr64(uint64_t x, int s) {
  uint32_t hi = static_cast<uint32_t>(x >> 32), lo = static_cast<uint32_t>(x);
  if (s == 0) return x;
  if (s >= 32) return hi >> (s - 32);
  return (static_cast<uint64_t>(hi >> s) << 32) | ((lo >> s) | (hi << (32 - s)));
}

inline uint64_t Shl64(uint64_t x, int s) {
  uint32_t hi = static_cast<uint32_t>(x >> 32), lo = static_cast<uint32_t>(x);
  if (s == 0) return x;
  if (s >= 32) return static_cast<uint64_t>(lo << (s - 32)) << 32;
  return (static_cast<uint64_t>((hi << s) | (lo >> (32 - s))) << 32) | (lo << s);
}

// --- Float rounding emulation ----------------------------------------------
// Marbles draws its uniforms as float(word) / 2^32 and moves them between
// float and integer form (NextVector, the deja vu replay hash). Emulating the
// float32 rounding of those conversions keeps the port's random stream
// bit-identical to upstream's, so the deja vu loops, patterns and decisions
// match exactly (see docs/marbles-port.md).

// Leading zeros of a 32-bit value (32 for 0). __builtin_clz is a libgcc call
// (__clzsi2) on Blackfin; __builtin_clrsb is the SIGNBITS instruction.
inline int Clz32(uint32_t x) {
  if (x >> 31) return 0;
  return __builtin_clrsb(static_cast<int32_t>(x)) + 1;
}

inline int Clz64(uint64_t x) {  // x != 0
  uint32_t hi = static_cast<uint32_t>(x >> 32);
  return hi ? Clz32(hi) : 32 + Clz32(static_cast<uint32_t>(x));
}

// The value of float(x) for a nonnegative integer x (round to nearest, ties
// to even), returned at the same scale. Scale-invariant, so it also rounds
// any nonnegative fixed-point number to float precision.
inline uint64_t RoundToFloat(uint64_t x) {
  uint32_t hi = static_cast<uint32_t>(x >> 32), lo = static_cast<uint32_t>(x);
  int bits = hi ? 64 - Clz32(hi) : 32 - Clz32(lo);
  int drop = bits - 24;  // bits beyond the 24 significant ones
  if (drop <= 0) return x;
  if (drop < 32) {  // the usual case: everything happens in the low word
    uint32_t one = 1u << drop, half = one >> 1;
    uint32_t low = lo & (one - 1);
    lo -= low;
    if (low > half || (low == half && (lo & one))) {
      uint32_t n = lo + one;
      if (n < lo) ++hi;
      lo = n;
    }
    return (static_cast<uint64_t>(hi) << 32) | lo;
  }
  // drop >= 32: the low word goes entirely.
  int d = drop - 32;
  uint32_t one = 1u << d, half = one >> 1;
  uint32_t low = hi & (one - 1);
  hi -= low;
  bool up = d == 0 ? (lo > 0x80000000u || (lo == 0x80000000u && (hi & 1)))
                   : (low > half || (low == half && (lo != 0 || (hi & one))));
  if (d == 0) {
    if (up) ++hi;
  } else if (up) {
    hi += one;
  }
  return static_cast<uint64_t>(hi) << 32;
}

inline int64_t RoundToFloatSigned(int64_t x) {
  return x < 0 ? -static_cast<int64_t>(RoundToFloat(static_cast<uint64_t>(-x)))
               : static_cast<int64_t>(RoundToFloat(static_cast<uint64_t>(x)));
}

// float(word) / 2^32 as Q32. float(word) rounds up to 2^32 (1.0f) for words
// >= 0xffffff80 (p = 2^-25); that case saturates to 0xffffffff here.
inline uint32_t WordToUnit(uint32_t w) {
  if (w < (1u << 24)) return w;
  int drop = 8 - Clz32(w);
  uint32_t one = 1u << drop, half = one >> 1;
  uint32_t low = w & (one - 1), r = w - low;
  if (low > half || (low == half && (r & one))) {
    r += one;
    if (r == 0) r = 0xffffffffu;
  }
  return r;
}

// A float32 value held exactly as m * 2^e (m < 2^24, normalized to
// >= 2^23 unless zero). Used where the port reproduces a float product of
// upstream's exactly (the T clock frequency).
struct Float24 {
  uint32_t m;
  int32_t e;
};

// A nonnegative integer x (value x * 2^e) rounded to float precision and
// normalized.
inline Float24 Float24Normalize(uint64_t x, int32_t e) {
  Float24 r;
  r.e = e;
  if (!x) {
    r.m = 0;
    return r;
  }
  x = RoundToFloat(x);
  int shift = 40 - Clz64(x);  // bits beyond 24 (zeros after the rounding)
  if (shift > 0) {
    x = Shr64(x, shift);
    r.e += shift;
  } else if (shift < 0) {
    x <<= -shift;  // x < 2^24 here: 32-bit
    r.e += shift;
  }
  r.m = static_cast<uint32_t>(x);
  return r;
}

// a * b with float rounding.
inline Float24 Float24Mul(Float24 a, Float24 b) {
  return Float24Normalize(Mul64(a.m, b.m), a.e + b.e);
}

// 1 / a with float rounding (round to nearest even), a != 0.
inline Float24 Float24Reciprocal(Float24 a) {
  // 2^48 / m for a normalized m in [2^23, 2^24): quotient in (2^24, 2^25].
  uint64_t num = 1ULL << 48;
  uint64_t q = num / a.m, rem = num - q * a.m;
  // q has 25 bits (or is 2^25 for m = 2^23): round to 24 using the
  // remainder for exact ties.
  int32_t e = -48 - a.e;
  if (q >= (1ULL << 25)) {  // m == 2^23: exact power of two
    return Float24Normalize(q, e);
  }
  uint64_t half_bit = q & 1;
  q >>= 1;
  e += 1;
  if (half_bit && (rem != 0 || (q & 1))) q += 1;
  return Float24Normalize(q, e);
}

// A nonnegative fixed-point value (frac_bits fractional bits) as a float.
inline Float24 Float24FromFixed(uint64_t x, int frac_bits) {
  return Float24Normalize(x, -frac_bits);
}

// The value as a fixed-point number with `frac_bits` fractional bits
// (truncated; exact when the float's lowest bit is >= 2^-frac_bits).
inline uint64_t Float24ToFixed(Float24 a, int frac_bits) {
  int s = a.e + frac_bits;
  if (s >= 0) return Shl64(a.m, s);
  if (s <= -32) return 0;
  return a.m >> -s;
}

// --- Float arithmetic on fixed-point values ----------------------------------
// The T clock keeps its phases as Q40 integers that hold float values; these
// apply upstream's float roundings to them. Exact while every operand and
// result is a float whose lowest bit is >= 2^-40, i.e. above ~2^-16 in
// magnitude (tiny phases or phase differences are rounded slightly
// differently: harmless).

// a + b with float rounding.
inline int64_t FloatAdd(int64_t a, int64_t b) {
  return RoundToFloatSigned(a + b);
}

// x * r with float rounding; x is a float-valued Q40 number, r a Float24.
inline int64_t FloatMul(int64_t x, Float24 r) {
  bool negative = x < 0;
  uint64_t ax = negative ? static_cast<uint64_t>(-x) : static_cast<uint64_t>(x);
  if (!ax || !r.m) return 0;
  int xs = 40 - Clz64(ax);  // reduce x to its 24 significant bits
  if (xs < 0) xs = 0;
  uint32_t xm = static_cast<uint32_t>(Shr64(ax, xs));
  uint64_t p = RoundToFloat(Mul64(xm, r.m));
  int s = xs + r.e;
  uint64_t v = s >= 0 ? Shl64(p, s) : (s <= -64 ? 0 : Shr64(p, -s));
  return negative ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
}

extern const Float24 lut_pitch_ratio_high[256];
extern const Float24 lut_pitch_ratio_low[256];

// stmlib's SemitonesToRatio for semitones in Q8 (|s| < 128): the table
// indices are exact for Q8 inputs, the product is rounded as a float.
inline Float24 SemitonesToRatioFloat(int32_t semitones_q8) {
  int32_t pitch = semitones_q8 + (128 << 8);  // >= 0 for s > -128
  return Float24Mul(lut_pitch_ratio_high[pitch >> 8], lut_pitch_ratio_low[pitch & 0xff]);
}

// --- Exponentials -----------------------------------------------------------

extern const uint32_t lut_exp2_q30[257];

// 2^(x / 65536) for x in Q16 octaves, split into a Q30 mantissa (1..2) and
// an integer octave (linear interpolation in a 257-entry table: relative
// error < 4e-6).
inline uint32_t Exp2Split(int32_t x, int32_t* octave) {
  *octave = x >> 16;  // floor
  uint32_t frac = static_cast<uint32_t>(x) & 0xffff;
  uint32_t i = frac >> 8, f = frac & 0xff;
  uint32_t a = lut_exp2_q30[i], b = lut_exp2_q30[i + 1];
  return a + (((b - a) * f) >> 8);
}

// 2^(x / 65536) as Q16.16, for -16 < x / 65536 < 15.
inline uint32_t Exp2Q16(int32_t x) {
  int32_t octave;
  uint32_t m = Exp2Split(x, &octave);
  int shift = 14 - octave;
  if (shift >= 32) return 0;
  return shift >= 0 ? (m >> shift) : (m << -shift);
}

// Semitones (Q8, |s| < 2^24 / 64) to octaves (Q16): s * 65536 / 3072.
inline int32_t SemitonesQ8ToOctavesQ16(int32_t semitones_q8) {
  int32_t num = semitones_q8 * 64;
  return num >= 0 ? num / 3 : -((-num + 2) / 3);  // floor
}

// 2^(semitones / 12) for semitones in Q8 (Marbles' SemitonesToRatio takes
// 1/256-semitone steps too), as Q16.16.
inline uint32_t SemitonesToRatioQ16(int32_t semitones_q8) {
  return Exp2Q16(SemitonesQ8ToOctavesQ16(semitones_q8));
}

// x * 2^(semitones / 12) for a Q32 x, keeping full precision.
inline uint32_t ScaleBySemitones(uint32_t x, int32_t semitones_q8) {
  int32_t octave;
  uint32_t m = Exp2Split(SemitonesQ8ToOctavesQ16(semitones_q8), &octave);
  uint64_t p = Mul64(x, m);  // Q62 relative to x's scale * 2^30
  int shift = 30 - octave;
  if (shift <= 0) return 0xffffffffu;
  if (shift >= 64) return 0;
  uint64_t r = p >> shift;
  return r > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(r);
}

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_FX_H_
