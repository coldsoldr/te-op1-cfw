#include "fixed/modal_engine.h"

#include "fixed/dsp.h"
#include "fixed/particle_engine.h"   // CosineOscillatorFixed

namespace op1plaits {

namespace {

// OnePole::tan<FREQUENCY_FAST>, per-mode version without 64-bit arithmetic.
// f Q32 (< 0.5), Q27 out.
inline int32_t TanFastFast(uint32_t f) {
  const int32_t x = static_cast<int32_t>(f >> 1);   // Q31
  const int32_t u = Mul(x, x);                      // Q31
  // pi + u (a + b u), a = 0.326 pi^3 (~10.1), b = 0.1823 pi^5 (~55.8): Q24.
  int32_t t = Q(1.823e-01 * 306.0196847852814, 24);
  t = Q(3.260e-01 * 31.00627668029982, 24) + Mul(u, t);
  t = Q(3.14159265358979, 24) + Mul(u, t);
  // g = f t: Mul(f_q32, t_q24) = Q25; << 2 -> Q27.
  return ShlSat(Mul(static_cast<int32_t>(f), t), 2);
}

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

}  // namespace

void ResonatorFixed::Init(int32_t position_q31, int resolution) {
  resolution_ = resolution > kMaxNumModes ? kMaxNumModes : resolution;
  CosineOscillatorFixed amplitudes;
  amplitudes.Init(static_cast<uint32_t>(position_q31) << 1);
  for (int i = 0; i < kMaxNumModes; ++i) {
    mode_amplitude_[i] = i < resolution ? amplitudes.Next() >> 2 : 0;   // * 0.25
    state_1_[i] = state_2_[i] = 0;
  }
}

void ResonatorFixed::Process(uint32_t f0, int32_t structure, int32_t brightness, int32_t damping,
                             const int32_t* in, int32_t* out, size_t size) {
  // stiffness = Interpolate(lut_stiffness, structure, 64) (Q29).
  const int32_t si = structure >> 25;
  const int32_t sf = (structure & 0x1ffffff) << 6;
  int32_t stiffness = lut_stiffness_q29[si] + MulQ31(lut_stiffness_q29[si + 1] - lut_stiffness_q29[si], sf);
  // f0 *= NthHarmonicCompensation(3, stiffness) = 1 / (1 + s + s k).
  {
    int32_t s = stiffness;
    int32_t stretch = 1 << 25;   // Q25 (the stretch reaches ~40 over 24 modes)
    for (int i = 0; i < 2; ++i) {
      stretch += s >> 4;
      s = MulQ31(s, s < 0 ? Q31(0.93) : Q31(0.98));
    }
    const FFloat c = FFloat::Reciprocal(FFloat::FromQ(stretch, 25));
    f0 = static_cast<uint32_t>((FFloatFromQ32(f0) * c).ToQ(31)) << 1;
  }
  // q = 500 SemitonesToRatio(damping 79.7)^2.
  const int32_t semis = Mul64(damping, Q(79.7, 16), 31);
  const FFloat q_sqrt = SemitonesToRatio(semis);
  static constexpr FFloat k500(500.0);
  FFloat q = k500 * q_sqrt * q_sqrt;
  // brightness *= (1 - 0.3 structure) (1 - 0.3 damping); q_loss = b (2 - b)
  // 0.85 + 0.15.
  brightness = MulQ31(brightness, INT32_MAX - MulQ31(structure, Q31(0.3)));
  brightness = MulQ31(brightness, INT32_MAX - MulQ31(damping, Q31(0.3)));
  const int32_t b2 = MulQ31(brightness, INT32_MAX - brightness) + brightness;   // b (2 - b)
  const FFloat q_loss = FFloat::FromQ(MulQ31(b2, Q31(0.85)) + Q31(0.15), 31);

  int32_t g[kMaxNumModes], g_shift[kMaxNumModes], r[kMaxNumModes], h[kMaxNumModes],
      a[kMaxNumModes];
  uint64_t harmonic = f0;   // grows past 1 (the modes clamp at 0.499)
  int32_t stretch_factor = 1 << 25;   // Q25
  static constexpr FFloat kOne(1.0);
  const int n = resolution_;
  for (int i = 0; i < n; ++i) {
    // mode_frequency = min(harmonic * stretch_factor, 0.499).
    const uint64_t mf64 = harmonic > 0xffffffffull ? 0xffffffffull
        : (harmonic * static_cast<uint32_t>(stretch_factor)) >> 25;
    const uint32_t mf = mf64 > Q32(0.499) ? Q32(0.499) : static_cast<uint32_t>(mf64);
    // mode_attenuation = 1 - 2 mf; mode_a = amplitude * attenuation.
    a[i] = Mul(mode_amplitude_[i], INT32_MAX - static_cast<int32_t>(mf));
    // mode_q = 1 + mf q; r = 1 / mode_q (Q31, <= 1).
    const FFloat mq = kOne + FFloatFromQ32(mf) * q;
    r[i] = FFloat::Reciprocal(mq).ToQ31();
    const int32_t gq = TanFastFast(mf);
    g_shift[i] = ShiftFor(gq);
    g[i] = gq << (4 - g_shift[i]);
    h[i] = SvfHFast(gq, r[i] >> 2);

    stretch_factor += stiffness >> 4;   // Q29 -> Q25
    stiffness = MulQ31(stiffness, stiffness < 0 ? Q31(0.93) : Q31(0.98));
    harmonic += f0;
    q = q * q_loss;
  }

  for (size_t j = 0; j < size; ++j) {
    const int32_t s_in = in[j];
    int32_t s_out = 0;
    for (int i = 0; i < n; ++i) {
      const int32_t s1 = state_1_[i];
      const int32_t s2 = state_2_[i];
      const int32_t gi = g[i];
      const int32_t sh = g_shift[i];
      const int32_t x = s_in - MulRound(r[i], s1) - (MulRound(s1, gi) << sh) - s2;
      const int32_t hp = MulRound(x, h[i]);
      const int32_t v1 = MulRound(hp, gi) << sh;
      const int32_t bp = v1 + s1;
      state_1_[i] = bp + v1;
      const int32_t v2 = MulRound(bp, gi) << sh;
      state_2_[i] = (v2 + s2) + v2;
      s_out += Mul(bp, a[i]);
    }
    out[j] += s_out;
  }
}

void ModalVoiceFixed::Init() {
  excitation_state_1_ = excitation_state_2_ = 0;
  resonator_.Init(Q31(0.015), kMaxNumModes);
}

void ModalVoiceFixed::Render(bool sustain, bool trigger, int32_t accent, uint32_t f0,
                             int32_t structure, int32_t brightness, int32_t damping,
                             int32_t* temp, int32_t* out, int32_t* aux, size_t size) {
  const int32_t density = MulQ31(brightness, brightness);
  brightness += MulQ31(Mul(accent, Q31(0.25)), INT32_MAX - brightness);
  damping += MulQ31(Mul(accent, Q31(0.25)), INT32_MAX - damping);

  // cutoff = min(f SemitonesToRatio((b (2 - b) - 0.5) range), 0.499).
  const int32_t b2 = MulQ31(brightness, INT32_MAX - brightness) + brightness;
  const int32_t semis = Mul64(b2 - Q31(0.5), (sustain ? 36 : 60) << 16, 31);
  static constexpr FFloat kMaxF(0.499);
  FFloat cutoff = FFloatFromQ32(f0).Scaled(sustain ? 2 : 1) * SemitonesToRatio(semis);
  if (kMaxF < cutoff) cutoff = kMaxF;
  const uint32_t cutoff_q32 = static_cast<uint32_t>(cutoff.ToQ(31)) << 1;
  // The excitation filter: ResonatorSvf<1>, FREQUENCY_FAST, low-pass.
  const int32_t gq = TanFastFast(cutoff_q32);
  const int32_t sh = ShiftFor(gq);
  const int32_t gs = gq << (4 - sh);
  const int32_t r = ResonanceToR(sustain ? Q(0.7, 16) : Q(1.5, 16));   // Q29
  const int32_t h = SvfHFast(gq, r);

  // The excitation: dust, or an impulse on the trigger, applied to the
  // filter through its linear contribution (it reaches ~100).
  int32_t impulse_gh = 0;    // g h x, Q24
  int32_t impulse_gh2 = 0;   // g^2 h x
  if (sustain) {
    const int32_t dust_f = Q31(0.00005) + Mul(MulQ31(density, density), Q31(0.99995));
    const int32_t k = (4 << 27) - 3 * (dust_f >> 4);   // 4 - 3 dust_f, Q27
    for (size_t i = 0; i < size; ++i) {
      const uint32_t u = Random::GetWord() >> 1;
      const int32_t dust = u < static_cast<uint32_t>(dust_f) ? Fraction(u, static_cast<uint32_t>(dust_f)) : 0;
      temp[i] = Mul(Mul(dust, k), accent) >> 3;   // Q24
    }
  } else {
    for (size_t i = 0; i < size; ++i) temp[i] = 0;
    if (trigger) {
      // amplitude = (0.12 + 0.08 accent) (1 - 0.5 damping);
      // x = amplitude SemitonesToRatio(24 cutoff^2) / cutoff.
      const int32_t amplitude = Mul(Q31(0.12) + Mul(Q31(0.08), accent), INT32_MAX - (damping >> 1));
      const FFloat c2 = cutoff * cutoff;
      static constexpr FFloat kOneAndHalf(1.5);
      const int32_t s24 = (c2.Scaled(4) * kOneAndHalf).ToQ(16);   // 24 c^2 semitones, Q16
      const FFloat x = FFloat::FromQ(amplitude, 31) * SemitonesToRatio(s24) / cutoff;
      // g^2 h x in Q24 (the low-pass output's share of the impulse).
      const FFloat gf = FFloat::FromQ(gq, 27);
      impulse_gh2 = (gf * gf * FFloat::FromQ(h, 31) * x).ToQ(24);
      impulse_gh = (gf * FFloat::FromQ(h, 31) * x).ToQ(24);
    }
  }
  int32_t s1 = excitation_state_1_;
  int32_t s2 = excitation_state_2_;
  for (size_t i = 0; i < size; ++i) {
    const int32_t x = temp[i] - (MulRound(r, s1) << 2) - (MulRound(s1, gs) << sh) - s2;
    const int32_t hp = MulRound(x, h);
    const int32_t v1 = MulRound(hp, gs) << sh;
    const int32_t bp = v1 + s1;
    s1 = bp + v1;
    const int32_t v2 = MulRound(bp, gs) << sh;
    const int32_t lp = v2 + s2;
    s2 = lp + v2;
    if (i == 0 && (impulse_gh | impulse_gh2)) {
      // The impulse x at sample 0: its share of this output and the states.
      s1 += impulse_gh << 1;
      s2 += impulse_gh2 << 1;
      temp[i] = lp + impulse_gh2;
    } else {
      temp[i] = lp;
    }
  }
  excitation_state_1_ = s1;
  excitation_state_2_ = s2;
  for (size_t i = 0; i < size; ++i) aux[i] += temp[i];
  resonator_.Process(f0, structure, brightness, damping, temp, out, size);
}

void ModalEngineFixed::Init(Allocator* allocator) {
  temp_buffer_ = allocator->Allocate<int32_t>(kMaxBlockSize);
  harmonics_lp_ = 0;
  Reset();
}

void ModalEngineFixed::Reset() { voice_.Init(); }

void ModalEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                              size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  for (size_t i = 0; i < size; ++i) out[i] = aux[i] = 0;   // Q24
  OnePole(harmonics_lp_, Q15ToQ31(parameters.harmonics), Q31(0.01));
  voice_.Render(parameters.trigger & TRIGGER_UNPATCHED, parameters.trigger & TRIGGER_RISING_EDGE,
                Q15ToQ31(parameters.accent), NoteToFrequency(parameters.note), harmonics_lp_,
                Q15ToQ31(parameters.timbre), Q15ToQ31(parameters.morph), temp_buffer_, out, aux, size);
  for (size_t i = 0; i < size; ++i) {
    out[i] = ShlSat(out[i], kAudioBits - 24);
    aux[i] = ShlSat(aux[i], kAudioBits - 24);
  }
}

}  // namespace op1plaits
