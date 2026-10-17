// stmlib::Svf (the TPT state-variable filter, as Clouds uses it: the
// feedback high-pass and the texture low-/high-pass) in fixed point.
//
// Coefficients are computed per block with FFloat (upstream's
// FREQUENCY_FAST tan approximation, r = 1/Q, h = 1/(1 + rg + g^2)); per
// sample, upstream's loop:
//   hp = (in - (r + g) s1 - s2) h;  bp = g hp + s1;  s1 = g hp + bp;
//   lp = g bp + s2;                 s2 = g bp + lp.
// Formats: states and signals Q27; g and r + g Q28 (g reaches ~4.6 at the
// low-pass's top cutoff, 0.499); h Q31.

#ifndef OP1CLOUDS_CORE_SVF_FIXED_H_
#define OP1CLOUDS_CORE_SVF_FIXED_H_

#include <cstddef>
#include <cstdint>

#include "src/core/dsp_fixed.h"

namespace op1clouds {

struct SvfCoefficients {
  int32_t g;    // Q28
  int32_t rg;   // r + g, Q28
  int32_t h;    // Q31
};

// f: normalised cutoff (f / fs, < 0.5); resonance: Q.
inline SvfCoefficients MakeSvfCoefficients(FFloat f, FFloat resonance) {
  // stmlib OnePole::tan<FREQUENCY_FAST>.
  static constexpr double kPi = 3.14159265358979323846;
  static constexpr FFloat a = FFloat(3.260e-01 * kPi * kPi * kPi);
  static constexpr FFloat b = FFloat(1.823e-01 * kPi * kPi * kPi * kPi * kPi);
  static constexpr FFloat pi = FFloat(kPi);
  static constexpr FFloat one = FFloat(1.0);
  const FFloat f2 = f * f;
  const FFloat g = f * (pi + f2 * (a + b * f2));
  const FFloat r = FFloat::Reciprocal(resonance);
  const FFloat h = FFloat::Reciprocal(one + r * g + g * g);
  SvfCoefficients c;
  c.g = g.ToQ(28);
  c.rg = (r + g).ToQ(28);
  c.h = h.ToQ(31);
  return c;
}

enum SvfMode { SVF_LOW_PASS, SVF_HIGH_PASS };

class SvfFixed {
 public:
  void Init() { s1_ = s2_ = 0; }

  template<SvfMode mode>
  void Process(const SvfCoefficients& c, int32_t* in_out, size_t size) {
    int32_t s1 = s1_, s2 = s2_;
    const int32_t g = c.g, rg = c.rg, h = c.h;
    while (size--) {
      const int32_t hp = MulQ31Fast(*in_out - MulQ28(rg, s1) - s2, h);
      int32_t t = MulQ28(g, hp);
      const int32_t bp = t + s1;
      s1 = t + bp;
      t = MulQ28(g, bp);
      const int32_t lp = t + s2;
      s2 = t + lp;
      *in_out++ = mode == SVF_LOW_PASS ? lp : hp;
    }
    s1_ = s1;
    s2_ = s2;
  }

 private:
  int32_t s1_, s2_;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_SVF_FIXED_H_
