#include "fixed/swarm_engine.h"

namespace op1plaits {

void GrainEnvelopeFixed::Step(int32_t rate, bool burst_mode, bool start_burst) {
  bool randomize = false;
  if (start_burst) {
    phase_ = 1 << 27;     // 0.5
    fm_ = INT32_MAX;      // 16 (Q27 saturates 1 LSB below)
    randomize = true;
  } else {
    phase_ += Mul(rate, fm_) << 1;   // Q31 x Q27 -> Q27 -> Q28
    if (phase_ >= (1 << 28)) {
      phase_ &= (1 << 28) - 1;
      randomize = true;
    }
  }
  if (randomize) {
    from_ += interval_;
    interval_ = static_cast<int32_t>(Random::GetWord() >> 2) - from_;   // u - from, Q30
    // Randomize the duration of the grain.
    if (burst_mode) {
      const int32_t u = static_cast<int32_t>(Random::GetWord() >> 1);   // Q31
      fm_ = Mul(fm_, Q31(0.8) + Mul(u, Q31(0.2)));
    } else {
      const int32_t u = static_cast<int32_t>(Random::GetWord() >> 1);
      fm_ = Q(0.5, 27) + (Mul(u, Q31(0.75)) >> 3);   // 0.5 + 1.5 u, Q27
    }
  }
}

int32_t GrainEnvelopeFixed::frequency(int32_t size_ratio) const {
  // Two overlapping grains approximated by a glide from f1 to f2.
  if (size_ratio < (1 << 24)) {
    return ((from_ + Mul(interval_, phase_ << 3)) << 1) - (1 << 30);
  }
  return from_;
}

int32_t GrainEnvelopeFixed::amplitude(int32_t size_ratio) {
  int32_t target = INT32_MAX;
  if (size_ratio >= (1 << 24)) {
    // phase = (phase - 0.5) size_ratio, clamped to [-1, 1] (Q30).
    int64_t p = (static_cast<int64_t>((phase_ << 2) - (1 << 29)) * size_ratio) >> 24;
    if (p > (1 << 30)) p = 1 << 30;
    if (p < -(1 << 30)) p = -(1 << 30);
    // e = Sine(0.5 phase + 1.25); target = 0.5 (e + 1).
    const int32_t e = Sine((static_cast<uint32_t>(static_cast<int32_t>(p)) << 1) + (1u << 30));
    target = (e >> 1) + (1 << 30);
  }
  if ((size_ratio >= (1 << 24)) ^ (previous_size_ratio_ >= (1 << 24))) {
    filter_coefficient_ = 1 << 30;   // 0.5
  }
  filter_coefficient_ = MulRound(filter_coefficient_, Q31(0.95));
  previous_size_ratio_ = size_ratio;
  amplitude_ += MulRound(target - amplitude_, (1 << 30) - filter_coefficient_);
  return amplitude_;
}

void AdditiveSawOscillatorFixed::Render(uint32_t frequency, int32_t level, int32_t* out,
                                        size_t size) {
  if (frequency >= (1u << 30)) frequency = 1u << 30;
  Interpolator fm(&frequency_, static_cast<int32_t>(frequency), size);
  Interpolator gain(&gain_, level, size);
  int32_t next_sample = next_sample_;
  uint32_t phase = phase_;
  while (size--) {
    int32_t this_sample = next_sample;
    next_sample = 0;
    const uint32_t f = static_cast<uint32_t>(fm.Next());
    const uint32_t old = phase;
    phase += f;
    if (phase < old) {
      const int32_t t = phase < f ? Fraction(phase, f) : INT32_MAX;
      this_sample -= ThisBlepSample(t) >> 1;
      next_sample -= NextBlepSample(t) >> 1;
    }
    next_sample += static_cast<int32_t>(phase >> 2);
    *out++ += Mul((this_sample - (1 << 29)) << 1, gain.Next()) >> 1;   // Q30 -> Q29
  }
  phase_ = phase;
  next_sample_ = next_sample;
}

void FastSineOscillatorFixed::Render(uint32_t frequency, int32_t amplitude, int32_t* out,
                                     size_t size) {
  if (frequency >= (1u << 30)) {
    frequency = 1u << 30;
    amplitude = 0;
  } else {
    amplitude = Mul(amplitude, INT32_MAX - static_cast<int32_t>(frequency << 1));   // 1 - 4 f
  }
  // Fast2Sin(f) = f pi (2 - (2 * 0.96 / 6) (f pi)^2), Q30.
  // (2 f pi (1 - 0.16 (f pi)^2) in Q30 is f pi (1 - 0.16 (f pi)^2) in Q31.)
  const int32_t f_pi = Mul(static_cast<int32_t>(frequency >> 1), Q31(3.14159265358979 / 4.0)) << 2;   // f pi, Q31
  const int32_t f_pi_2 = Mul(f_pi, f_pi);                                                         // Q31
  const int32_t epsilon = Mul(f_pi, INT32_MAX - Mul(f_pi_2, Q31(0.16)));                          // Q30
  Interpolator epsilon_modulation(&epsilon_, epsilon, size);
  Interpolator am(&amplitude_, amplitude, size);
  int32_t x = x_;
  int32_t y = y_;
  // Renormalise when x^2 + y^2 leaves [0.5, 2] (upstream: an approximate
  // 1 / sqrt; here exact).
  const int32_t norm = Mul(x, x) + Mul(y, y);   // Q29
  if (norm <= Q(0.5, 29) || norm >= Q(2.0, 29)) {
    const int32_t scale = FFloat::ReciprocalSqrt(FFloat::FromQ(norm, 29)).ToQ(30);
    x = Mul(x, scale) << 1;
    y = Mul(y, scale) << 1;
  }
  while (size--) {
    const int32_t e = epsilon_modulation.Next();
    x += Mul(e, y) << 1;
    y -= Mul(e, x) << 1;
    *out++ += Mul(am.Next(), x) >> 1;   // Q30 -> Q29
  }
  x_ = x;
  y_ = y;
}

void SwarmVoiceFixed::Render(uint32_t f0, int32_t density, bool burst_mode, bool start_burst,
                             int32_t spread, int32_t size_ratio, int32_t* saw, int32_t* sine,
                             size_t size) {
  envelope_.Step(density, burst_mode, start_burst);
  const int32_t amplitude = envelope_.amplitude(size_ratio) >> 3;   // / kNumSwarmVoices
  const int32_t expo_amount = envelope_.frequency(size_ratio);       // Q30
  // f0 *= SemitonesToRatio(48 expo spread rank) * (1 + rank (rank + 0.01) spread / 4).
  const int32_t semis = Mul(Mul(Mul(expo_amount, spread), rank_), 48 << 18) << 1;   // Q29, Q28, Q15 -> Q16
  // rank (rank + 0.01) spread / 4: the Q28 product read as Q30 is the / 4.
  const int32_t linear_amount = Mul(Mul(rank_, rank_ + Q(0.01, 30)), spread);
  static constexpr FFloat kOne(1.0);
  const FFloat f = FFloatFromQ32(f0) * SemitonesToRatio(semis) *
                   (kOne + FFloat::FromQ(linear_amount, 30));
  const int32_t fq = f.ToQ(31);   // saturates at 1.0
  const uint32_t frequency = static_cast<uint32_t>(fq) << 1;
  saw_.Render(frequency, amplitude, saw, size);
  sine_.Render(frequency, amplitude, sine, size);
}

void SwarmEngineFixed::Reset() {
  // rank = (i - n) / n with n = (8 - 1) / 2 = 3 (integer division upstream).
  for (int i = 0; i < kNumSwarmVoices; ++i) {
    swarm_voice_[i].Init(Q(1.0 / 3.0, 30) * (i - 3));
  }
}

void SwarmEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                              size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  // density = NoteToFrequency(timbre * 120) * 0.025 * size (Q31).
  const uint32_t d = NoteToFrequency(parameters.timbre * 240);
  const int32_t density = Mul(static_cast<int32_t>(d >> 1), Q31(0.025)) * static_cast<int32_t>(size);
  // spread = harmonics^3 (Q30).
  const int32_t h = parameters.harmonics << 15;
  const int32_t spread = Mul(Mul(h, h) << 1, h) << 1;
  // size_ratio = 0.25 SemitonesToRatio((1 - morph) 84), Q24.
  int32_t size_ratio = SemitonesToRatio((kQ15One - parameters.morph) * 168).Scaled(-2).ToQ(24);
  const bool burst_mode = !(parameters.trigger & TRIGGER_UNPATCHED);
  const bool start_burst = parameters.trigger & TRIGGER_RISING_EDGE;
  for (size_t i = 0; i < size; ++i) out[i] = aux[i] = 0;   // Q29 accumulators
  for (int i = 0; i < kNumSwarmVoices; ++i) {
    swarm_voice_[i].Render(f0, density, burst_mode, start_burst, spread, size_ratio, out, aux, size);
    size_ratio = Mul(size_ratio, Q31(0.97));
  }
  for (size_t i = 0; i < size; ++i) {
    out[i] >>= (29 - kAudioBits);
    aux[i] >>= (29 - kAudioBits);
  }
}

}  // namespace op1plaits
