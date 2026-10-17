#include "fixed/snare_drum_engine.h"

namespace op1plaits {

namespace {

constexpr int32_t kTriggerPulseDuration = static_cast<int32_t>(1.0e-3 * kSampleRateD);
constexpr double kPulseDecayTime = 0.1e-3 * kSampleRateD;

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

// decay * (1 + decay * (decay - 1)), Q15.
inline int32_t DecayXt(int32_t decay) {
  return MulQ15(decay, kQ15One + MulQ15(decay, decay - kQ15One));
}

// snappy * 1.1 - 0.05, clamped to [0, 1], Q31.
inline int32_t SnappyShape(int32_t snappy) {
  const int32_t s = Mul64(snappy, Q(1.1, 16), 15) - Q(0.05, 16);   // Q16
  if (s <= 0) return 0;
  if (s >= 65536) return INT32_MAX;
  return s << 15;
}

const int32_t kModeFrequencies[AnalogSnareDrumFixed::kNumModes] = {
  Q(1.00, 28), Q(2.00, 28), Q(3.18, 28), Q(4.16, 28), Q(5.62, 28),
};

}  // namespace

void AnalogSnareDrumFixed::Init() {
  pulse_remaining_samples_ = 0;
  pulse_ = 0;
  pulse_height_ = 0;
  pulse_lp_ = 0;
  noise_envelope_ = 0;
  sustain_gain_ = 0;
  for (int i = 0; i < kNumModes; ++i) {
    resonator_[i].Init();
    oscillator_[i].Init();
  }
  noise_filter_.Init();
}

void AnalogSnareDrumFixed::Render(bool sustain, bool trigger, int32_t accent, uint32_t f0,
                                  int32_t tone, int32_t decay, int32_t snappy, int32_t* out,
                                  size_t size) {
  const int32_t decay_xt = DecayXt(decay);
  // q = 2000 SemitonesToRatio(decay_xt * 84).
  static constexpr FFloat k2000(2000.0);
  const FFloat q = k2000 * SemitonesToRatio(decay_xt * 168);
  // noise_envelope_decay = 1 - 0.0017 SemitonesToRatio(-decay (50 + 10 snappy)).
  const int32_t semis = -Mul64(decay, (50 << 16) + snappy * 20, 15);   // Q16
  static constexpr FFloat k0017(0.0017);
  const int32_t noise_envelope_decay = INT32_MAX - (k0017 * SemitonesToRatio(semis)).ToQ31();
  // exciter_leak = snappy (2 - snappy) 0.1.
  const int32_t exciter_leak = Mul(Q15ToQ31(MulQ15(snappy, 2 * kQ15One - snappy) >> 1) << 0, Q31(0.2));
  const int32_t snappy_shaped = SnappyShape(snappy);

  if (trigger) {
    pulse_remaining_samples_ = kTriggerPulseDuration;
    pulse_height_ = (3 << 24) + 7 * (accent << 9);
    noise_envelope_ = 2 << 29;
  }

  uint32_t f[kNumModes];
  int32_t gain[kNumModes];   // Q28
  for (int i = 0; i < kNumModes; ++i) {
    // min(f0 k, 0.499).
    const uint32_t fk = static_cast<uint32_t>(ShlSat(Mul(static_cast<int32_t>(f0 >> 1), kModeFrequencies[i]), 4));
    f[i] = fk > Q32(0.499) ? Q32(0.499) : fk;
    // resonance 1 + f (i == 0 ? q : q / 4), Q16.
    FFloat res = FFloatFromQ32(f[i]) * (i == 0 ? q : q.Scaled(-2));
    int32_t res_q16 = res.ToQ(16);
    res_q16 = (res_q16 > INT32_MAX - (1 << 16) ? INT32_MAX - (1 << 16) : res_q16) + (1 << 16);
    resonator_[i].set_f_q<FREQUENCY_FAST>(f[i], res_q16);
  }
  if (tone < Q(0.666667, 15)) {
    // 808-style (2 modes).
    const int32_t t = Clip((tone * 3) >> 1, 0, kQ15One);   // tone * 1.5, Q15
    const int32_t u = kQ15One - t;
    gain[0] = Q(1.5, 28) + Mul64(MulQ15(u, u), Q(4.5, 28), 15);
    gain[1] = Mul64(t, Q(2.0, 28), 15) + Q(0.15, 28);
    gain[2] = gain[3] = gain[4] = 0;
  } else {
    // What the 808 could have been if there were extra modes!
    int32_t t = Clip(Mul64(tone - Q(0.666667, 15), Q(3.0, 16), 16), 0, kQ15One);   // Q15
    gain[0] = Q(1.5, 28) - Mul64(t, Q(0.5, 28), 15);
    gain[1] = Q(2.15, 28) - Mul64(t, Q(0.7, 28), 15);
    for (int i = 2; i < kNumModes; ++i) {
      gain[i] = t << 13;   // Q15 -> Q28
      t = MulQ15(t, t);
    }
  }
  uint32_t f_noise = f0 >= (1u << 28) ? Q32(0.499) : f0 << 4;   // f0 * 16
  if (f_noise > Q32(0.499)) f_noise = Q32(0.499);
  // resonance 1 + 1.5 f_noise, Q16.
  noise_filter_.set_f_q<FREQUENCY_FAST>(f_noise, (1 << 16) + (Mul(static_cast<int32_t>(f_noise >> 1), Q(1.5, 30)) >> 14));

  Interpolator sustain_gain(&sustain_gain_, Q15ToQ31(MulQ15(accent, decay)), size);

  while (size--) {
    // Q45 / Q46
    int32_t pulse = 0;
    if (pulse_remaining_samples_) {
      --pulse_remaining_samples_;
      pulse = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - (1 << 24);
      pulse_ = pulse;
    } else {
      pulse_ = MulRound(pulse_, Q31(1.0 - 1.0 / kPulseDecayTime));
      pulse = pulse_;
    }
    const int32_t sustain_gain_value = sustain_gain.Next();
    // R189 / C57 / R190 + C58 / C59 / R197 / R196 / IC14
    pulse_lp_ += MulRound(pulse - pulse_lp_, Q31(0.75));

    int32_t shell = 0;   // Q25
    for (int i = 0; i < kNumModes; ++i) {
      if (!gain[i]) {
        // A mode at zero gain still runs upstream (its state rings on).
        if (!sustain) {
          const int32_t excitation = i == 0
              ? (pulse - pulse_lp_) + Mul(pulse, Q31(0.006)) : Mul(pulse, Q31(0.026));
          resonator_[i].Process<FILTER_MODE_BAND_PASS>(excitation << 1);
        } else {
          oscillator_[i].Next(f[i]);
        }
        continue;
      }
      int32_t v;   // Q25
      if (sustain) {
        v = Mul(Mul(oscillator_[i].Next(f[i]), sustain_gain_value), 1 << 29) >> 6;
      } else {
        const int32_t excitation = i == 0
            ? (pulse - pulse_lp_) + Mul(pulse, Q31(0.006)) : Mul(pulse, Q31(0.026));   // Q24
        v = resonator_[i].Process<FILTER_MODE_BAND_PASS>(excitation << 1) +
            (Mul(excitation, exciter_leak) << 1);
      }
      shell += MulRound(v, gain[i]) << 3;   // Q25 x Q28 -> Q22 -> Q25
    }
    const int32_t shell_clipped = SoftClip(ShlSat(shell, 1));   // Q31

    // C56 / R194 / Q48 / C54 / R188 / D54
    int32_t noise = static_cast<int32_t>(Random::GetWord() ^ 0x80000000u);   // 2 u - 1, Q31
    if (noise < 0) noise = 0;
    noise_envelope_ = MulRound(noise_envelope_, noise_envelope_decay);
    // noise * (sustain ? gain : envelope) * snappy * 2, Q25.
    const int32_t env = sustain ? (sustain_gain_value >> 2) : noise_envelope_;   // Q29
    noise = Mul(Mul(noise, env), snappy_shaped) >> 3;                            // Q29 x 2 -> Q25
    // C66 / R201 / C67 / R202 / R203 / Q49
    noise = noise_filter_.Process<FILTER_MODE_BAND_PASS>(noise);
    // IC13
    const int32_t o = noise + (Mul(shell_clipped, INT32_MAX - snappy_shaped) >> 6);   // Q25
    *out++ = ShlSat(o, kAudioBits - 25);
  }
}

void SyntheticSnareDrumFixed::Init() {
  phase_[0] = phase_[1] = 0;
  drum_amplitude_ = 0;
  snare_amplitude_ = 0;
  fm_ = 0;
  hold_counter_ = 0;
  sustain_gain_ = 0;
  drum_lp_.Init();
  snare_hp_.Init();
  snare_lp_.Init();
}

namespace {

// DistortedSine: triangle (p < 0.5 ? p : 1 - p) * 4 - 1.3, then 2 t / (1 +
// |t|). Phase Q30 (signed), Q30 out.
inline int32_t DistortedSine909(int32_t phase) {
  const int32_t tri = (phase < (1 << 29) ? phase : (1 << 30) - phase) - Q(1.3, 28);   // Q28
  return SoftRatio(tri >> 4);   // t / (1 + |t|) in Q31 = 2 t / (1 + |t|) in Q30
}

}  // namespace

void SyntheticSnareDrumFixed::Render(bool sustain, bool trigger, int32_t accent, uint32_t f0,
                                     int32_t fm_amount, int32_t decay, int32_t snappy,
                                     int32_t* out, size_t size) {
  const int32_t decay_xt = DecayXt(decay);
  fm_amount = MulQ15(fm_amount, fm_amount);
  // drum_decay = 1 - 1 / (0.015 SR) SemitonesToRatio(-72 decay_xt - 12 fm + 7 snappy).
  const int32_t drum_semis = -decay_xt * 144 - fm_amount * 24 + snappy * 14;   // Q16
  static constexpr FFloat kDrumRate(1.0 / (0.015 * kSampleRateD));
  const int32_t drum_decay = INT32_MAX - (kDrumRate * SemitonesToRatio(drum_semis)).ToQ31();
  const int32_t snare_semis = -decay * 120 - snappy * 14;
  static constexpr FFloat kSnareRate(1.0 / (0.01 * kSampleRateD));
  const int32_t snare_decay = INT32_MAX - (kSnareRate * SemitonesToRatio(snare_semis)).ToQ31();
  const int32_t fm_decay = Q31(1.0 - 1.0 / (0.007 * kSampleRateD));

  const int32_t snappy_shaped = SnappyShape(snappy);
  const FFloat sn = FFloat::FromQ(snappy_shaped, 31);
  static constexpr FFloat kOne(1.0);
  const int32_t drum_level = snappy_shaped == INT32_MAX ? 0 : FFloat::Sqrt(kOne - sn).ToQ31();
  const int32_t snare_level = FFloat::Sqrt(sn).ToQ31();

  // snare_f_min = min(10 f0, 0.5), snare_f_max = min(35 f0, 0.5).
  const uint64_t f10 = static_cast<uint64_t>(f0) * 10;
  const uint64_t f35 = static_cast<uint64_t>(f0) * 35;
  const uint32_t snare_f_min = f10 > (1u << 31) ? (1u << 31) - 1 : static_cast<uint32_t>(f10);
  const uint32_t snare_f_max = f35 > (1u << 31) ? (1u << 31) - 1 : static_cast<uint32_t>(f35);
  snare_hp_.set_f<FREQUENCY_FAST>(snare_f_min);
  snare_lp_.set_f_q<FREQUENCY_FAST>(snare_f_max, (1 << 15) + (snappy_shaped >> 14));   // 0.5 + 2 snappy
  const uint64_t f3 = static_cast<uint64_t>(f0) * 3;
  drum_lp_.set_f<FREQUENCY_FAST>(f3 > (1u << 31) - 1 ? (1u << 31) - 1 : static_cast<uint32_t>(f3));

  if (trigger) {
    snare_amplitude_ = drum_amplitude_ = Q31(0.3) + Mul(Q31(0.7), Q15ToQ31(accent));
    fm_ = INT32_MAX;
    phase_[0] = phase_[1] = 0;
    hold_counter_ = static_cast<int32_t>(
        (Q(0.04 * kSampleRateD, 8) + Mul64(decay, Q(0.03 * kSampleRateD, 8), 15)) >> 8);
  }

  InterpolatorReached sustain_gain(&sustain_gain_, Q15ToQ31(MulQ15(accent, decay)), size);

  // reset_noise_amount = clamp((0.125 - f0) 8, 0, 1)^2 fm_amount, Q31.
  const int64_t rn = (1LL << 31) - (static_cast<int64_t>(f0) << 2);   // 1 - 8 f0
  int32_t reset_noise_amount = rn <= 0 ? 0 : (rn >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(rn));
  reset_noise_amount = Mul(Mul(reset_noise_amount, reset_noise_amount), Q15ToQ31(fm_amount));
  const int32_t fm_amount_4 = Q15ToQ31(fm_amount);   // x 4, applied in Q28 below

  while (size--) {
    if (sustain) {
      snare_amplitude_ = sustain_gain.Next();
      drum_amplitude_ = snare_amplitude_;
      fm_ = 0;
    } else {
      // The drum's envelope has a long tail; the snare's has a hold stage
      // of 40 to 70 ms.
      if (drum_amplitude_ > Q31(0.03) || !(size & 1)) {
        drum_amplitude_ = MulRound(drum_amplitude_, drum_decay);
      }
      if (hold_counter_) {
        --hold_counter_;
      } else {
        snare_amplitude_ = MulRound(snare_amplitude_, snare_decay);
      }
      fm_ = MulRound(fm_, fm_decay);
    }
    // Oscillator coupling through the reset noise (Q30).
    int32_t reset_noise = 0;
    reset_noise += phase_[0] > (1 << 29) ? -(1 << 30) : (1 << 30);
    reset_noise += phase_[1] > (1 << 29) ? -(1 << 30) : (1 << 30);
    reset_noise = Mul(Mul(reset_noise, reset_noise_amount), Q31(0.025));
    // f = f0 (1 + 4 fm_amount fm), Q30 increments.
    const int32_t mult = (1 << 28) + (Mul(fm_amount_4, fm_) >> 1);   // Q28 (4 x in Q28 = x in Q30... )
    const int32_t inc = ShlSat(Mul(static_cast<int32_t>(f0 >> 1), mult), 2);   // Q30
    phase_[0] += inc;
    phase_[1] += Mul(inc, Q(1.47 / 2.0, 31)) << 1;
    if (reset_noise_amount > Q31(0.1)) {
      if (phase_[0] >= (1 << 30) + reset_noise) phase_[0] = (1 << 30) - phase_[0];
      if (phase_[1] >= (1 << 30) + reset_noise) phase_[1] = (1 << 30) - phase_[1];
    } else {
      if (phase_[0] >= (1 << 30)) phase_[0] -= 1 << 30;
      if (phase_[1] >= (1 << 30)) phase_[1] -= 1 << 30;
    }

    int32_t drum = -Q(0.1, 30);
    drum += Mul(DistortedSine909(phase_[0]), Q31(0.60));
    drum += Mul(DistortedSine909(phase_[1]), Q31(0.25));
    drum = Mul(Mul(drum, drum_amplitude_), drum_level);
    drum = drum_lp_.Process<FILTER_MODE_LOW_PASS>(drum);   // Q30

    // The noise filters run in Q26 (near Nyquist, g ~ 4.6, their states
    // reach well above 2).
    const int32_t noise = static_cast<int32_t>(Random::GetWord() >> 6);   // Q26
    int32_t snare = snare_lp_.Process<FILTER_MODE_LOW_PASS>(noise);
    snare = snare_hp_.Process<FILTER_MODE_HIGH_PASS>(snare);
    // (snare + 0.1) (snare_amplitude + fm) snare_level, Q29.
    const int32_t amp = (snare_amplitude_ >> 1) + (fm_ >> 1);   // Q30
    snare = Mul(Mul(snare + Q(0.1, 26), amp), snare_level) << 4;
    *out++ = ((snare + (drum >> 1)) >> (29 - kAudioBits));
  }
}

void SnareDrumEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  analog_snare_drum_.Init();
  synthetic_snare_drum_.Init();
}

void SnareDrumEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                                  size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;
  analog_snare_drum_.Render(sustain, trigger, parameters.accent, f0, parameters.timbre,
                            parameters.morph, parameters.harmonics, out, size);
  synthetic_snare_drum_.Render(sustain, trigger, parameters.accent, f0, parameters.timbre,
                               parameters.morph, parameters.harmonics, aux, size);
}

}  // namespace op1plaits
