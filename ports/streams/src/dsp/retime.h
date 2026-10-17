// Retiming Streams' processors for another sample rate, and the port's exact
// speed-ups. Not upstream: added for the OP-1 port (ports/streams).
//
// Upstream runs every processor once per ADC sample at 31089 Hz (STM32F1 ADC
// scan of 6 channels at 12 MHz). OP1STREAMS_SAMPLE_RATE selects:
//   31089: upstream's code and tables, unchanged (the copy-check and the
//          bit-exactness checks use this);
//   44100: the OP-1 effect rate. Tables come from resources_44k.cc
//          (resources/gen_tables.py, upstream's formulas), constants below
//          are retimed so every time constant stays the same in seconds.
//   22050: the same, for processors run on every other sample of the 44.1
//          kHz stream (the core's cheap profile, kControlDecimation = 2):
//          resources_22k.cc.
//
// OP1STREAMS_FAST (0 or 1): exact replacements that give bit-identical
// results (checked against upstream by tests/copy_check.cc):
//   - Compressor::Log2 normalises with a count-leading-sign-bits instead of
//     shifting one bit per loop;
//   - 32x32 -> 64-bit products are written as such (int64 x int64 multiplies
//     call libgcc's __muldi3 on Blackfin).
//
// OP1STREAMS_CENTROID_STRIDE (1, 2, 4, 8, 16): the follower's spectral
// centroid is a division per sample; with a stride N it's computed every N
// samples and held in between. 1 is upstream (exact); the port uses 8, as
// libgcc's division costs hundreds of cycles on Blackfin. Not exact.

#ifndef OP1STREAMS_DSP_RETIME_H_
#define OP1STREAMS_DSP_RETIME_H_

#include <stdint.h>

#ifndef OP1STREAMS_SAMPLE_RATE
#define OP1STREAMS_SAMPLE_RATE 31089
#endif

#ifndef OP1STREAMS_FAST
#define OP1STREAMS_FAST 0
#endif

#ifndef OP1STREAMS_CENTROID_STRIDE
#define OP1STREAMS_CENTROID_STRIDE 1
#endif

#if OP1STREAMS_SAMPLE_RATE != 31089 && OP1STREAMS_SAMPLE_RATE != 44100 &&     OP1STREAMS_SAMPLE_RATE != 22050
#error "OP1STREAMS_SAMPLE_RATE must be 31089 (upstream), 44100 or 22050 (OP-1)"
#endif

namespace op1streams {

// round(y * (1 - 31089 / 44100)) = round(y * 0.29503), for any int32 y,
// without overflow: 9668 / 32768 = 0.295044.
inline int32_t RetimeComplement(int32_t y) {
  return (y >> 15) * 9668 + ((((y & 0x7fff) * 9668) + 16384) >> 15);
}

// One-pole step `x >> shift` per sample, as upstream writes its smoothers.
// At 44.1 kHz the step is upstream's floored `x >> shift`, scaled by
// 31089 / 44100 = 0.70497 with rounding (so the time constant in seconds is
// kept; the exact retimed fraction 1 - (1 - 2^-s)^0.70497 differs from the
// scaled one by under 0.3% for shifts >= 6). Rounding the scale keeps
// upstream's settling behaviour: a step of -1 stays -1 (the value creeps down
// to the target) and 0 stays 0. (A first version scaled with five floored
// shifts, which turns -1 into +1: Envelope's rate modulation then crept up
// and its decays ran 6% fast against upstream.) y must fit 32 bits: true for
// all of upstream's uses (follower spectrum errors are below 2^31 before the
// shift).
// round(y * (31089 / 22050 - 1)) = round(y * 0.40993): 13433 / 32768.
inline int32_t RetimeExtra(int32_t y) {
  return (y >> 15) * 13433 + ((((y & 0x7fff) * 13433) + 16384) >> 15);
}

template<typename T>
inline T Slew(T x, int shift) {
#if OP1STREAMS_SAMPLE_RATE == 31089
  return x >> shift;
#elif OP1STREAMS_SAMPLE_RATE == 44100
  int32_t y = static_cast<int32_t>(x >> shift);
  return static_cast<T>(y - RetimeComplement(y));
#else
  int32_t y = static_cast<int32_t>(x >> shift);
  return static_cast<T>(y + RetimeExtra(y));
#endif
}

// One-pole coefficients hard-coded in upstream's .cc files (Q31 fractions
// per sample), retimed with 1 - (1 - c)^(31089 / 44100) by gen_tables.py.
#if OP1STREAMS_SAMPLE_RATE == 31089
const int32_t kCompressorSidechainDecay = 14174;   // ~5 s
const int32_t kVactrolOvershootLp = 67976239;      // ~1 ms
const int32_t kVactrolSensitize = 138132;          // ~0.5 s
const int32_t kVactrolDesensitize = 1151;          // ~60 s
#else
#if OP1STREAMS_SAMPLE_RATE == 44100
const int32_t kCompressorSidechainDecay = 9992;
const int32_t kVactrolOvershootLp = 48147817;
const int32_t kVactrolSensitize = 97379;
const int32_t kVactrolDesensitize = 811;
#else  // 22050
const int32_t kCompressorSidechainDecay = 19984;
const int32_t kVactrolOvershootLp = 95216132;
const int32_t kVactrolSensitize = 194754;
const int32_t kVactrolDesensitize = 1623;
#endif
// Plucked vactrol's fast attack, upstream's int32 `<<= 4` (with its wrap at
// 0.1 and 0.2 ms) retimed: resources_44k.cc / _22k.cc, gen_tables.py.
extern const int32_t lut_vactrol_pluck_attack[];
#endif

// (a * b) >> 31 for 32-bit a and b, as upstream's
// `static_cast<int64_t>(a) * b >> 31` (exact, floor), for results that fit
// 32 bits. Built from four 16x16 partial products in 32-bit arithmetic:
// GCC 14 for Blackfin calls libgcc's __muldi3 for any 64-bit product, even
// of two sign-extended 32-bit values.
inline int32_t Mul32x32Shr31(int32_t a, int32_t b) {
  int32_t ah = a >> 16, bh = b >> 16;
  uint32_t al = static_cast<uint32_t>(a) & 0xffff;
  uint32_t bl = static_cast<uint32_t>(b) & 0xffff;
  uint32_t ll = al * bl;
  int32_t hl = ah * static_cast<int32_t>(bl);
  int32_t lh = static_cast<int32_t>(al) * bh;
  int32_t hh = ah * bh;
  // product = (hh + hl>>16 + lh>>16) 2^32 + (hl&ffff + lh&ffff + ll>>16) 2^16 + ll&ffff
  uint32_t t = (ll >> 16) + (static_cast<uint32_t>(hl) & 0xffff) +
      (static_cast<uint32_t>(lh) & 0xffff);
  int32_t upper = hh + (hl >> 16) + (lh >> 16) + static_cast<int32_t>(t >> 16);
  return static_cast<int32_t>((static_cast<uint32_t>(upper) << 1) | ((t >> 15) & 1));
}

// Count of redundant sign bits (Blackfin SIGNBITS), for x > 0: 30 - floor(log2 x).
// GCC maps __builtin_clrsb to SIGNBITS on Blackfin (checked with objdump).
inline int32_t NormPositive(int32_t x) {
  return __builtin_clrsb(x);
}

}  // namespace op1streams

#endif  // OP1STREAMS_DSP_RETIME_H_
