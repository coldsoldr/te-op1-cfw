#include "fixed/grain_engine.h"

namespace op1plaits {

namespace {

const uint32_t kOne31u = 1u << 31;   // 1.0 in unsigned Q31

// t^3, Q30.
inline int32_t Cube30(int32_t t) {
  const int32_t t2 = Mul(t, t) << 1;
  return Mul(t2, t) << 1;
}

// GrainletOscillator::Carrier(phase, shape): phase Q30 (0..1), shape Q30.
// Q31 out, in [0, 0.5].
inline int32_t Carrier(int32_t phase, int32_t shape) {
  const int32_t s3 = (shape >> 1) * 3;           // shape * 3, Q29
  const int32_t integral = s3 >> 29;
  const int32_t fractional = (s3 & ((1 << 29) - 1)) << 1;   // Q30
  int32_t t = (1 << 30) - fractional;
  int32_t p;   // Q30
  if (integral == 0) {
    const int32_t mult = (1 << 26) + 15 * (Cube30(t) >> 4);   // 1 + 15 t^3, Q26
    p = ShlSat(Mul(phase, mult), 5);
    if (p >= (1 << 30)) p = 1 << 30;
    p += Q(0.75, 30);
  } else if (integral == 1) {
    const int32_t breakpoint = Q(0.001, 30) + Mul(Cube30(t), Q31(0.499));   // Q30
    if (phase < breakpoint) {
      p = Fraction(static_cast<uint32_t>(phase), static_cast<uint32_t>(breakpoint)) >> 2;
    } else {
      const uint32_t num = static_cast<uint32_t>(phase - breakpoint);
      const uint32_t den = static_cast<uint32_t>((1 << 30) - breakpoint);
      p = (1 << 29) + (num < den ? Fraction(num, den) >> 2 : (1 << 29));
    }
    p += Q(0.75, 30);
  } else {
    t = (1 << 30) - t;
    const int32_t mult = Q(0.5, 27) + (Mul(Cube30(t), Q31(14.5 / 16.0)) << 1);   // 0.5 + 14.5 t^3, Q27
    // phase (0.5 + 14.5 t^3) reaches 15: saturate, then clamp at 0.75.
    const int32_t x = ShlSat(Mul(phase, mult), 4);
    p = x >= Q(0.5, 30) ? Q(0.75, 30) : Q(0.25, 30) + x;
  }
  return (Sine(static_cast<uint32_t>(p) << 2) >> 2) + (1 << 29);
}

// Grainlet(carrier_phase, formant_phase, shape, bleed): carrier (formant +
// bleed) / (1 + bleed). Phase Q30 / Q32; shape, bleed Q30. Q30 out.
inline int32_t Grainlet(int32_t carrier_phase, uint32_t formant_phase, int32_t shape,
                        int32_t bleed) {
  const int32_t carrier = Carrier(carrier_phase, shape);       // Q31
  const int32_t num = (Sine(formant_phase) >> 1) + bleed;      // Q30
  const uint32_t den = static_cast<uint32_t>((1 << 30) + bleed);
  const uint32_t a = static_cast<uint32_t>(num < 0 ? -num : num);
  int32_t r = a < den ? Fraction(a, den) : INT32_MAX;          // Q31
  if (num < 0) r = -r;
  return Mul(carrier, r) >> 1;
}

}  // namespace

void GrainletOscillatorFixed::Render(uint32_t carrier_frequency, uint32_t formant_frequency,
                                     int32_t carrier_shape, int32_t carrier_bleed,
                                     int32_t* out, size_t size) {
  if (carrier_frequency >= (1u << 29)) carrier_frequency = 1u << 29;   // kMaxFrequency / 2
  if (formant_frequency >= (1u << 30)) formant_frequency = 1u << 30;
  Interpolator carrier_frequency_modulation(&carrier_frequency_,
                                            static_cast<int32_t>(carrier_frequency), size);
  Interpolator formant_frequency_modulation(&formant_frequency_,
                                            static_cast<int32_t>(formant_frequency), size);
  Interpolator carrier_shape_modulation(&carrier_shape_, carrier_shape, size);
  Interpolator carrier_bleed_modulation(&carrier_bleed_, carrier_bleed, size);
  int32_t next_sample = next_sample_;

  while (size--) {
    int32_t this_sample = next_sample;
    next_sample = 0;
    const uint32_t f0 = static_cast<uint32_t>(carrier_frequency_modulation.Next()) >> 1;   // Q31
    const uint32_t f1 = static_cast<uint32_t>(formant_frequency_modulation.Next());        // Q32
    carrier_phase_ += f0;
    if (carrier_phase_ >= kOne31u) {
      carrier_phase_ -= kOne31u;
      const int32_t reset_time = carrier_phase_ < f0 ? Fraction(carrier_phase_, f0) : INT32_MAX;
      const int32_t before_t = INT32_MAX - reset_time;
      const int32_t before = Grainlet(
          1 << 30, formant_phase_ + static_cast<uint32_t>(Mul(before_t, static_cast<int32_t>(f1))),
          carrier_shape_modulation.subsample(before_t),
          carrier_bleed_modulation.subsample(before_t));
      const int32_t after = Grainlet(
          0, 0, carrier_shape_modulation.subsample(INT32_MAX),
          carrier_bleed_modulation.subsample(INT32_MAX));
      const int32_t discontinuity = after - before;   // Q30
      this_sample += Mul(discontinuity, ThisBlepSample(reset_time));
      next_sample += Mul(discontinuity, NextBlepSample(reset_time));
      formant_phase_ = static_cast<uint32_t>(Mul(reset_time, static_cast<int32_t>(f1)));
    } else {
      formant_phase_ += f1;
    }
    next_sample += Grainlet(static_cast<int32_t>(carrier_phase_ >> 1), formant_phase_,
                            carrier_shape_modulation.Next(), carrier_bleed_modulation.Next());
    *out++ = this_sample;
  }
  next_sample_ = next_sample;
}

namespace {

// ZOscillator::Z(c, d, f, shape, mode): c, d unsigned Q31 (0..1); f Q32;
// shape, mode Q30. Q29 out (|.| <= 3).
inline int32_t Z(uint32_t c, uint32_t d, uint32_t f, int32_t shape, int32_t mode) {
  int32_t ramp_down = (Sine(d + (1u << 30)) >> 1) + (1 << 30);   // Q31: 0.5 (1 + sin(0.5 d + 0.25))
  int32_t offset;          // Q30
  int32_t phase_shift;     // Q30 (cycles)
  if (mode < Q(0.333, 30)) {
    offset = 1 << 30;
    phase_shift = Q(0.25, 30) + Mul(mode, Q31(0.75)) * 2;   // 0.25 + 1.5 mode
  } else if (mode < Q(0.666, 30)) {
    phase_shift = Q(0.7495, 30) - Mul(mode - Q(0.33, 30), Q31(0.75));
    offset = -(Sine(static_cast<uint32_t>(phase_shift) << 2) >> 1);
  } else {
    phase_shift = Q(0.7495, 30) - Mul(mode - Q(0.33, 30), Q31(0.75));
    offset = Q(0.001, 30);
  }
  const int32_t discontinuity = Sine(f + (static_cast<uint32_t>(phase_shift) << 2)) >> 1;   // Q30
  int32_t contour;   // Q30
  if (shape < (1 << 29)) {
    const int32_t s2 = shape << 1;   // Q30
    if (c >= (1u << 30)) ramp_down = Mul(ramp_down, s2) << 1;
    contour = (1 << 30) + Mul((Sine((c << 1) + (1u << 30)) >> 1) - (1 << 30), s2 << 1);
  } else {
    contour = Sine((c << 1) + (static_cast<uint32_t>(shape) << 1)) >> 1;
  }
  // (ramp_down (offset + discontinuity) - offset) contour.
  const int32_t a = Mul(ramp_down, (offset >> 1) + (discontinuity >> 1)) - (offset >> 1);   // Q29
  return Mul(a, contour) << 1;   // Q29
}

}  // namespace

void ZOscillatorFixed::Render(uint32_t carrier_frequency, uint32_t formant_frequency,
                              int32_t carrier_shape, int32_t mode, int32_t* out, size_t size) {
  if (carrier_frequency >= (1u << 29)) carrier_frequency = 1u << 29;
  if (formant_frequency >= (1u << 30)) formant_frequency = 1u << 30;
  Interpolator carrier_frequency_modulation(&carrier_frequency_,
                                            static_cast<int32_t>(carrier_frequency), size);
  Interpolator formant_frequency_modulation(&formant_frequency_,
                                            static_cast<int32_t>(formant_frequency), size);
  Interpolator carrier_shape_modulation(&carrier_shape_, carrier_shape, size);
  Interpolator mode_modulation(&mode_, mode, size);
  int32_t next_sample = next_sample_;

  while (size--) {
    int32_t this_sample = next_sample;
    next_sample = 0;
    const uint32_t f0 = static_cast<uint32_t>(carrier_frequency_modulation.Next());   // Q32 = 2 f0 in Q31
    const uint32_t f1 = static_cast<uint32_t>(formant_frequency_modulation.Next());
    discontinuity_phase_ += f0;           // 2 f0
    carrier_phase_ += f0 >> 1;            // f0
    if (discontinuity_phase_ >= kOne31u) {
      discontinuity_phase_ -= kOne31u;
      const int32_t reset_time = discontinuity_phase_ < f0 ? Fraction(discontinuity_phase_, f0) : INT32_MAX;
      const bool past = carrier_phase_ >= kOne31u;
      const uint32_t carrier_phase_before = past ? kOne31u : (1u << 30);
      const uint32_t carrier_phase_after = past ? 0 : (1u << 30);
      const int32_t before_t = INT32_MAX - reset_time;
      const int32_t before = Z(carrier_phase_before, kOne31u,
                               formant_phase_ + static_cast<uint32_t>(Mul(before_t, static_cast<int32_t>(f1))),
                               carrier_shape_modulation.subsample(before_t),
                               mode_modulation.subsample(before_t));
      const int32_t after = Z(carrier_phase_after, 0, 0, carrier_shape_modulation.subsample(INT32_MAX),
                              mode_modulation.subsample(INT32_MAX));
      const int32_t discontinuity = after - before;   // Q29
      this_sample += Mul(discontinuity, ThisBlepSample(reset_time)) >> 1;
      next_sample += Mul(discontinuity, NextBlepSample(reset_time)) >> 1;
      formant_phase_ = static_cast<uint32_t>(Mul(reset_time, static_cast<int32_t>(f1)));
      if (carrier_phase_ > kOne31u) carrier_phase_ = discontinuity_phase_ >> 1;
    } else {
      formant_phase_ += f1;
    }
    if (carrier_phase_ >= kOne31u) carrier_phase_ -= kOne31u;
    next_sample += Z(carrier_phase_, discontinuity_phase_, formant_phase_,
                     carrier_shape_modulation.Next(), mode_modulation.Next()) >> 1;   // Q28
    *out++ = this_sample;
  }
  next_sample_ = next_sample;
}

void GrainEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  grainlet_[0].Init();
  grainlet_[1].Init();
  z_oscillator_.Init();
  dc_blocker_[0].Init();
  dc_blocker_[1].Init();
}

void GrainEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                              size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const int32_t root = parameters.note;
  const uint32_t f0 = NoteToFrequency(root);
  const uint32_t f1 = NoteToFrequency((24 << 16) + parameters.timbre * 168);   // 24 + 84 timbre
  // ratio = SemitonesToRatio(-24 + 48 harmonics): f1 * ratio.
  const FFloat ratio = SemitonesToRatio(-(24 << 16) + parameters.harmonics * 96);
  const int32_t f1_ratio_q31 = (FFloatFromQ32(f1) * ratio).ToQ31();   // saturates at 1.0
  const uint32_t f1_ratio = static_cast<uint32_t>(f1_ratio_q31) << 1;
  const int32_t h = parameters.harmonics;
  // carrier_bleed = h < 0.5 ? 1 - 2 h : 0; fixed = b (2 - b) (Q30).
  const int32_t carrier_bleed = h < kQ15One / 2 ? (kQ15One - 2 * h) : 0;     // Q15
  const int32_t carrier_bleed_fixed = MulQ15(carrier_bleed, 2 * kQ15One - carrier_bleed) << 15;
  // carrier_shape = 0.33 + (morph - 0.33) max(1 - 24 f0, 0), Q30.
  const int64_t k = (1LL << 31) - static_cast<int64_t>(f0) * 12;   // 1 - 24 f0 in Q31
  const int32_t kk = k <= 0 ? 0 : (k >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(k));
  const int32_t carrier_shape = Q(0.33, 30) + Mul((parameters.morph << 15) - Q(0.33, 30), kk);

  grainlet_[0].Render(f0, f1, carrier_shape, carrier_bleed_fixed, out, size);
  grainlet_[1].Render(f0, f1_ratio, carrier_shape, carrier_bleed_fixed, aux, size);
  // dc_blocker_[0].set_f<FREQUENCY_DIRTY>(0.3 f0).
  dc_blocker_[0].set_f<FREQUENCY_DIRTY>(static_cast<uint32_t>(Mul(static_cast<int32_t>(f0 >> 1), Q31(0.3))) << 1);
  for (size_t i = 0; i < size; ++i) {
    out[i] = dc_blocker_[0].Process<FILTER_MODE_HIGH_PASS>(out[i] + aux[i]) >> (30 - kAudioBits);
  }
  const uint32_t cutoff = NoteToFrequency(root + parameters.timbre * 192);   // + 96 timbre
  z_oscillator_.Render(f0, cutoff, parameters.morph << 15, parameters.harmonics << 15, aux, size);
  dc_blocker_[1].set_f<FREQUENCY_DIRTY>(static_cast<uint32_t>(Mul(static_cast<int32_t>(f0 >> 1), Q31(0.3))) << 1);
  dc_blocker_[1].Process<FILTER_MODE_HIGH_PASS>(aux, size);
  for (size_t i = 0; i < size; ++i) aux[i] >>= (28 - kAudioBits);
}

}  // namespace op1plaits
