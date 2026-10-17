// stmlib's filters in fixed point: the TPT state-variable filter (Svf) and
// the one-pole (OnePole), with the tan() approximations upstream uses.
//
// Coefficients: g = tan(pi f) is kept as a Q31 mantissa with a shift (g =
// g_ 2^g_shift_, g_shift_ in 0..3, so g < 8, f < ~0.46): low cutoffs keep
// their full precision. r = 1 / resonance in Q29 (r <= 4), h = 1 / (1 + r g +
// g^2) in Q31. States and signals: any common int32 format chosen by the
// caller (the arithmetic only multiplies them by coefficients).

#ifndef OP1PLAITS_FIXED_SVF_H_
#define OP1PLAITS_FIXED_SVF_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

enum FrequencyApproximation {
  FREQUENCY_ACCURATE,
  FREQUENCY_FAST,
  FREQUENCY_DIRTY,
};

enum FilterMode {
  FILTER_MODE_LOW_PASS,
  FILTER_MODE_BAND_PASS,
  FILTER_MODE_HIGH_PASS,
};

// A coefficient as mantissa (Q31, < 1 unless shift is 0 and it saturates)
// and left shift.
struct ScaledCoefficient {
  int32_t value;
  int32_t shift;
};

// OnePole::tan<approximation>(f) for f in Q32 (f < 0.5). Returns tan(pi f)
// as Q27 (< 16).
template<FrequencyApproximation approximation>
inline int32_t TanQ27(uint32_t f) {
  const int32_t x = static_cast<int32_t>(f >> 1);   // f, Q31
  const int32_t u = MulQ31(x, x);                    // f^2, Q31 (< 0.25)
  int32_t t;                                         // the polynomial in f^2, Q20
  if (approximation == FREQUENCY_DIRTY) {
    t = Q(3.736e-01 * 31.00627668029982, 20);
    t = Q(3.14159265358979, 20) + MulQ31(u, t);
  } else if (approximation == FREQUENCY_FAST) {
    t = Q(1.823e-01 * 306.0196847852814, 20);
    t = Q(3.260e-01 * 31.00627668029982, 20) + MulQ31(u, t);
    t = Q(3.14159265358979, 20) + MulQ31(u, t);
  } else {
    // The first coefficient (~2800) needs Q18; from the second step on the
    // partial sum is below 800.
    t = Q(9.5168091e-03 * 294204.0179738905, 18);
    t = Q(2.900525e-03 * 29809.09933344621, 18) + MulQ31(u, t);
    t <<= 2;
    t = Q(5.33740603e-02 * 3020.293227776792, 20) + MulQ31(u, t);
    t = Q(1.333923995e-01 * 306.0196847852814, 20) + MulQ31(u, t);
    t = Q(3.333314036e-01 * 31.00627668029982, 20) + MulQ31(u, t);
    t = Q(3.14159265358979, 20) + MulQ31(u, t);
  }
  // g = f t: f as Q32 (f_q32 < 2^31) times t Q20 -> Q21; << 6 -> Q27.
  int64_t g = (static_cast<int64_t>(static_cast<int32_t>(f)) * t) >> 25;   // Q27
  return g > INT32_MAX ? INT32_MAX : static_cast<int32_t>(g);
}

// Per-sample versions (no 64-bit arithmetic), for filters whose cutoff
// moves every sample (the analog bass drum's resonator).
// OnePole::tan<FREQUENCY_DIRTY>, f Q32 (< 0.5), Q27 result.
inline int32_t TanDirtyFast(uint32_t f) {
  const int32_t x = static_cast<int32_t>(f >> 1);                      // f, Q31
  const int32_t u = Mul(x, x);                                         // f^2, Q31
  const int32_t t = Q(3.14159265358979, 27) + Mul(u, Q(3.736e-01 * 31.00627668029982, 27));
  return Mul(static_cast<int32_t>(f), t) >> 1;                         // Q28 -> Q27
}

// 1 / (1 + r g + g^2), Q31; g Q27 (< 4.7: f < 0.5), r Q29 (< 4).
inline int32_t SvfHFast(int32_t g_q27, int32_t r_q29) {
  const int32_t d = (1 << 26) + (Mul(r_q29, g_q27) << 1) + (Mul(g_q27, g_q27) << 3);   // Q26 (< 32)
  const int n = CountLeadingZeros(static_cast<uint32_t>(d));   // 1..5
  // d << (n - 1) in [2^30, 2^31): d = x 2^(6 - n); 1 / d = (1 / x) 2^(n - 6).
  const int32_t r = ReciprocalNormalized(d << (n - 1));
  return r >> (5 - n);
}

// 1 / res, Q29, for res in Q16 (>= 1).
inline int32_t ReciprocalQ16ToQ29(uint32_t res_q16) {
  const int n = CountLeadingZeros(res_q16);   // res >= 2^16: n <= 15
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>((res_q16 << n) >> 1));
  return r >> (17 - n);
}

// Q27 g -> mantissa + shift.
inline ScaledCoefficient ScaleG(int32_t g_q27, int32_t shift) {
  ScaledCoefficient c;
  c.shift = shift;
  int64_t v = static_cast<int64_t>(g_q27) << (4 - shift);   // Q31 / 2^shift
  c.value = v > INT32_MAX ? INT32_MAX : static_cast<int32_t>(v);
  return c;
}

inline int32_t ShiftFor(int32_t g_q27) {
  if (g_q27 < (1 << 27)) return 0;
  if (g_q27 < (2 << 27)) return 1;
  if (g_q27 < (4 << 27)) return 2;
  return 3;
}

// x * c, c a scaled coefficient.
inline int32_t MulScaled(int32_t x, int32_t value, int32_t shift) {
  return MulRound(x, value) << shift;
}

// 1 / (1 + r g + g^2), Q31; g Q27, r Q29.
inline int32_t SvfH(int32_t g_q27, int32_t r_q29) {
  // d = 1 + r g + g^2 in Q24 (< 128).
  const int64_t g = g_q27;
  int64_t d = (1LL << 24) + ((static_cast<int64_t>(r_q29) * g) >> 32) + ((g * g) >> 30);
  // 1 / d: normalise d into [2^30, 2^31).
  if (d < (1LL << 24)) d = 1LL << 24;
  int n = 0;
  while (d < (1LL << 30)) { d <<= 1; ++n; }
  // d_norm / 2^31 = x in [0.5, 1); d = x 2^(31 - n) in Q24 units, i.e.
  // value = x 2^(7 - n); 1 / value = (1 / x) 2^(n - 7). r is Q30 of 1 / x:
  // Q31 of 1 / value = r 2^(n - 7 + 1).
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>(d));
  const int s = n - 6;
  if (s >= 1) return INT32_MAX;
  return r >> (-s);
}

// 1 / resonance in Q29 from a Q16 resonance (>= 0.25).
inline int32_t ResonanceToR(int32_t resonance_q16) {
  if (resonance_q16 < (1 << 14)) resonance_q16 = 1 << 14;
  const int n = CountLeadingZeros(static_cast<uint32_t>(resonance_q16));
  const uint32_t d = static_cast<uint32_t>(resonance_q16) << n;   // [2^31, 2^32)
  // resonance = (d / 2^32) 2^(32 - n - 16); 1 / resonance = (1 / x)
  // 2^(n - 16); Q29: r_q30 2^(n - 16 - 1).
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>(d >> 1));
  const int s = n - 17;
  if (s >= 0) return s >= 2 ? INT32_MAX : r << s;
  return r >> (-s);
}

class SvfFixed {
 public:
  void Init() {
    g_ = 0;
    g_shift_ = 0;
    r_ = 1 << 29;
    h_ = INT32_MAX;
    Reset();
  }
  void Reset() { state_1_ = state_2_ = 0; }

  // f: Q32; resonance: Q16.
  template<FrequencyApproximation approximation>
  void set_f_q(uint32_t f, int32_t resonance_q16) {
    const int32_t g = TanQ27<approximation>(f);
    set_g_r(g, ResonanceToR(resonance_q16));
  }

  // g: Q27; r: Q29.
  void set_g_r(int32_t g_q27, int32_t r_q29) {
    g_shift_ = ShiftFor(g_q27);
    const ScaledCoefficient c = ScaleG(g_q27, g_shift_);
    g_ = c.value;
    r_ = r_q29;
    h_ = SvfH(g_q27, r_q29);
  }

  void set(int32_t g, int32_t g_shift, int32_t r, int32_t h) {
    g_ = g;
    g_shift_ = g_shift;
    r_ = r;
    h_ = h;
  }

  int32_t g() const { return g_; }
  int32_t g_shift() const { return g_shift_; }
  int32_t r() const { return r_; }
  int32_t h() const { return h_; }

  // One sample; returns the requested outputs.
  inline void Tick(int32_t in, int32_t* lp, int32_t* bp, int32_t* hp) {
    Tick(in, g_, g_shift_, r_, h_, lp, bp, hp);
  }

  // With explicit coefficients (interpolated by the caller).
  inline void Tick(int32_t in, int32_t g, int32_t g_shift, int32_t r, int32_t h,
                   int32_t* lp, int32_t* bp, int32_t* hp) {
    const int32_t s1 = state_1_;
    const int32_t s2 = state_2_;
    const int32_t x = in - (MulRound(r, s1) << 2) - MulScaled(s1, g, g_shift) - s2;
    const int32_t h_out = MulRound(x, h);
    const int32_t v1 = MulScaled(h_out, g, g_shift);
    const int32_t b = v1 + s1;
    state_1_ = b + v1;
    const int32_t v2 = MulScaled(b, g, g_shift);
    const int32_t l = v2 + s2;
    state_2_ = l + v2;
    *lp = l;
    *bp = b;
    *hp = h_out;
  }

  // The input's contribution to one Tick is linear: an input x adds g h x
  // to bp, 2 g h x to state 1 and 2 g^2 h x to state 2 (and g^2 h x to lp).
  // Inject() adds it separately, so a large impulse (the particle engine's,
  // up to ~1600) can be applied to states kept in a finer format.
  // gh_x = g h x, g2h_x = g^2 h x, in the states' format.
  inline void Inject(int32_t gh_x, int32_t g2h_x) {
    state_1_ += gh_x << 1;
    state_2_ += g2h_x << 1;
  }
  // g h and g^2 h (Q31) for the current coefficients.
  void InjectionGains(int32_t* gh, int32_t* g2h) const {
    const int64_t g = static_cast<int64_t>(g_) << g_shift_;   // Q31
    const int64_t a = (g * h_) >> 31;                          // g h, Q31
    *gh = a > INT32_MAX ? INT32_MAX : static_cast<int32_t>(a);
    const int64_t b = (g * a) >> 31;
    *g2h = b > INT32_MAX ? INT32_MAX : static_cast<int32_t>(b);
  }

  template<FilterMode mode>
  inline int32_t Process(int32_t in) {
    int32_t lp, bp, hp;
    Tick(in, &lp, &bp, &hp);
    return mode == FILTER_MODE_LOW_PASS ? lp : (mode == FILTER_MODE_BAND_PASS ? bp : hp);
  }

  template<FilterMode mode>
  void Process(const int32_t* in, int32_t* out, size_t size) {
    while (size--) *out++ = Process<mode>(*in++);
  }

 private:
  int32_t g_;
  int32_t g_shift_;
  int32_t r_;
  int32_t h_;
  int32_t state_1_;
  int32_t state_2_;
};

// stmlib::OnePole (TPT one-pole): lp = (g in + s) gi; s = g (in - lp) + lp,
// with gi = 1 / (1 + g). Stored as G = g / (1 + g) (Q31, < 1): lp = s + G (in
// - s); s = 2 lp - s. The same filter, one multiply.
class OnePoleFixed {
 public:
  void Init() { G_ = Q31(0.0304); state_ = 0; }   // set_f<DIRTY>(0.01)
  void Reset() { state_ = 0; }

  template<FrequencyApproximation approximation>
  void set_f(uint32_t f) {
    const int32_t g = TanQ27<approximation>(f);   // Q27
    // G = g / (1 + g).
    const int64_t d = (1LL << 27) + g;            // Q27, in [1, 17)
    int n = 0;
    int64_t dn = d;
    while (dn < (1LL << 30)) { dn <<= 1; ++n; }
    while (dn >= (1LL << 31)) { dn >>= 1; --n; }
    // d = x 2^(31 - n - 27) -> 1 / d = (1 / x) 2^(n - 4); r Q30 of 1 / x.
    const int32_t r = ReciprocalNormalized(static_cast<int32_t>(dn));
    // G = g (1 / d): g Q27 x r Q30 -> Q26 (Mul), scaled by 2^(n - 4) -> Q31
    // needs << (5 + n - 4).
    const int s = n + 1;
    int64_t G = s >= 0 ? static_cast<int64_t>(Mul(g, r)) << s : Mul(g, r) >> (-s);
    G_ = G >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(G);
  }

  template<FilterMode mode>
  inline int32_t Process(int32_t in) {
    const int32_t lp = state_ + MulRound(in - state_, G_);
    state_ = (lp << 1) - state_;
    return mode == FILTER_MODE_LOW_PASS ? lp : in - lp;
  }

  template<FilterMode mode>
  void Process(int32_t* in_out, size_t size) {
    while (size--) {
      *in_out = Process<mode>(*in_out);
      ++in_out;
    }
  }

 private:
  int32_t G_;
  int32_t state_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_SVF_H_
