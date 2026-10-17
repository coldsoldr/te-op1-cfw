#include "fixed/noise_engine.h"

namespace op1plaits {

void ClockedNoiseFixed::Render(bool sync, uint32_t frequency, int32_t* out, size_t size) {
  if (frequency > (1u << 31)) frequency = 1u << 31;
  Interpolator fm(&frequency_, static_cast<int32_t>(frequency == (1u << 31) ? INT32_MAX : frequency),
                  size);
  int32_t next_sample = next_sample_;
  int32_t sample = sample_;
  uint32_t phase = phase_;
  if (sync) phase = 1u << 31;

  while (size--) {
    int32_t this_sample = next_sample;
    next_sample = 0;
    const uint32_t f = static_cast<uint32_t>(fm.Next());
    // Random::GetFloat() * 2 - 1, Q29.
    const int32_t raw_sample = static_cast<int32_t>(Random::GetWord() ^ 0x80000000u) >> 2;
    // raw_amount = clamp(4 (f - 0.25), 0, 1), Q31.
    const int32_t raw_amount = f <= (1u << 29) ? 0
        : (f >= (1u << 30) ? INT32_MAX : static_cast<int32_t>((f - (1u << 29)) << 2));
    phase += f;
    if (phase >= (1u << 31)) {
      phase -= 1u << 31;
      const int32_t t = phase < f ? Fraction(phase, f) : INT32_MAX;
      const int32_t discontinuity = raw_sample - sample;   // Q29 (|.| <= 2)
      this_sample += Mul(discontinuity, ThisBlepSample(t));
      next_sample += Mul(discontinuity, NextBlepSample(t));
      sample = raw_sample;
    }
    next_sample += sample;
    *out++ = this_sample + Mul(raw_sample - this_sample, raw_amount);
  }
  next_sample_ = next_sample;
  sample_ = sample;
  phase_ = phase;
}

void NoiseEngineFixed::Init(Allocator* allocator) {
  clocked_noise_[0].Init();
  clocked_noise_[1].Init();
  lp_hp_filter_.Init();
  bp_filter_[0].Init();
  bp_filter_[1].Init();
  previous_g0_ = previous_g1_ = 0;
  previous_r_ = 1 << 29;
  previous_h0_ = previous_h1_ = INT32_MAX;
  previous_gain_ = 0;
  previous_mode_ = 0;
  first_ = true;
  temp_buffer_ = allocator->Allocate<int32_t>(kMaxBlockSize);
}

void NoiseEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                              size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const uint32_t f1 = NoteToFrequency(parameters.note + parameters.harmonics * 96 - (24 << 16));
  const int32_t clock_lowest_note = parameters.trigger & TRIGGER_UNPATCHED ? 0 : -(24 << 16);
  // timbre * (128 - lowest) + lowest, Q16.
  const int32_t clock_note = Mul64(parameters.timbre, (128 << 16) - clock_lowest_note, 15) +
                             clock_lowest_note;
  const uint32_t clock_f = NoteToFrequency(clock_note);
  // q = 0.5 * SemitonesToRatio(morph * 120).
  const FFloat q = SemitonesToRatio(parameters.morph * 240).Scaled(-1);
  const bool sync = parameters.trigger & TRIGGER_RISING_EDGE;

  // The clocks, Q31 (up to 1.0); the second at clock_f * f1 / f0.
  const FFloat clock = FFloatFromQ32(clock_f);
  const uint32_t clock_q31 = clock_f >> 1;
  const FFloat clock_2 = clock * FFloatFromQ32(f1) / FFloatFromQ32(f0);
  const int32_t clock_2_q31 = clock_2.ToQ31();   // saturates at 1.0
  clocked_noise_[0].Render(sync, clock_q31, aux, size);
  clocked_noise_[1].Render(sync, static_cast<uint32_t>(clock_2_q31), temp_buffer_, size);

  // Coefficients at the block's end.
  const int32_t g0 = TanQ27<FREQUENCY_ACCURATE>(f0);
  const int32_t g1 = TanQ27<FREQUENCY_ACCURATE>(f1);
  const int32_t r = FFloat::Reciprocal(q).ToQ(29);
  const int32_t h0 = SvfH(g0, r);
  const int32_t h1 = SvfH(g1, r);
  // gain = 1 / sqrt((0.5 + q) * 40 * f0), Q27 (saturates at 16).
  static constexpr FFloat kHalf(0.5);   // constexpr: no run-time soft-float
  static constexpr FFloat kForty(40.0);
  const FFloat x = (kHalf + q) * kForty * FFloatFromQ32(f0);
  const int32_t gain = FFloat::ReciprocalSqrt(x).ToQ(27);
  const int32_t mode = parameters.harmonics >= kQ15One ? INT32_MAX : parameters.harmonics << 16;
  if (first_) {
    previous_g0_ = g0;
    previous_g1_ = g1;
    previous_r_ = r;
    previous_h0_ = h0;
    previous_h1_ = h1;
    previous_gain_ = gain;
    previous_mode_ = mode;
    first_ = false;
  }
  const int32_t shift0 = ShiftFor(g0 > previous_g0_ ? g0 : previous_g0_);
  const int32_t shift1 = ShiftFor(g1 > previous_g1_ ? g1 : previous_g1_);

  Interpolator g0_modulation(&previous_g0_, g0, size);
  Interpolator g1_modulation(&previous_g1_, g1, size);
  Interpolator r_modulation(&previous_r_, r, size);
  Interpolator h0_modulation(&previous_h0_, h0, size);
  Interpolator h1_modulation(&previous_h1_, h1, size);
  Interpolator gain_modulation(&previous_gain_, gain, size);
  Interpolator mode_modulation(&previous_mode_, mode, size);

  const int32_t* in_1 = aux;
  const int32_t* in_2 = temp_buffer_;
  for (size_t i = 0; i < size; ++i) {
    const int32_t gs0 = g0_modulation.Next() << (4 - shift0);
    const int32_t gs1 = g1_modulation.Next() << (4 - shift1);
    const int32_t rr = r_modulation.Next();
    const int32_t hh0 = h0_modulation.Next();
    const int32_t hh1 = h1_modulation.Next();
    const int32_t gg = gain_modulation.Next();
    const int32_t m = mode_modulation.Next();

    // Noise (Q29) x gain (Q27) -> Q25 signals.
    const int32_t input_1 = Mul(in_1[i], gg);
    const int32_t input_2 = Mul(in_2[i], gg);

    // ProcessMultimodeLPtoHP: hp_gain = min(1 - 2 mode, 0), bp_gain = 1 -
    // 2 |mode - 0.5|, lp_gain = max(1 - 2 mode, 0) (Q30).
    const int32_t one_minus = (1 << 30) - (m >> 0);   // 1 - 2 mode, Q30
    const int32_t hp_gain = one_minus < 0 ? one_minus : 0;
    const int32_t lp_gain = one_minus > 0 ? one_minus : 0;
    const int32_t bp_gain = (1 << 30) - Abs(one_minus);
    int32_t lp, bp, hp;
    lp_hp_filter_.Tick(input_1, gs0, shift0, rr, hh0, &lp, &bp, &hp);
    const int32_t o = (Mul(hp, hp_gain) + Mul(bp, bp_gain) + Mul(lp, lp_gain)) << 1;   // Q25
    out[i] = ShlSat(o, kAudioBits - 25);

    int32_t lp0, bp0, hp0, lp1, bp1, hp1;
    bp_filter_[0].Tick(input_1, gs0, shift0, rr, hh0, &lp0, &bp0, &hp0);
    bp_filter_[1].Tick(input_2, gs1, shift1, rr, hh1, &lp1, &bp1, &hp1);
    aux[i] = ShlSat(bp0 + bp1, kAudioBits - 25);
  }
}

}  // namespace op1plaits
