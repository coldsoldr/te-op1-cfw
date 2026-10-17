// Plaits' polyBLEP Oscillator (plaits/dsp/oscillator/oscillator.h) in fixed
// point, for the shapes the engines use: saw, square, triangle and slope
// (no external FM, no through-zero FM).
//
// Phase and pulse width are Q32; frequencies Q32 increments; the BLEP
// accumulators (this/next sample) Q30. Output: upstream's 2 x - 1, Q30.
//
// Differences from upstream:
//   - the slope shape computes phase / pw (or (phase - pw) / (1 - pw)) with
//     Fraction() instead of multiplying by 1 / pw: the same value, exact to
//     ~30 bits, one division per sample (upstream divides twice per sample
//     too, for slope_up and slope_down);
//   - BLEP sub-sample times are clamped to [0, 1) (upstream's t can stray
//     slightly when the pulse width moves past the phase).

#ifndef OP1PLAITS_FIXED_OSCILLATOR_H_
#define OP1PLAITS_FIXED_OSCILLATOR_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

enum OscillatorShape {
  OSCILLATOR_SHAPE_IMPULSE_TRAIN,
  OSCILLATOR_SHAPE_SAW,
  OSCILLATOR_SHAPE_TRIANGLE,
  OSCILLATOR_SHAPE_SLOPE,
  OSCILLATOR_SHAPE_SQUARE,
};

// kMaxFrequency = 0.25, kMinFrequency = 1e-6 (Q32).
const uint32_t kMaxFrequencyQ32 = 1u << 30;
const uint32_t kMinFrequencyQ32 = 4295;

// t = num / den clamped to [0, 1) in Q31 (num may be slightly out of range).
inline int32_t BlepTime(int32_t num, uint32_t den) {
  if (num <= 0) return 0;
  if (static_cast<uint32_t>(num) >= den) return INT32_MAX;
  return Fraction(static_cast<uint32_t>(num), den);
}

class OscillatorFixed {
 public:
  void Init() {
    phase_ = 1u << 31;
    next_sample_ = 0;
    lp_state_ = 1 << 30;
    hp_state_ = 0;
    high_ = true;
    frequency_ = static_cast<int32_t>(Q32(0.001));
    pw_ = 0;   // 0.5, top bit flipped
  }

  // frequency: Q32; pw: Q32 (pulse width, slope shapes); out: Q30
  // (Q29 for OSCILLATOR_SHAPE_IMPULSE_TRAIN).
  template<OscillatorShape shape>
  void Render(uint32_t frequency, uint32_t pw, int32_t* out, size_t size) {
    if (frequency < kMinFrequencyQ32) frequency = kMinFrequencyQ32;
    if (frequency > kMaxFrequencyQ32) frequency = kMaxFrequencyQ32;
    // CONSTRAIN(pw, 2 f, 1 - 2 f).
    const uint32_t pw_min = frequency << 1;
    const uint32_t pw_max = 0u - pw_min;   // 1 - 2 f
    if (pw < pw_min) pw = pw_min;
    if (pw > pw_max) pw = pw_max;

    Interpolator fm(&frequency_, static_cast<int32_t>(frequency), size);
    Interpolator pwm(&pw_, static_cast<int32_t>(pw ^ 0x80000000u), size);   // offset: signed

    int32_t next_sample = next_sample_;
    uint32_t phase = phase_;
    bool high = high_;

    while (size--) {
      int32_t this_sample = next_sample;
      next_sample = 0;
      const uint32_t f = static_cast<uint32_t>(fm.Next());
      const uint32_t pw_now = (shape == OSCILLATOR_SHAPE_TRIANGLE)
          ? (1u << 31) : (static_cast<uint32_t>(pwm.Next()) ^ 0x80000000u);
      const uint32_t old_phase = phase;
      phase += f;
      const bool wrapped = phase < old_phase;

      if (shape == OSCILLATOR_SHAPE_SAW || shape == OSCILLATOR_SHAPE_IMPULSE_TRAIN) {
        if (wrapped) {
          const int32_t t = BlepTime(static_cast<int32_t>(phase), f);
          this_sample -= ThisBlepSample(t) >> 1;
          next_sample -= NextBlepSample(t) >> 1;
        }
        next_sample += static_cast<int32_t>(phase >> 2);   // Q30
        if (shape == OSCILLATOR_SHAPE_SAW) {
          *out++ = (this_sample - (1 << 29)) << 1;
        } else {
          // lp += 0.25 ((hp - this) - lp); out = 4 lp; hp = this. Out in
          // Q29 for this shape: 4 lp starts at 3 (lp_state_ = 1 at Init).
          lp_state_ += ((hp_state_ - this_sample) - lp_state_) >> 2;
          *out++ = ShlSat(lp_state_, 1);
          hp_state_ = this_sample;
        }
      } else if (shape == OSCILLATOR_SHAPE_TRIANGLE || shape == OSCILLATOR_SHAPE_SLOPE) {
        // (slope_up + slope_down) f = f / pw + f / (1 - pw), Q31 (< 1).
        if (wrapped) {
          // The phase went past 1, and past pw on the way if it was still
          // below it (high: phase < pw).
          if (high) {
            // Upstream handles the pw crossing first, with the unwrapped
            // phase: (phase + 1 - pw) / f.
            const int32_t t = BlepTime(static_cast<int32_t>(phase - pw_now), f);
            const int32_t d = Discontinuity<shape>(f, pw_now);
            this_sample -= Mul(ThisIntegratedBlepSample(t), d) >> 1;
            next_sample -= Mul(NextIntegratedBlepSample(t), d) >> 1;
          }
          const int32_t t = BlepTime(static_cast<int32_t>(phase), f);
          const int32_t d = Discontinuity<shape>(f, pw_now);
          this_sample += Mul(ThisIntegratedBlepSample(t), d) >> 1;
          next_sample += Mul(NextIntegratedBlepSample(t), d) >> 1;
          high = true;
        } else if (high ^ (phase < pw_now)) {
          const int32_t t = BlepTime(static_cast<int32_t>(phase - pw_now), f);
          const int32_t d = Discontinuity<shape>(f, pw_now);
          this_sample -= Mul(ThisIntegratedBlepSample(t), d) >> 1;
          next_sample -= Mul(NextIntegratedBlepSample(t), d) >> 1;
          high = phase < pw_now;
        }
        if (shape == OSCILLATOR_SHAPE_TRIANGLE) {
          // 2 phase or 1 - 2 (phase - 0.5), Q30.
          next_sample += high
              ? static_cast<int32_t>(phase >> 1)
              : (1 << 30) - static_cast<int32_t>((phase - (1u << 31)) >> 1);
        } else {
          next_sample += high
              ? (Fraction(phase, pw_now) >> 1)
              : (1 << 30) - (Fraction(phase - pw_now, 0u - pw_now) >> 1);
        }
        *out++ = (this_sample - (1 << 29)) << 1;
      } else {   // square
        if (wrapped) {
          if (!high) {
            const int32_t t = BlepTime(static_cast<int32_t>(phase - pw_now), f);
            this_sample += ThisBlepSample(t) >> 1;
            next_sample += NextBlepSample(t) >> 1;
          }
          const int32_t t = BlepTime(static_cast<int32_t>(phase), f);
          this_sample -= ThisBlepSample(t) >> 1;
          next_sample -= NextBlepSample(t) >> 1;
          high = false;
        } else if (high ^ (phase >= pw_now)) {
          const int32_t t = BlepTime(static_cast<int32_t>(phase - pw_now), f);
          this_sample += ThisBlepSample(t) >> 1;
          next_sample += NextBlepSample(t) >> 1;
          high = phase >= pw_now;
        }
        next_sample += phase < pw_now ? 0 : (1 << 30);
        *out++ = (this_sample - (1 << 29)) << 1;
      }
    }
    next_sample_ = next_sample;
    phase_ = phase;
    high_ = high;
  }

 private:
  template<OscillatorShape shape>
  static inline int32_t Discontinuity(uint32_t f, uint32_t pw) {
    if (shape == OSCILLATOR_SHAPE_TRIANGLE) {
      return f >= (1u << 30) ? INT32_MAX : static_cast<int32_t>(f << 1);   // 4 f, Q31
    }
    // pw is kept within [2 f, 1 - 2 f], so each term is <= 0.5.
    const uint32_t up = 0u - pw;
    return (f < pw ? Fraction(f, pw) : (1 << 30)) + (f < up ? Fraction(f, up) : (1 << 30));
  }

  uint32_t phase_;
  int32_t next_sample_;
  int32_t lp_state_;
  int32_t hp_state_;
  bool high_;

  int32_t frequency_;
  int32_t pw_;   // Q32 pulse width, stored with its top bit flipped (signed)
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_OSCILLATOR_H_
