// The chord engine's oscillators in fixed point:
//   - StringSynthOscillator: a divide-down organ / string machine voice, four
//     saws an octave apart from one phase in [0, 8) (Q29, so 8 wraps);
//   - WavetableOscillator<128, 15>: integrated waves, linear interpolation,
//     a differentiator (the integrated wave's derivative) and a one-pole.
// Both add into Q28 buffers.

#ifndef OP1PLAITS_FIXED_CHORD_OSCILLATORS_H_
#define OP1PLAITS_FIXED_CHORD_OSCILLATORS_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

class StringSynthOscillatorFixed {
 public:
  void Init() {
    phase_ = 0;
    next_sample_ = 0;
    segment_ = 0;
    frequency_ = Q(0.001, 29);   // upstream: 0.001 in units of the [0, 8) phase
    saw_8_gain_ = saw_4_gain_ = saw_2_gain_ = saw_1_gain_ = 0;
  }

  // frequency: Q32 (saturated at 0xffffffff); registration[7]: Q31;
  // gain: Q31. Adds 2 x this_sample into out (Q28).
  void Render(uint32_t frequency, const int32_t* unshifted_registration, int32_t gain,
              int32_t* out, size_t size) {
    // frequency *= 8: in Q29 units of the [0, 8) phase, 8 f is f's Q32
    // value. Shift very high notes down an octave at a time (playing a
    // higher harmonic of a lower wave).
    uint32_t f = frequency;
    size_t shift = 0;
    while (f > (1u << 28)) {   // 8 f > 0.5
      shift += 2;
      f >>= 1;
    }
    if (shift >= 8) return;

    int32_t registration[7];
    for (size_t i = 0; i < shift; ++i) registration[i] = 0;
    for (size_t i = shift; i < 7; ++i) registration[i] = unshifted_registration[i - shift];

    // (r0 + 2 r1) gain etc., summed in Q30.
    Interpolator fm(&frequency_, static_cast<int32_t>(f), size);
    Interpolator saw_8_gain_modulation(
        &saw_8_gain_, Mul((registration[0] >> 1) + registration[1], gain) << 1, size);
    Interpolator saw_4_gain_modulation(
        &saw_4_gain_,
        Mul((registration[2] >> 1) - (registration[1] >> 1) + registration[3], gain) << 1, size);
    Interpolator saw_2_gain_modulation(
        &saw_2_gain_,
        Mul((registration[4] >> 1) - (registration[3] >> 1) + registration[5], gain) << 1, size);
    Interpolator saw_1_gain_modulation(
        &saw_1_gain_, Mul((registration[6] >> 1) - (registration[5] >> 1), gain) << 1, size);

    uint32_t phase = phase_;
    int32_t next_sample = next_sample_;
    uint32_t segment = segment_;
    while (size--) {
      int32_t this_sample = next_sample;
      next_sample = 0;
      const uint32_t fr = static_cast<uint32_t>(fm.Next());
      const int32_t saw_8_gain = saw_8_gain_modulation.Next();
      const int32_t saw_4_gain = saw_4_gain_modulation.Next();
      const int32_t saw_2_gain = saw_2_gain_modulation.Next();
      const int32_t saw_1_gain = saw_1_gain_modulation.Next();
      phase += fr;   // wraps at 8
      const uint32_t next_segment = phase >> 29;
      if (next_segment != segment) {
        int32_t discontinuity = 0;
        if (next_segment == 0) discontinuity -= saw_8_gain;   // wrapped past 8
        if ((next_segment & 3) == 0) discontinuity -= saw_4_gain;
        if ((next_segment & 1) == 0) discontinuity -= saw_2_gain;
        discontinuity -= saw_1_gain;
        if (discontinuity != 0) {
          const uint32_t fraction = phase & ((1u << 29) - 1);
          const int32_t t = fraction < fr ? Fraction(fraction, fr) : INT32_MAX;
          this_sample += Mul(ThisBlepSample(t), discontinuity) >> 2;
          next_sample += Mul(NextBlepSample(t), discontinuity) >> 2;
        }
      }
      segment = next_segment;

      next_sample += Mul(static_cast<int32_t>(phase - (4u << 29)), saw_8_gain) >> 3;
      next_sample += Mul(static_cast<int32_t>(phase - ((segment & 4) << 29) - (2u << 29)),
                         saw_4_gain) >> 2;
      next_sample += Mul(static_cast<int32_t>(phase - ((segment & 6) << 29) - (1u << 29)),
                         saw_2_gain) >> 1;
      next_sample += Mul(static_cast<int32_t>(phase - ((segment & 7) << 29) - (1u << 28)),
                         saw_1_gain);
      *out++ += this_sample;   // 2 x this (Q29) in Q28
    }
    next_sample_ = next_sample;
    phase_ = phase;
    segment_ = segment;
  }

 private:
  uint32_t phase_;        // Q29 in [0, 8)
  int32_t next_sample_;   // Q29
  uint32_t segment_;

  int32_t frequency_;     // Q29 increment of the [0, 8) phase
  int32_t saw_8_gain_;    // Q31
  int32_t saw_4_gain_;
  int32_t saw_2_gain_;
  int32_t saw_1_gain_;
};

// 1 / (f 2^17) in Q35, saturated (f in Q32; exact for f >= 1.63e-4).
inline int32_t IntegratedWaveScale(uint32_t f) {
  if (f < 700000u) return INT32_MAX;
  const int n = CountLeadingZeros(f);
  return ReciprocalNormalized(static_cast<int32_t>((f << n) >> 1)) >> (12 - n);
}

template<size_t num_waves>
class WavetableOscillatorFixed {
 public:
  void Init() {
    phase_ = 0;
    frequency_ = 0;
    amplitude_ = 0;
    waveform_ = 0;
    lp_ = 0;
    diff_previous_ = 0;
    diff_lp_ = 0;
  }

  // frequency Q32; amplitude Q31; waveform Q31 (0..1); wavetable: the waves
  // (132 int16 each). Adds into out (Q28). Upstream's approximate_scale and
  // attenuate_high_frequencies variant (the chord engine's).
  void Render(uint32_t frequency, int32_t amplitude, int32_t waveform,
              const int16_t* const* wavetable, int32_t* out, size_t size) {
    if (frequency < 429u) frequency = 429u;          // 1e-7
    if (frequency > (1u << 30)) frequency = 1u << 30;   // kMaxFrequency
    // amplitude *= 1 - 2 f (2 f in Q31 is f in Q32); *= 1 / (f 131072).
    amplitude = Mul(amplitude, INT32_MAX - static_cast<int32_t>(frequency));
    const int32_t scaled_amplitude = Mul(amplitude, IntegratedWaveScale(frequency));   // Q35

    Interpolator frequency_modulation(&frequency_, static_cast<int32_t>(frequency), size);
    Interpolator amplitude_modulation(&amplitude_, scaled_amplitude, size);
    // waveform * (num_waves - 1.0001), Q24.
    Interpolator waveform_modulation(
        &waveform_, Mul(waveform, Q((num_waves - 1.0001) / 16.0, 31)) >> 3, size);

    int32_t lp = lp_;
    uint32_t phase = phase_;
    int32_t diff_previous = diff_previous_;
    int32_t diff_lp = diff_lp_;
    while (size--) {
      const uint32_t f0 = static_cast<uint32_t>(frequency_modulation.Next());
      const int32_t cutoff = f0 >= (1u << 25) ? INT32_MAX : static_cast<int32_t>(f0 << 6);
      phase += f0;
      int32_t w = waveform_modulation.Next();
      if (w < 0) w = 0;   // a rounded ramp may dip a few LSB below 0
      const int32_t w_integral = w >> 24;
      const int32_t w_fractional = (w & 0xffffff) << 7;   // Q31
      const uint32_t p_integral = phase >> 25;
      const int32_t p_fractional = static_cast<int32_t>((phase << 7) >> 1);
      const int16_t* t0 = wavetable[w_integral] + p_integral;
      const int16_t* t1 = wavetable[w_integral + 1] + p_integral;
      const int32_t x0 = (t0[0] << 12) + Mul((t0[1] - t0[0]) << 12, p_fractional);
      const int32_t x1 = (t1[0] << 12) + Mul((t1[1] - t1[0]) << 12, p_fractional);
      const int32_t s_in = x0 + Mul(x1 - x0, w_fractional);   // Q12
      diff_lp += Mul((s_in - diff_previous) - diff_lp, cutoff);
      diff_previous = s_in;
      lp += Mul(diff_lp - lp, cutoff);
      // lp (Q12) x amplitude (Q35) -> Q16 (here Q18 with lp << 2) -> Q28.
      *out++ += Mul(lp << 2, amplitude_modulation.Next()) << 10;
    }
    lp_ = lp;
    phase_ = phase;
    diff_previous_ = diff_previous;
    diff_lp_ = diff_lp;
  }

 private:
  uint32_t phase_;
  int32_t frequency_;   // Q32
  int32_t amplitude_;   // Q35
  int32_t waveform_;    // Q24
  int32_t lp_;          // Q12
  int32_t diff_previous_;
  int32_t diff_lp_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_CHORD_OSCILLATORS_H_
