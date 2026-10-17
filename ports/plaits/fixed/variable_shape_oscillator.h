// Plaits' VariableShapeOscillator (continuously variable triangle / saw /
// square with optional hard sync, polyBLEP) and VariableSawOscillator in
// fixed point.
//
// Phases and pulse widths are unsigned Q31 (range [0, 2): upstream lets a
// phase run past 1 before wrapping it), frequencies Q31; BLEP accumulators
// and naive samples Q30. Output (Q30): upstream's 2 x - 1 (variable shape)
// or (2 x - 1) / 1.2 (variable saw).
//
// phase / pw and (phase - pw) / (1 - pw) (upstream: phase * slope_up, ...)
// are computed with Fraction(); the BLEP times too. Upstream divides twice
// per sample for the slopes; this divides once per naive sample.

#ifndef OP1PLAITS_FIXED_VARIABLE_SHAPE_OSCILLATOR_H_
#define OP1PLAITS_FIXED_VARIABLE_SHAPE_OSCILLATOR_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

const uint32_t kOne31 = 1u << 31;   // 1.0 in unsigned Q31
const uint32_t kMaxFrequencyQ31 = 1u << 29;   // 0.25

// num / den in Q30 for 0 <= num < 2 den (a slope value up to ~1 + a bit).
inline int32_t Ratio30(uint32_t num, uint32_t den) {
  if (num < den) return Fraction(num, den) >> 1;
  num -= den;
  if (num >= den) return INT32_MAX;
  return (1 << 30) + (Fraction(num, den) >> 1);
}

// A BLEP time num / den in Q31, clamped to [0, 1).
inline int32_t BlepTime31(int32_t num, int32_t den) {
  if (num <= 0 || den <= 0) return 0;
  if (num >= den) return INT32_MAX;
  return Fraction(static_cast<uint32_t>(num), static_cast<uint32_t>(den));
}

// (slope_up + slope_down) * f = f / pw + f / (1 - pw), Q31 (pw is kept
// within [2 f, 1 - 2 f], so each term is at most 0.5).
inline int32_t SlopeStep(uint32_t f, uint32_t pw) {
  const uint32_t up = kOne31 - pw;
  return (f < pw ? Fraction(f, pw) : (1 << 30)) + (f < up ? Fraction(f, up) : (1 << 30));
}

class VariableShapeOscillatorFixed {
 public:
  void Init() {
    master_phase_ = 0;
    slave_phase_ = 0;
    next_sample_ = 0;
    previous_pw_ = 1u << 30;
    high_ = false;
    master_frequency_ = 0;
    slave_frequency_ = static_cast<int32_t>(Q32(0.01) >> 1);
    pw_ = 1 << 30;
    waveshape_ = 0;
  }

  void set_master_phase(uint32_t phase_q31) { master_phase_ = phase_q31; }

  // Frequencies: Q32 increments (converted to Q31 here); pw: Q31 (0..1);
  // waveshape: Q31 (0..1, 1.0 = INT32_MAX). out: Q30.
  template<bool enable_sync>
  void Render(uint32_t master_frequency, uint32_t frequency, uint32_t pw,
              int32_t waveshape, int32_t* out, size_t size) {
    uint32_t master_f = master_frequency >> 1;
    uint32_t f = frequency >> 1;
    if (master_f >= kMaxFrequencyQ31) master_f = kMaxFrequencyQ31;
    if (f >= kMaxFrequencyQ31) f = kMaxFrequencyQ31;
    if (f >= kMaxFrequencyQ31) {
      pw = 1u << 30;
    } else {
      const uint32_t pw_min = f << 1;
      const uint32_t pw_max = kOne31 - pw_min;
      if (pw < pw_min) pw = pw_min;
      if (pw > pw_max) pw = pw_max;
    }

    Interpolator master_fm(&master_frequency_, static_cast<int32_t>(master_f), size);
    Interpolator fm(&slave_frequency_, static_cast<int32_t>(f), size);
    Interpolator pwm(&pw_, static_cast<int32_t>(pw), size);
    Interpolator waveshape_modulation(&waveshape_, waveshape, size);

    int32_t next_sample = next_sample_;
    uint32_t master_phase = master_phase_;
    uint32_t slave_phase = slave_phase_;
    uint32_t previous_pw = previous_pw_;
    bool high = high_;

    while (size--) {
      bool reset = false;
      bool transition_during_reset = false;
      int32_t reset_time = 0;

      int32_t this_sample = next_sample;
      next_sample = 0;

      const uint32_t mf = static_cast<uint32_t>(master_fm.Next());
      const uint32_t sf = static_cast<uint32_t>(fm.Next());
      const uint32_t pw_now = static_cast<uint32_t>(pwm.Next());
      const int32_t ws = waveshape_modulation.Next();

      // max(ws - 0.5, 0) * 2 and max(1 - 2 ws, 0), Q31.
      const int32_t square_amount = ws > (1 << 30) ? (ws - (1 << 30)) << 1 : 0;
      const int32_t triangle_amount = ws < (1 << 30) ? INT32_MAX - (ws << 1) : 0;

      if (enable_sync) {
        master_phase += mf;
        if (master_phase >= kOne31) {
          master_phase -= kOne31;
          reset_time = BlepTime31(static_cast<int32_t>(master_phase), static_cast<int32_t>(mf));
          uint32_t slave_phase_at_reset = slave_phase + static_cast<uint32_t>(
              Mul(INT32_MAX - reset_time, static_cast<int32_t>(sf)));
          reset = true;
          if (slave_phase_at_reset >= kOne31) {
            slave_phase_at_reset -= kOne31;
            transition_during_reset = true;
          }
          if (!high && slave_phase_at_reset >= pw_now) {
            transition_during_reset = true;
          }
          const int32_t value = NaiveSample(slave_phase_at_reset, pw_now, triangle_amount, square_amount);
          this_sample -= Mul(value, ThisBlepSample(reset_time));
          next_sample -= Mul(value, NextBlepSample(reset_time));
        }
      }

      slave_phase += sf;
      while (transition_during_reset || !reset) {
        if (!high) {
          if (slave_phase < pw_now) break;
          const int32_t t = BlepTime31(
              static_cast<int32_t>(slave_phase - pw_now),
              static_cast<int32_t>(previous_pw - pw_now + sf));
          const int32_t triangle_step = triangle_amount ? Mul(SlopeStep(sf, pw_now), triangle_amount) : 0;
          this_sample += Mul(square_amount, ThisBlepSample(t)) >> 1;
          next_sample += Mul(square_amount, NextBlepSample(t)) >> 1;
          this_sample -= Mul(triangle_step, ThisIntegratedBlepSample(t)) >> 1;
          next_sample -= Mul(triangle_step, NextIntegratedBlepSample(t)) >> 1;
          high = true;
        }
        if (high) {
          if (slave_phase < kOne31) break;
          slave_phase -= kOne31;
          const int32_t t = BlepTime31(static_cast<int32_t>(slave_phase), static_cast<int32_t>(sf));
          const int32_t triangle_step = triangle_amount ? Mul(SlopeStep(sf, pw_now), triangle_amount) : 0;
          const int32_t not_triangle = INT32_MAX - triangle_amount;
          this_sample -= Mul(not_triangle, ThisBlepSample(t)) >> 1;
          next_sample -= Mul(not_triangle, NextBlepSample(t)) >> 1;
          this_sample += Mul(triangle_step, ThisIntegratedBlepSample(t)) >> 1;
          next_sample += Mul(triangle_step, NextIntegratedBlepSample(t)) >> 1;
          high = false;
        }
      }

      if (enable_sync && reset) {
        slave_phase = static_cast<uint32_t>(Mul(reset_time, static_cast<int32_t>(sf)));
        high = false;
      }

      next_sample += NaiveSample(slave_phase, pw_now, triangle_amount, square_amount);
      previous_pw = pw_now;
      *out++ = (this_sample - (1 << 29)) << 1;
    }

    next_sample_ = next_sample;
    master_phase_ = master_phase;
    slave_phase_ = slave_phase;
    previous_pw_ = previous_pw;
    high_ = high;
  }

 private:
  // saw += (square - saw) * sq; saw += (triangle - saw) * tri (Q30).
  static inline int32_t NaiveSample(uint32_t phase, uint32_t pw, int32_t triangle_amount,
                                    int32_t square_amount) {
    int32_t saw = static_cast<int32_t>(phase >> 1);
    const bool below = phase < pw;
    const int32_t square = below ? 0 : (1 << 30);
    saw += Mul(square - saw, square_amount);
    if (triangle_amount) {   // the division only when the triangle is heard
      const int32_t triangle = below
          ? Ratio30(phase, pw)
          : (1 << 30) - Ratio30(phase - pw, kOne31 - pw);
      saw += Mul(triangle - saw, triangle_amount);
    }
    return saw;
  }

  uint32_t master_phase_;
  uint32_t slave_phase_;
  int32_t next_sample_;
  uint32_t previous_pw_;
  bool high_;

  int32_t master_frequency_;
  int32_t slave_frequency_;
  int32_t pw_;
  int32_t waveshape_;
};

class VariableSawOscillatorFixed {
 public:
  void Init() {
    phase_ = 0;
    next_sample_ = 0;
    previous_pw_ = 1u << 30;
    high_ = false;
    frequency_ = static_cast<int32_t>(Q32(0.01) >> 1);
    pw_ = 1 << 30;
    waveshape_ = 0;
  }

  // frequency Q32; pw Q31; waveshape Q31 (0..1). out: Q30.
  void Render(uint32_t frequency, uint32_t pw, int32_t waveshape, int32_t* out, size_t size) {
    uint32_t f = frequency >> 1;
    if (f >= kMaxFrequencyQ31) f = kMaxFrequencyQ31;
    if (f >= kMaxFrequencyQ31) {
      pw = 1u << 30;
    } else {
      const uint32_t pw_min = f << 1;
      const uint32_t pw_max = kOne31 - pw_min;
      if (pw < pw_min) pw = pw_min;
      if (pw > pw_max) pw = pw_max;
    }
    Interpolator fm(&frequency_, static_cast<int32_t>(f), size);
    Interpolator pwm(&pw_, static_cast<int32_t>(pw), size);
    Interpolator waveshape_modulation(&waveshape_, waveshape, size);

    int32_t next_sample = next_sample_;
    uint32_t phase = phase_;
    uint32_t previous_pw = previous_pw_;
    bool high = high_;
    // kVariableSawNotchDepth = 0.2.
    const int32_t kNotchDepthQ30 = Q(0.2, 30);

    while (size--) {
      int32_t this_sample = next_sample;
      next_sample = 0;
      const uint32_t fr = static_cast<uint32_t>(fm.Next());
      const uint32_t pw_now = static_cast<uint32_t>(pwm.Next());
      const int32_t triangle_amount = waveshape_modulation.Next();
      const int32_t notch_amount = INT32_MAX - triangle_amount;

      phase += fr;
      if (!high && phase >= pw_now) {
        const int32_t triangle_step = triangle_amount ? Mul(SlopeStep(fr, pw_now), triangle_amount) : 0;
        // (0.2 + 1 - pw) * notch_amount, Q30.
        const int32_t notch = Mul(kNotchDepthQ30 + (1 << 30) - static_cast<int32_t>(pw_now >> 1),
                                  notch_amount);
        const int32_t t = BlepTime31(static_cast<int32_t>(phase - pw_now),
                                     static_cast<int32_t>(previous_pw - pw_now + fr));
        this_sample += Mul(notch, ThisBlepSample(t));
        next_sample += Mul(notch, NextBlepSample(t));
        this_sample -= Mul(triangle_step, ThisIntegratedBlepSample(t)) >> 1;
        next_sample -= Mul(triangle_step, NextIntegratedBlepSample(t)) >> 1;
        high = true;
      } else if (phase >= kOne31) {
        phase -= kOne31;
        const int32_t triangle_step = triangle_amount ? Mul(SlopeStep(fr, pw_now), triangle_amount) : 0;
        const int32_t notch = Mul(kNotchDepthQ30 + (1 << 30), notch_amount);
        const int32_t t = BlepTime31(static_cast<int32_t>(phase), static_cast<int32_t>(fr));
        this_sample -= Mul(notch, ThisBlepSample(t));
        next_sample -= Mul(notch, NextBlepSample(t));
        this_sample += Mul(triangle_step, ThisIntegratedBlepSample(t)) >> 1;
        next_sample += Mul(triangle_step, NextIntegratedBlepSample(t)) >> 1;
        high = false;
      }

      // notch_saw = phase < pw ? phase : 1.2; triangle = phase / pw or
      // 1 - (phase - pw) / (1 - pw).
      const bool below = phase < pw_now;
      const int32_t notch_saw = below ? static_cast<int32_t>(phase >> 1) : kNotchDepthQ30 + (1 << 30);
      next_sample += Mul(notch_saw, notch_amount);
      if (triangle_amount) {
        const int32_t triangle = below
            ? Ratio30(phase, pw_now)
            : (1 << 30) - Ratio30(phase - pw_now, kOne31 - pw_now);
        next_sample += Mul(triangle, triangle_amount);
      }
      previous_pw = pw_now;

      // (2 x - 1) / 1.2.
      *out++ = Mul((this_sample - (1 << 29)) << 1, Q31(1.0 / 1.2));
    }
    next_sample_ = next_sample;
    phase_ = phase;
    previous_pw_ = previous_pw;
    high_ = high;
  }

 private:
  uint32_t phase_;
  int32_t next_sample_;
  uint32_t previous_pw_;
  bool high_;

  int32_t frequency_;
  int32_t pw_;
  int32_t waveshape_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_VARIABLE_SHAPE_OSCILLATOR_H_
