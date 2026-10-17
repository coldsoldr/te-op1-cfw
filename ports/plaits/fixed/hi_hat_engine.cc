#include "fixed/hi_hat_engine.h"

namespace op1plaits {

namespace {

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

}  // namespace

void SquareNoiseFixed::Render(uint32_t f0, int32_t* temp_1, int32_t* temp_2, int32_t* out,
                              size_t size) {
  (void)temp_1;
  (void)temp_2;
  // Nominal f0: 414 Hz.
  static const int32_t kRatios[6] = {
    Q(1.0, 28), Q(1.304, 28), Q(1.466, 28), Q(1.787, 28), Q(1.932, 28), Q(2.536, 28),
  };
  uint32_t increment[6];
  uint32_t phase[6];
  for (int i = 0; i < 6; ++i) {
    uint32_t f = static_cast<uint32_t>(ShlSat(Mul(static_cast<int32_t>(f0 >> 1), kRatios[i]), 4));
    if (f >= Q32(0.499)) f = Q32(0.499);
    increment[i] = f;
    phase[i] = phase_[i];
  }
  while (size--) {
    uint32_t noise = 0;
    for (int i = 0; i < 6; ++i) {
      phase[i] += increment[i];
      noise += phase[i] >> 31;
    }
    *out++ = static_cast<int32_t>(noise) * Q(0.33, 26) - (1 << 26);
  }
  for (int i = 0; i < 6; ++i) phase_[i] = phase[i];
}

void RingModNoiseFixed::Render(uint32_t f0, int32_t* temp_1, int32_t* temp_2, int32_t* out,
                               size_t size) {
  // ratio = f0 / (0.01 + f0); the pairs' frequencies scale with it.
  static constexpr FFloat kHundredth(0.01);
  const FFloat f = FFloatFromQ32(f0);
  const FFloat ratio = f / (kHundredth + f);
  static constexpr FFloat k1a(200.0 / kSampleRateD), k1b(7530.0 / kSampleRateD);
  static constexpr FFloat k2a(510.0 / kSampleRateD), k2b(8075.0 / kSampleRateD);
  static constexpr FFloat k3a(730.0 / kSampleRateD), k3b(10500.0 / kSampleRateD);
  const FFloat ks[6] = { k1a, k1b, k2a, k2b, k3a, k3b };
  for (size_t i = 0; i < size; ++i) out[i] = 0;
  for (int pair = 0; pair < 3; ++pair) {
    const uint32_t fa = static_cast<uint32_t>((ks[2 * pair] * ratio).ToQ(31)) << 1;
    const uint32_t fb = static_cast<uint32_t>((ks[2 * pair + 1] * ratio).ToQ(31)) << 1;
    oscillator_[2 * pair].Render<OSCILLATOR_SHAPE_SQUARE>(fa, 1u << 31, temp_1, size);
    oscillator_[2 * pair + 1].Render<OSCILLATOR_SHAPE_SAW>(fb, 1u << 31, temp_2, size);
    for (size_t i = 0; i < size; ++i) {
      out[i] += Mul(temp_1[i], temp_2[i]) >> 3;   // Q30 x Q30 -> Q29 -> Q26
    }
  }
}

template<typename MetallicNoiseSource, bool swing_vca, bool resonance, bool two_stage_envelope>
void HiHatFixed<MetallicNoiseSource, swing_vca, resonance, two_stage_envelope>::Render(
    bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t tone, int32_t decay,
    int32_t noisiness, int32_t* temp_1, int32_t* temp_2, int32_t* out, size_t size) {
  static constexpr FFloat k003(0.003), k0025(0.0025);
  const int32_t envelope_decay = INT32_MAX - (k003 * SemitonesToRatio(-decay * 168)).ToQ31();
  const int32_t cut_decay = INT32_MAX - (k0025 * SemitonesToRatio(-decay * 72)).ToQ31();

  if (trigger) {
    // (1.5 + 0.5 (1 - decay)) (0.3 + 0.7 accent), Q30.
    const int32_t a = Q(1.5, 30) + ((kQ15One - decay) << 14);
    envelope_ = Mul(a, Q31(0.3) + Mul(Q31(0.7), Q15ToQ31(accent)));
  }

  // Render the metallic noise.
  const uint32_t f2 = f0 >= (1u << 31) ? 0xffffffffu : f0 << 1;
  metallic_noise_.Render(f2, temp_1, temp_2, out, size);

  // Apply BPF on the metallic noise.
  static constexpr FFloat kCutoffBase(150.0 / kSampleRateD);
  static constexpr FFloat kCutoffMax(16000.0 / kSampleRateD);
  FFloat cutoff_ff = kCutoffBase * SemitonesToRatio(tone * 144);
  if (kCutoffMax < cutoff_ff) cutoff_ff = kCutoffMax;
  const uint32_t cutoff = static_cast<uint32_t>(cutoff_ff.ToQ(31)) << 1;
  noise_coloration_svf_.set_f_q<FREQUENCY_ACCURATE>(
      cutoff, resonance ? (3 << 16) + tone * 6 : (1 << 16));
  noise_coloration_svf_.Process<FILTER_MODE_BAND_PASS>(out, out, size);

  // A variable amount of clocked noise (not part of the 808 circuit).
  const int32_t n2 = Q15ToQ31(MulQ15(noisiness, noisiness));
  // noise_f = clamp(f0 (16 + 16 (1 - n2)), 0, 0.5): f0 (32 - 16 n2).
  const int32_t mult = (32 << 24) - (n2 >> 3);   // Q24 (16 n2 in Q24 = n2_q31 >> 3)
  int64_t nf = (static_cast<int64_t>(f0) * mult) >> 24;
  const uint32_t noise_f = nf >= (1LL << 31) ? (1u << 31) : static_cast<uint32_t>(nf);
  for (size_t i = 0; i < size; ++i) {
    const uint32_t old = noise_clock_;
    noise_clock_ += noise_f;
    if (noise_clock_ < old) {
      noise_sample_ = static_cast<int32_t>(Random::GetWord() - 0x80000000u) >> 6;   // u - 0.5, Q26
    }
    out[i] += Mul(noise_sample_ - out[i], n2);
  }

  // Apply VCA.
  InterpolatorReached sustain_gain(&sustain_gain_, Q15ToQ31(MulQ15(accent, decay)), size);
  for (size_t i = 0; i < size; ++i) {
    envelope_ = MulRound(envelope_,
                         envelope_ > (1 << 29) || !two_stage_envelope ? envelope_decay : cut_decay);
    const int32_t gain = sustain ? (sustain_gain.Next() >> 1) : envelope_;   // Q30
    const int32_t s = out[i];
    if (swing_vca) {
      // s *= s > 0 ? 4 : 0.1; s / (1 + |s|); (s + 0.1) gain.
      const int32_t x = s > 0 ? s : Mul(s, Q31(0.1)) >> 2;   // Q24 (4 s in Q24 is s in Q26)
      const int32_t r = SoftRatio(x);                         // Q31
      out[i] = Mul((r >> 1) + Q(0.1, 30), gain) >> 3;         // Q29 -> Q26
    } else {
      out[i] = Mul(s, gain) << 1;                             // Q25 -> Q26
    }
  }

  hpf_.set_f_q<FREQUENCY_ACCURATE>(cutoff, 1 << 15);
  hpf_.Process<FILTER_MODE_HIGH_PASS>(out, out, size);
}

void HiHatEngineFixed::Init(Allocator* allocator) {
  hi_hat_1_.Init();
  hi_hat_2_.Init();
  temp_buffer_ = allocator->Allocate<int32_t>(kMaxBlockSize * 2);
}

void HiHatEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                              size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;
  hi_hat_1_.Render(sustain, trigger, parameters.accent, f0, parameters.timbre, parameters.morph,
                   parameters.harmonics, temp_buffer_, temp_buffer_ + size, out, size);
  hi_hat_2_.Render(sustain, trigger, parameters.accent, f0, parameters.timbre, parameters.morph,
                   parameters.harmonics, temp_buffer_, temp_buffer_ + size, aux, size);
}

}  // namespace op1plaits
