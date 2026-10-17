// 2:1 decimation and interpolation, to run the core at half the OP-1's
// effect rate (op1/clouds_fx.cc, OP1CLOUDS_SAMPLE_RATE 22050), and the
// output mix at the full rate.
//
// The filter is a 19-tap halfband FIR (Blackman-windowed sinc, DC gain 1):
// only the odd taps around the centre (0.5) are non-zero. int16 samples,
// Q15 taps: every product is one 16 x 16 multiply. The mix is upstream's
// output stage (dry x fade + wet x 1.2 x fade, soft limit of half the sum)
// with the soft limit from a Q15 table, at unity gain: upstream's
// SoftConvert soft-limits half the sum (its hardware makes up the 6 dB), so
// the table holds 2 x SoftLimit, clipped at full scale.
//
// Each kernel has a C reference and a Blackfin version (halfband_bfin.S,
// bit-exact, checked by tests/test_kernels.cc).

#ifndef OP1CLOUDS_CORE_HALFBAND_H_
#define OP1CLOUDS_CORE_HALFBAND_H_

#include <cstdint>

#include "src/core/dsp_fixed.h"

namespace op1clouds {

// h[+-1], h[+-3], ... h[+-9] in Q15 (they sum to 0.25 = 8192).
const int16_t kHalfbandTaps[5] = { 10019, -2397, 710, -151, 11 };
const int32_t kHalfbandMaxIn = 64;   // input samples per call

// 2 x upstream's SoftLimit over [0, 4] as int16 output samples, clipped
// (257 points), then the steps between them (256).
struct SoftLimitTable {
  int16_t value[257];
  int16_t step[256];
};
void InitSoftLimitTable(SoftLimitTable* t);

// x: 18 samples of history then 2n new ones; n outputs. Output k is
// centred on x[2k + 10].
void HalfbandDecimateC(const int16_t* x, int16_t* out, int32_t n);
// x: 9 samples of history then n new ones; 2n outputs. Output pair m: the
// even one centred between x[m + 4] and x[m + 5], the odd one x[m + 5].
void HalfbandInterpolateC(const int16_t* x, int16_t* out, int32_t n);
// io = 2 x SoftLimit(((io x fade_out) >> 4) + ((wet x wet_gain) >> 2)),
// half the sum in Q27; fades in Q15 (wet_gain includes the 0.6), wet at
// half scale.
void MixSoftLimitC(int16_t* io, const int16_t* wet, int32_t n, int32_t fade_out, int32_t wet_gain,
                   const SoftLimitTable* t);

extern "C" void HalfbandDecimateAsm(const int16_t* x, int16_t* out, int32_t n);
extern "C" void HalfbandInterpolateAsm(const int16_t* x, int16_t* out, int32_t n);
// gains = fade_out | wet_gain << 16.
extern "C" void MixSoftLimitAsm(int16_t* io, const int16_t* wet, int32_t n, int32_t gains,
                                const SoftLimitTable* t);

inline void HalfbandDecimate(const int16_t* x, int16_t* out, int32_t n) {
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  HalfbandDecimateAsm(x, out, n);
#else
  HalfbandDecimateC(x, out, n);
#endif
}

inline void HalfbandInterpolate(const int16_t* x, int16_t* out, int32_t n) {
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  HalfbandInterpolateAsm(x, out, n);
#else
  HalfbandInterpolateC(x, out, n);
#endif
}

inline void MixSoftLimit(int16_t* io, const int16_t* wet, int32_t n, int32_t fade_out,
                         int32_t wet_gain, const SoftLimitTable* t) {
#if defined(__bfin__) && !defined(OP1CLOUDS_NO_ASM)
  MixSoftLimitAsm(io, wet, n, (fade_out & 0xffff) | (wet_gain << 16), t);
#else
  MixSoftLimitC(io, wet, n, fade_out, wet_gain, t);
#endif
}

class HalfbandDecimator {
 public:
  void Init() {
    for (int32_t i = 0; i < kHistory; ++i) x_[i] = 0;
  }

  // 2n samples in, n samples out (out may be in).
  void Process(const int16_t* in, int16_t* out, int32_t n) {
    for (int32_t i = 0; i < 2 * n; ++i) x_[kHistory + i] = in[i];
    HalfbandDecimate(x_, out, n);
    for (int32_t i = 0; i < kHistory; ++i) x_[i] = x_[2 * n + i];
  }

 private:
  static const int32_t kHistory = 18;
  int16_t x_[kHistory + kHalfbandMaxIn];
};

class HalfbandInterpolator {
 public:
  void Init() {
    for (int32_t i = 0; i < kHistory; ++i) x_[i] = 0;
  }

  // n samples in, 2n samples out.
  void Process(const int16_t* in, int16_t* out, int32_t n) {
    for (int32_t i = 0; i < n; ++i) x_[kHistory + i] = in[i];
    HalfbandInterpolate(x_, out, n);
    for (int32_t i = 0; i < kHistory; ++i) x_[i] = x_[n + i];
  }

 private:
  static const int32_t kHistory = 9;
  int16_t x_[kHistory + kHalfbandMaxIn / 2];
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_HALFBAND_H_
