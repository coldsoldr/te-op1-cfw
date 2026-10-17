// Fixed-point helpers for the Plaits port.
//
// Formats used throughout fixed/:
//   - parameters (harmonics, timbre, morph, accent): Q15, 0..32768 (1.0);
//     the OP-1's encoders give 0..32767;
//   - notes: Q16 semitones (MIDI note numbers);
//   - frequencies: f / fs as a Q32 phase increment (uint32; < 2^31 for
//     f < fs / 2, so they also fit an int32);
//   - phases: Q32 (uint32, wrapping);
//   - engine outputs: Q26 (+-32; Plaits' engines reach ~+-20 before their
//     limiters);
//   - coefficients and gains: Q31 unless noted.
//
// The multiply primitives come from the Rings port (ports/rings/src/fixed.h):
// they map to Blackfin fract builtins / inline assembly and are emulated
// bit-exactly on the host, so host renders predict the device.

#ifndef OP1PLAITS_FIXED_FIXED_MATH_H_
#define OP1PLAITS_FIXED_FIXED_MATH_H_

#include <cstddef>
#include <cstdint>

#include "../../rings/src/fixed.h"
#include "../../rings/src/ffloat.h"
#include "fixed/tables.h"

#ifndef PLAITS_SAMPLE_RATE
#define PLAITS_SAMPLE_RATE 48000
#endif

#ifndef PLAITS_BLOCK_SIZE
#define PLAITS_BLOCK_SIZE 12
#endif

namespace op1plaits {

using op1rings::FFloat;
using op1rings::MulQ31;
using op1rings::MulQ31Fast;
using op1rings::ShlSat;
using op1rings::AddSat;

const int32_t kSampleRate = PLAITS_SAMPLE_RATE;
const size_t kBlockSize = PLAITS_BLOCK_SIZE;
const size_t kMaxBlockSize = 24 > PLAITS_BLOCK_SIZE ? 24 : PLAITS_BLOCK_SIZE;

// Upstream runs at 48 kHz (its tuning uses the corrected 47872.34 Hz); the
// OP-1 build at 44.1 kHz.
constexpr double kSampleRateD = PLAITS_SAMPLE_RATE == 48000 ? 48000.0 : 44100.0;

const int32_t kQ15One = 32768;
const int kAudioBits = 26;
const int32_t kAudioOne = 1 << kAudioBits;

inline float AudioToFloat(int32_t x) { return static_cast<float>(x) * (1.0f / 67108864.0f); }

// Compile-time conversions from double constants (never call these with a
// run-time argument in Blackfin code: they'd pull in soft-float).
constexpr int32_t Q(double v, int bits) {
  return static_cast<int32_t>(v * static_cast<double>(1LL << bits) + (v >= 0 ? 0.5 : -0.5));
}
constexpr int32_t Q31(double v) {
  return v >= 1.0 ? INT32_MAX : (v <= -1.0 ? INT32_MIN : Q(v, 31));
}
constexpr uint32_t Q32(double v) {
  return static_cast<uint32_t>(v * 4294967296.0 + 0.5);
}

// Q15 x Q15 -> Q15 (both in [0, 1]: no overflow).
inline int32_t MulQ15(int32_t a, int32_t b) { return (a * b) >> 15; }

// Audio-rate Q31 multiply: the 4-instruction form without the low x low
// partial product (error < 2 LSB; MulQ31, exact, takes 7).
inline int32_t Mul(int32_t a, int32_t b) { return MulQ31Fast(a, b); }

// (a * b) >> 31 rounded to nearest (unbiased): for recursive filters, where
// the truncation bias of Mul accumulates (a TPT state-variable filter at
// low cutoff integrates it: ~40 dB of SNR at f = 0.0005 with Mul).
//   A1 = a.L * b.L (FU); A1 >>= 16; A0 = a.H * b.H, A1 += a.H * b.L (M);
//   A1 += b.H * a.L (M); A1 += 0x4000 (as 0x4000 * 1, FU); A1 >>>= 15;
//   r = (A0 += A1)
inline int32_t MulRoundReference(int32_t a, int32_t b) {
  const int64_t r = (static_cast<int64_t>(a) * b + (1LL << 30)) >> 31;
  return r > INT32_MAX ? INT32_MAX : static_cast<int32_t>(r);
}

inline int32_t MulRound(int32_t a, int32_t b) {
#ifdef __bfin__
  int32_t r;
  const int32_t k = 0x00014000;   // .H = 1, .L = 0x4000
  __asm__("A1 = %1.L * %2.L (FU);\n\t"
          "A1 = A1 >> 16;\n\t"
          "A1 += %1.H * %2.L (M), A0 = %1.H * %2.H;\n\t"
          "A1 += %2.H * %1.L (M);\n\t"
          "A1 += %3.H * %3.L (IS);\n\t"
          "A1 = A1 >>> 15;\n\t"
          "%0 = (A0 += A1);"
          : "=d"(r) : "d"(a), "d"(b), "d"(k) : "A0", "A1");
  return r;
#else
  return MulRoundReference(a, b);
#endif
}

// a (Q31) x c (Q15, int16) -> Q31 with a's top 16 bits only: one 16 x 16
// multiply (~-90 dB of error relative to a full-scale a).
inline int32_t Mul16(int32_t a, int16_t c) {
  return (static_cast<int32_t>(static_cast<int16_t>(a >> 16)) * c) << 1;
}

// (a * b) >> shift in 64 bits: control-rate code only (libgcc's __muldi3 on
// Blackfin).
inline int32_t Mul64(int32_t a, int32_t b, int shift) {
  return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> shift);
}

// The same, saturated to int32 (a Q15 1.0 is 32768: products of 1.0s can
// reach 2^31).
inline int32_t Mul64Sat(int32_t a, int32_t b, int shift) {
  const int64_t r = (static_cast<int64_t>(a) * b) >> shift;
  return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r));
}

// Saturating conversions.
inline int32_t Clip(int32_t x, int32_t lo, int32_t hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

inline int32_t Abs(int32_t x) { return x < 0 ? -x : x; }

// Count of leading zeros of x > 0. Blackfin has no CLZ (GCC calls libgcc's
// __clzsi2, ~20 cycles); SIGNBITS (__builtin_clrsb) is one instruction:
// for 0 < x < 2^31, clz(x) = clrsb(x) + 1.
inline int CountLeadingZeros(uint32_t x) {
  return (x & 0x80000000u) ? 0 : __builtin_clrsb(static_cast<int32_t>(x)) + 1;
}

// --- Reciprocals ------------------------------------------------------------

// 1 / x for d = x * 2^31 with x in [0.5, 1) (d in [2^30, 2^31)): Q30 result
// in (1, 2]. A 64-entry seed and two Newton steps: ~29 bits.
inline int32_t ReciprocalNormalized(int32_t d) {
  if (d <= (1 << 30)) return INT32_MAX;   // x = 0.5: 2.0 saturates (1 LSB off)
  int32_t r = lut_reciprocal_seed_q30[(d >> 24) & 63];
  for (int i = 0; i < 2; ++i) {
    // e = 1 - d r (Q31, small); r += r e.
    const int32_t e = static_cast<int32_t>((1u << 31) - (static_cast<uint32_t>(Mul(d, r)) << 1));
    r += Mul(r, e);
  }
  return r;
}

// num / den in Q31 for 0 <= num < den (den > 0): used for the polyBLEP
// sub-sample times (upstream's `phase / frequency`). ~30 bits.
inline int32_t Fraction(uint32_t num, uint32_t den) {
  const int n = CountLeadingZeros(den);
  const uint32_t d = den << n;          // [2^31, 2^32)
  const uint32_t a = num << n;          // < d
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>(d >> 1));   // Q30 of 2^32 / d
  // a / d = (a / 2^32) (2^32 / d): (a >> 1) is Q31 of a / 2^32, times the
  // Q30 r gives Q30 of a / d; << 1 -> Q31.
  int32_t t = Mul(static_cast<int32_t>(a >> 1), r);
  t = t >= (1 << 30) ? (1 << 30) - 1 : t;
  return t << 1;
}

// FFloat from an unsigned Q32 (e.g. a frequency).
inline FFloat FFloatFromQ32(uint32_t x) {
  return FFloat::FromParts(static_cast<int32_t>(x >> 1), -31);
}

// --- Pitch ------------------------------------------------------------------

// stmlib::SemitonesToRatio as an FFloat; semitones in Q16, |s| < 128.
inline FFloat SemitonesToRatio(int32_t semitones_q16) {
  const int32_t pitch = semitones_q16 + (128 << 16);
  const int32_t i = pitch >> 16;
  const int32_t j = (pitch >> 8) & 255;
  const FFloat high = FFloat::FromParts(lut_pitch_ratio_high_m[i], lut_pitch_ratio_high_e[i]);
  const FFloat low = FFloat::FromParts((1 << 30) + (lut_pitch_ratio_low_q31[j] >> 1), -30);
  return high * low;
}

// The same as a Q16 ratio (for ratios below 2^15) or a Q31 ratio (below 1).
inline int32_t SemitonesToRatioQ(int32_t semitones_q16, int bits) {
  return SemitonesToRatio(semitones_q16).ToQ(bits);
}

// Upstream's NoteToFrequency: a0 / 4 * SemitonesToRatio(note - 9), note
// clamped to [-119, 136] (i.e. note - 9 to [-128, 127]). Q32 increment.
inline uint32_t NoteToFrequency(int32_t note_q16) {
  int32_t pitch = note_q16 - (9 << 16);
  pitch = Clip(pitch, -(128 << 16), 127 << 16) + (128 << 16);
  const int32_t i = pitch >> 16;
  const int32_t j = (pitch >> 8) & 255;
#if PLAITS_SAMPLE_RATE == 48000
  const int32_t high = static_cast<int32_t>(lut_note_frequency_48k_q32[i]);
#else
  const int32_t high = static_cast<int32_t>(lut_note_frequency_44k_q32[i]);
#endif
  return static_cast<uint32_t>(high + MulQ31(high, lut_pitch_ratio_low_q31[j]));
}

// --- Sine -------------------------------------------------------------------

// Upstream's Sine(phase) (lut_sine, 512 points, linear interpolation), phase
// in Q32. Q31 result.
// The interpolation is one 16 x 16 multiply: (b - a) < 2^25 in Q31, so
// (b - a) >> 10 fits 16 bits, times a Q15 fraction (error < 4e-7).
inline int32_t Sine(uint32_t phase) {
  const uint32_t i = phase >> 23;
  const int32_t a = lut_sine_q31[i];
  const int32_t b = lut_sine_q31[i + 1];
  const int16_t d = static_cast<int16_t>((b - a) >> 10);
  const int16_t f = static_cast<int16_t>((phase >> 8) & 0x7fff);
  return a + ((static_cast<int32_t>(d) * f) >> 5);
}

// --- PolyBLEP (Q31 in and out) ----------------------------------------------

inline int32_t ThisBlepSample(int32_t t) { return Mul(t, t) >> 1; }
inline int32_t NextBlepSample(int32_t t) {
  const int32_t u = INT32_MAX - t;   // 1 - t
  return -(Mul(u, u) >> 1);
}
inline int32_t NextIntegratedBlepSample(int32_t t) {
  const int32_t t1 = t >> 1;
  const int32_t t2 = Mul(t1, t1);
  const int32_t t4 = Mul(t2, t2);
  return Q31(0.1875) - t1 + t2 + (t2 >> 1) - t4;
}
inline int32_t ThisIntegratedBlepSample(int32_t t) {
  return NextIntegratedBlepSample(INT32_MAX - t);
}

// --- Parameter interpolation -------------------------------------------------

// (delta / size), for the block sizes used (12 in the comparable build, 16
// on the OP-1); truncates towards -infinity like an arithmetic shift.
// round(2^32 / n) for n in 1..64 (0 unused), a link-time table.
#define OP1PLAITS_INV(n) static_cast<uint32_t>(((1ULL << 33) / (n) + 1) >> 1)
#define OP1PLAITS_INV8(n) OP1PLAITS_INV(n), OP1PLAITS_INV(n + 1), OP1PLAITS_INV(n + 2), \
  OP1PLAITS_INV(n + 3), OP1PLAITS_INV(n + 4), OP1PLAITS_INV(n + 5), OP1PLAITS_INV(n + 6), \
  OP1PLAITS_INV(n + 7)
static const uint32_t kInverseSize[65] = {
  0, 0xffffffffu, OP1PLAITS_INV(2), OP1PLAITS_INV(3), OP1PLAITS_INV(4), OP1PLAITS_INV(5),
  OP1PLAITS_INV(6), OP1PLAITS_INV(7), OP1PLAITS_INV8(8), OP1PLAITS_INV8(16),
  OP1PLAITS_INV8(24), OP1PLAITS_INV8(32), OP1PLAITS_INV8(40), OP1PLAITS_INV8(48),
  OP1PLAITS_INV8(56), OP1PLAITS_INV(64),
};
#undef OP1PLAITS_INV8
#undef OP1PLAITS_INV

// Rounded to nearest: unbiased, so a sweep's frequency doesn't lag (a
// truncated increment cost ~15 dB of SNR on frequency sweeps). A ramp can
// overshoot its target by up to size / 2 LSB within the block: values used
// as table indices are clamped where they're used.
inline int32_t DivideBySize(int32_t delta, size_t size) {
  switch (size) {
    case 16: return (delta >> 4) + ((delta >> 3) & 1);   // no overflow near INT32_MAX
    case 32: return (delta >> 5) + ((delta >> 4) & 1);
    case 64: return (delta >> 6) + ((delta >> 5) & 1);
    case 8: return (delta >> 3) + ((delta >> 2) & 1);
    default: {
      // delta / size rounded, via a rounded 2^32 / size (control rate;
      // sizes up to 64, no division).
      const int64_t inv = size <= 64 ? kInverseSize[size] : 0;
      return static_cast<int32_t>((static_cast<int64_t>(delta) * inv + (1LL << 31)) >> 32);
    }
  }
}

// stmlib::ParameterInterpolator: linear ramp from *state to the new value
// over `size` samples. Upstream writes back the value reached (in float,
// the target to within rounding); here the target itself is written back:
// the rounded increment would otherwise leave the state up to size / 2 LSB
// off the target for good (a constant 2e-6 pitch error on frequencies).
class Interpolator {
 public:
  Interpolator(int32_t* state, int32_t new_value, size_t size) {
    state_ = state;
    value_ = *state;
    target_ = new_value;
    increment_ = DivideBySize(new_value - *state, size);
  }
  ~Interpolator() { *state_ = target_; }
  inline int32_t Next() { value_ += increment_; return value_; }
  // value + increment * t, t in Q31 [0, 1].
  inline int32_t subsample(int32_t t) const { return value_ + MulQ31(increment_, t); }
  inline int32_t value() const { return value_; }
  inline int32_t increment() const { return increment_; }

 private:
  int32_t* state_;
  int32_t value_;
  int32_t target_;
  int32_t increment_;
};

// The same, writing back the value reached, exactly as upstream: for loops
// that don't call Next() on every sample (the synthetic bass drum's f0
// interpolator), where the reached value differs from the target.
class InterpolatorReached {
 public:
  InterpolatorReached(int32_t* state, int32_t new_value, size_t size) {
    state_ = state;
    value_ = *state;
    increment_ = DivideBySize(new_value - *state, size);
  }
  ~InterpolatorReached() { *state_ = value_; }
  inline int32_t Next() { value_ += increment_; return value_; }

 private:
  int32_t* state_;
  int32_t value_;
  int32_t increment_;
};

// x / (1 + |x|) for x in Q24 (|x| < 128): Q31 (|result| < 1). The soft
// clipping shape of the drums' diode and transistor models.
inline int32_t SoftRatio(int32_t x_q24) {
  const uint32_t a = static_cast<uint32_t>(x_q24 < 0 ? -x_q24 : x_q24);
  const int32_t r = Fraction(a, a + (1u << 24));
  return x_q24 < 0 ? -r : r;
}

// stmlib::SoftClip: x < -3 -> -1, x > 3 -> 1, else x (27 + x^2) / (27 + 9 x^2).
// x in Q26 (|x| < 32), result Q31.
inline int32_t SoftClip(int32_t x_q26) {
  if (x_q26 >= (3 << 26)) return INT32_MAX;
  if (x_q26 <= -(3 << 26)) return -INT32_MAX;
  const int32_t x28 = (x_q26 < 0 ? -x_q26 : x_q26) << 2;   // |x|, Q28 (< 3)
  const int32_t x2 = Mul(x28, x28);                          // x^2, Q25 (< 9)
  const uint32_t num = static_cast<uint32_t>(Mul(x28, (27 << 25) + x2));   // Q22 (< 108)
  const uint32_t den = static_cast<uint32_t>((27 << 22) + 9 * (x2 >> 3));  // Q22
  const int32_t r = num < den ? Fraction(num, den) : INT32_MAX;
  return x_q26 < 0 ? -r : r;
}

// --- Random -----------------------------------------------------------------

// stmlib::Random: the same LCG, so noise sequences match upstream's.
class Random {
 public:
  static inline void Seed(uint32_t seed) { state_ = seed; }
  static inline uint32_t state() { return state_; }
  static inline uint32_t GetWord() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
  }
  // GetFloat() in Q32 (the word itself) or Q31.
  static inline int32_t GetQ31() { return static_cast<int32_t>(GetWord() >> 1); }
 private:
  static uint32_t state_;
};

// --- Engine interface ---------------------------------------------------------

// Upstream's TriggerState.
enum TriggerState {
  TRIGGER_LOW = 0,
  TRIGGER_RISING_EDGE = 1,
  TRIGGER_UNPATCHED = 2,
  TRIGGER_HIGH = 4,
};

struct FixedParameters {
  int trigger;
  int32_t note;        // Q16 semitones
  int32_t timbre;      // Q15
  int32_t morph;       // Q15
  int32_t harmonics;   // Q15
  int32_t accent;      // Q15
};

// A minimal bump allocator for engines' buffers (stmlib::BufferAllocator).
class Allocator {
 public:
  void Init(void* buffer, size_t size) {
    next_ = static_cast<uint8_t*>(buffer);
    free_ = size;
  }
  template<typename T>
  T* Allocate(size_t n) {
    size_t bytes = (sizeof(T) * n + 3) & ~static_cast<size_t>(3);
    if (bytes > free_) return nullptr;
    T* p = reinterpret_cast<T*>(next_);
    next_ += bytes;
    free_ -= bytes;
    return p;
  }
  size_t free() const { return free_; }
 private:
  uint8_t* next_;
  size_t free_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_FIXED_MATH_H_
