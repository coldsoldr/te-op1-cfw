#include "fixed/bass_drum_engine.h"

#include "fixed/dsp.h"

namespace op1plaits {

namespace {

// Durations and time constants in samples (upstream: float * kSampleRate).
constexpr int32_t kTriggerPulseDuration = static_cast<int32_t>(1.0e-3 * kSampleRateD);
constexpr int32_t kFMPulseDuration = static_cast<int32_t>(6.0e-3 * kSampleRateD);
constexpr double kPulseDecayTime = 0.2e-3 * kSampleRateD;
constexpr double kPulseFilterTime = 0.1e-3 * kSampleRateD;
constexpr double kRetrigPulseDuration = 0.05 * kSampleRateD;

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

// Diode(x): x >= 0 ? x : 0.7 (2 x) / (1 + |2 x|). Q24 in and out.
inline int32_t Diode(int32_t x) {
  if (x >= 0) return x;
  return Mul(SoftRatio(x << 1), Q31(0.7)) >> 7;
}

// A FFloat as mantissa and a shift such that x * value = Mul(x, m) << shift
// (or >> -shift) gives the product in x's format.
struct Scaled {
  int32_t m;
  int32_t shift;
};

inline int32_t ApplyScaled(int32_t x, const Scaled& s) {
  const int32_t p = MulRound(x, s.m);
  return s.shift >= 0 ? ShlSat(p, s.shift) : p >> (-s.shift);
}

}  // namespace

void AnalogBassDrumFixed::Init() {
  pulse_remaining_samples_ = 0;
  fm_pulse_remaining_samples_ = 0;
  pulse_ = 0;
  pulse_height_ = 0;
  pulse_lp_ = 0;
  fm_pulse_lp_ = 0;
  retrig_pulse_ = 0;
  lp_out_ = 0;
  tone_lp_ = 0;
  sustain_gain_ = 0;
  resonator_.Init();
  oscillator_.Init();
}

void AnalogBassDrumFixed::Render(bool sustain, bool trigger, int32_t accent, uint32_t f0,
                                 int32_t tone, int32_t decay, int32_t attack_fm_amount,
                                 int32_t self_fm_amount, int32_t* out, size_t size) {
  // scale = 0.001 / f0, Q27 (saturates at 16).
  static constexpr FFloat kMilli(0.001);
  const int32_t scale = (kMilli / FFloatFromQ32(f0)).ToQ(27);
  // q = 1500 * SemitonesToRatio(decay * 80): resonance = 1 + q f per sample,
  // with q f = Mul(q_m, f) * 2^(q_e + 31 - 32) in Q16 units -> shift q_e + 15.
  static constexpr FFloat k1500(1500.0);
  const FFloat q = k1500 * SemitonesToRatio(decay * 160);
  const int32_t q_m = q.mantissa();
  const int32_t q_shift = q.exponent() + 15;
  // tone_f = min(4 f0 SemitonesToRatio(tone * 108), 1), Q31.
  const FFloat tone_ff = SemitonesToRatio(tone * 216) * FFloatFromQ32(f0).Scaled(2);
  static constexpr FFloat kOne(1.0);
  const int32_t tone_f = tone_ff < kOne ? tone_ff.ToQ31() : INT32_MAX;
  // exciter_leak = 0.08 (tone + 0.25), Q31.
  const int32_t exciter_leak = Mul((tone << 15) + Q(0.25, 30), Q31(0.08)) << 1;

  if (trigger) {
    pulse_remaining_samples_ = kTriggerPulseDuration;
    fm_pulse_remaining_samples_ = kFMPulseDuration;
    pulse_height_ = (3 << 24) + 7 * (accent << 9);   // 3 + 7 accent, Q24
    lp_out_ = 0;
  }

  InterpolatorReached sustain_gain(&sustain_gain_, Q15ToQ31(MulQ15(accent, decay)), size);
  const int32_t attack_fm = Q15ToQ31(attack_fm_amount);
  const int32_t self_fm = Q15ToQ31(self_fm_amount);

  while (size--) {
    // Q39 / Q40
    int32_t pulse = 0;
    if (pulse_remaining_samples_) {
      --pulse_remaining_samples_;
      pulse = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - (1 << 24);
      pulse_ = pulse;
    } else {
      pulse_ = MulRound(pulse_, Q31(1.0 - 1.0 / kPulseDecayTime));
      pulse = pulse_;
    }
    if (sustain) pulse = 0;

    // C40 / R163 / R162 / D83
    pulse_lp_ += MulRound(pulse - pulse_lp_, Q31(1.0 / kPulseFilterTime));
    pulse = Diode((pulse - pulse_lp_) + Mul(pulse, Q31(0.044)));

    // Q41 / Q42
    int32_t fm_pulse = 0;
    if (fm_pulse_remaining_samples_) {
      --fm_pulse_remaining_samples_;
      fm_pulse = INT32_MAX;
      // C39 / C52
      retrig_pulse_ = fm_pulse_remaining_samples_ ? 0 : -Q31(0.8);
    } else {
      // C39 / R161
      retrig_pulse_ = MulRound(retrig_pulse_, Q31(1.0 - 1.0 / kRetrigPulseDuration));
    }
    if (sustain) fm_pulse = 0;
    fm_pulse_lp_ += MulRound(fm_pulse - fm_pulse_lp_, Q31(1.0 / kPulseFilterTime));

    // Q43 and R170 leakage
    const int32_t punch = Q(0.7, 24) + Diode(((lp_out_ >> 1) * 10) - (1 << 24));   // Q24
    // Q43 / R165: f = f0 (1 + attack_fm + self_fm), clamped to [0, 0.4].
    const int32_t a = Mul(Mul(fm_pulse_lp_, Q31(1.7 / 2.0)), attack_fm) >> 2;   // 1.7 x, Q28
    const int32_t s = Mul(Mul(punch, self_fm), Q31(0.08)) << 4;   // Q24 -> Q28 (punch can exceed 8)
    int32_t fm = (1 << 28) + a + s;                                              // Q28
    if (fm < 0) fm = 0;
    // f0 fm: Mul(f0, fm) is f0 fm / 8 in Q32.
    const uint32_t f64 = static_cast<uint32_t>(ShlSat(Mul(static_cast<int32_t>(f0 >> 1), fm), 4));
    const uint32_t f = f64 > Q32(0.4) ? Q32(0.4) : f64;

    int32_t resonator_out;   // Q25
    if (sustain) {
      int32_t sn, cs;
      oscillator_.Next(f, sustain_gain.Next(), &sn, &cs);
      resonator_out = sn >> 6;
      lp_out_ = cs >> 6;
    } else {
      // set_f_q<FREQUENCY_DIRTY>(f, 1 + q f), per sample.
      int32_t qf = MulRound(q_m, static_cast<int32_t>(f));
      qf = q_shift >= 0 ? ShlSat(qf, q_shift) : qf >> (-q_shift);
      const uint32_t res = 65536u + static_cast<uint32_t>(qf);
      const int32_t g = TanDirtyFast(f);
      const int32_t r = ReciprocalQ16ToQ29(res < 65536u ? 0xffffffffu : res);
      const int32_t h = SvfHFast(g, r);
      const int32_t shift = ShiftFor(g);
      const int32_t gs = g << (4 - shift);
      // Input (pulse - retrig * 0.2) * scale: Q24 x Q27 -> Q20 -> Q25.
      const int32_t x = pulse - (Mul(retrig_pulse_, Q31(0.2)) >> 7);
      const int32_t in = ShlSat(MulRound(x, scale), 5);
      int32_t lp, bp, hp;
      resonator_.Tick(in, gs, shift, r, h, &lp, &bp, &hp);
      resonator_out = bp;
      lp_out_ = lp;
    }
    // ONE_POLE(tone_lp_, pulse * exciter_leak + resonator_out, tone_f).
    const int32_t t_in = (Mul(pulse, exciter_leak) << 1) + resonator_out;   // Q25
    tone_lp_ += MulRound(t_in - tone_lp_, tone_f);
    *out++ = ShlSat(tone_lp_, kAudioBits - 25);
  }
}

void OverdriveFixed::Process(int32_t drive, int32_t* in_out, size_t size) {
  // drive in [0.5, 1]: pre_gain = a + (b - a) d^2 with a = d / 2, b = 24 d^5;
  // post_gain = 1 / SoftClip(0.33 + d (2 - d) (pre_gain - 0.33)).
  const int32_t d = drive;                                // Q31
  const int32_t d2 = MulQ31(d, d);
  const int32_t d4 = MulQ31(d2, d2);
  const int32_t a = d >> 1;                               // Q31
  const int32_t b26 = Mul64(MulQ31(d4, d), 24, 5);         // 24 d^5, Q26
  const int32_t a26 = a >> 5;
  const int32_t pre_gain = a26 + MulQ31(b26 - a26, d2);   // Q26
  const int32_t squashed = MulQ31(d, (INT32_MAX - (d >> 0)) ) ;   // d (1 - d), Q31
  // d (2 - d) = d + d (1 - d), Q30.
  const int32_t ds = (d >> 1) + (squashed >> 1);          // Q30
  const int32_t sc_in = Q(0.33, 26) + (MulQ31(ds, pre_gain - Q(0.33, 26)) << 1);   // Q26
  const int32_t sc = SoftClip(sc_in);                     // Q31, > 0
  // post_gain = 1 / sc, Q29 (sc >= ~0.32 -> post <= ~3.2).
  const int n = CountLeadingZeros(static_cast<uint32_t>(sc));
  const int32_t r = ReciprocalNormalized(sc << (n - 1));   // 1 / x, Q30; sc = x 2^(1 - n)
  const int32_t post_gain = n <= 2 ? r >> (2 - n) : ShlSat(r, n - 2);   // (1 / x) 2^(n - 1), Q29

  Interpolator pre_gain_modulation(&pre_gain_, pre_gain, size);
  Interpolator post_gain_modulation(&post_gain_, post_gain, size);
  while (size--) {
    // pre = pre_gain * in (Q26 x Q26 -> Q21 -> Q26, saturated: SoftClip
    // limits at 3 anyway).
    const int32_t pre = ShlSat(MulRound(pre_gain_modulation.Next(), *in_out), 5);
    *in_out++ = MulRound(SoftClip(pre), post_gain_modulation.Next()) >> 3;   // Q29 -> Q26
  }
}

void SyntheticBassDrumFixed::Init() {
  phase_ = 0;
  phase_noise_ = 0;
  f0_ = 0;
  fm_ = 0;
  fm_lp_ = 0;
  body_env_lp_ = 0;
  body_env_ = 0;
  body_env_pulse_width_ = 0;
  fm_pulse_width_ = 0;
  tone_lp_ = 0;
  sustain_gain_ = 0;
  transient_env_ = 0;
  transient_env_lp_ = 0;
  click_lp_ = 0;
  click_hp_ = 0;
  click_filter_.Init();
  click_filter_.set_f_q<FREQUENCY_FAST>(Q32(5000.0 / kSampleRateD), 2 << 16);
  noise_lp_ = 0;
  noise_hp_ = 0;
}

namespace {

// DistortedSine(phase, phase_noise, dirtiness), Q30 out. Upstream adds the
// noise to the phase and takes the fractional part by truncation towards
// zero: a phase pushed below 0 isn't wrapped.
inline int32_t DistortedSine(uint32_t phase, int32_t phase_noise, int32_t dirtiness) {
  int32_t p = static_cast<int32_t>(phase >> 2) + (Mul(phase_noise, dirtiness) >> 1);   // Q30
  if (p >= (1 << 30)) p -= 1 << 30;
  // triangle = (p < 0.5 ? p : 1 - p) * 4 - 1, Q28.
  const int32_t tri = ((p < (1 << 29) ? p : (1 << 30) - p) << 0) - (1 << 28);   // 4 p in Q28 = p in Q30
  const int32_t sine = SoftRatio(tri >> 4) >> 0;   // tri / (1 + |tri|), Q31 = 2 x in Q30
  const int32_t clean = Sine((static_cast<uint32_t>(p) << 2) + (3u << 30)) >> 1;   // Q30
  return sine + Mul(INT32_MAX - dirtiness, clean - sine);
}

// TransistorVCA(s, gain): s = (s - 0.6) gain; 3 s / (2 + |s|) + 0.3 gain.
// s Q30, gain Q31; Q28 out (|.| < 3.3).
inline int32_t TransistorVCA(int32_t s, int32_t gain) {
  const int32_t x = Mul(s - Q(0.6, 30), gain);   // Q30
  // 3 s / (2 + |s|) = 3 t / (1 + |t|) with t = s / 2 (Q24: x >> 7).
  const int32_t r = SoftRatio(x >> 7);           // t / (1 + |t|), Q31
  return Mul(r, Q31(3.0 / 8.0)) + (Mul(gain, Q31(0.3)) >> 3);   // 3 r, 0.3 gain in Q28
}

}  // namespace

void SyntheticBassDrumFixed::Render(bool sustain, bool trigger, int32_t accent, uint32_t f0,
                                    int32_t tone, int32_t decay, int32_t dirtiness,
                                    int32_t fm_envelope_amount, int32_t fm_envelope_decay,
                                    int32_t* out, size_t size) {
  decay = MulQ15(decay, decay);
  fm_envelope_decay = MulQ15(fm_envelope_decay, fm_envelope_decay);

  InterpolatorReached f0_mod(&f0_, static_cast<int32_t>(f0 >= (1u << 31) ? 0x7fffffffu : f0), size);

  // dirtiness *= max(1 - 8 f0, 0).
  int32_t dirt = Q15ToQ31(dirtiness);
  const int64_t df = (1LL << 31) - (static_cast<int64_t>(f0) << 2);   // 1 - 8 f0 in Q31
  dirt = df <= 0 ? 0 : Mul(dirt, df >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(df));

  // fm_decay = 1 - 1 / (0.008 (1 + 4 fm_envelope_decay) SR).
  static constexpr FFloat kFmTime(0.008 * kSampleRateD);
  static constexpr FFloat kOne(1.0);
  const FFloat fm_time = kFmTime * (kOne + FFloat::FromQ(fm_envelope_decay, 15).Scaled(2));
  const int32_t fm_decay = INT32_MAX - FFloat::Reciprocal(fm_time).ToQ31();
  // body_env_decay = 1 - 1 / (0.02 SR) SemitonesToRatio(-decay 60).
  static constexpr FFloat kBodyRate(1.0 / (0.02 * kSampleRateD));
  const int32_t body_env_decay = INT32_MAX - (kBodyRate * SemitonesToRatio(-decay * 120)).ToQ31();
  const int32_t transient_env_decay = Q31(1.0 - 1.0 / (0.005 * kSampleRateD));
  const FFloat tone_ff = SemitonesToRatio(tone * 216) * FFloatFromQ32(f0).Scaled(2);
  const int32_t tone_f = tone_ff < kOne ? tone_ff.ToQ31() : INT32_MAX;
  const int32_t transient_level = Q15ToQ31(tone);

  if (trigger) {
    fm_ = INT32_MAX;
    body_env_ = transient_env_ = Q31(0.3) + Mul(Q31(0.7), Q15ToQ31(accent));
    body_env_pulse_width_ = static_cast<int32_t>(kSampleRateD * 0.001);
    fm_pulse_width_ = static_cast<int32_t>(kSampleRateD * 0.0013);
  }

  InterpolatorReached sustain_gain(&sustain_gain_, Q15ToQ31(MulQ15(accent, decay)), size);
  const int32_t fm_amount = Q15ToQ31(fm_envelope_amount);

  while (size--) {
    // ONE_POLE(phase_noise_, Random::GetFloat() - 0.5, 0.002).
    const int32_t noise = static_cast<int32_t>(Random::GetWord() - 0x80000000u) >> 1;   // Q31
    phase_noise_ += MulRound(noise - phase_noise_, Q31(0.002));

    int32_t mix = 0;   // Q28
    if (sustain) {
      phase_ += static_cast<uint32_t>(f0_mod.Next());
      const int32_t body = DistortedSine(phase_, phase_noise_, dirt);
      mix -= TransistorVCA(body, sustain_gain.Next());
    } else {
      if (fm_pulse_width_) {
        --fm_pulse_width_;
        phase_ = 1u << 30;   // 0.25
      } else {
        fm_ = MulRound(fm_, fm_decay);
        // fm = 1 + 3.5 fm_envelope_amount fm_lp, Q28.
        const int32_t fm = (1 << 28) + (Mul(Mul(fm_amount, fm_lp_), Q31(3.5 / 4.0)) >> 1);
        // min(f0 fm, 0.5): Mul(f0, fm) is f0 fm / 8 in Q32.
        const int32_t inc = ShlSat(Mul(f0_mod.Next(), fm), 3);
        phase_ += static_cast<uint32_t>(inc);
      }
      if (body_env_pulse_width_) {
        --body_env_pulse_width_;
      } else {
        body_env_ = MulRound(body_env_, body_env_decay);
        transient_env_ = MulRound(transient_env_, transient_env_decay);
      }
      body_env_lp_ += MulRound(body_env_ - body_env_lp_, Q31(0.1));
      transient_env_lp_ += MulRound(transient_env_ - transient_env_lp_, Q31(0.1));
      fm_lp_ += MulRound(fm_ - fm_lp_, Q31(0.1));

      const int32_t body = DistortedSine(phase_, phase_noise_, dirt);
      // click_.Process(in): SLOPE(lp, in, 0.5, 0.1); ONE_POLE(hp, lp, 0.04);
      // LP(lp - hp) (Q30).
      const int32_t click_in = body_env_pulse_width_ ? 0 : (1 << 30);
      const int32_t e = click_in - click_lp_;
      click_lp_ += MulRound(e, e > 0 ? Q31(0.5) : Q31(0.1));
      click_hp_ += MulRound(click_lp_ - click_hp_, Q31(0.04));
      const int32_t click = click_filter_.Process<FILTER_MODE_LOW_PASS>(click_lp_ - click_hp_);
      // noise_.Render(): ONE_POLE(lp, u, 0.05); ONE_POLE(hp, lp, 0.005); lp - hp.
      const int32_t u = static_cast<int32_t>(Random::GetWord() >> 1);   // Q31
      noise_lp_ += MulRound(u - noise_lp_, Q31(0.05));
      noise_hp_ += MulRound(noise_lp_ - noise_hp_, Q31(0.005));
      const int32_t transient = click + ((noise_lp_ - noise_hp_) >> 1);   // Q30

      mix -= TransistorVCA(body, body_env_lp_);
      mix -= Mul(Mul(transient, transient_env_lp_), transient_level) >> 2;
    }
    // ONE_POLE(tone_lp_, mix, tone_f) in Q28.
    tone_lp_ += MulRound(mix - tone_lp_, tone_f);
    *out++ = tone_lp_ >> (28 - kAudioBits);
  }
}

void BassDrumEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  analog_bass_drum_.Init();
  synthetic_bass_drum_.Init();
  overdrive_.Init();
}

void BassDrumEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                                 size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const int32_t h = parameters.harmonics;
  const int32_t attack_fm_amount = Clip(h * 4, 0, kQ15One);
  const int32_t self_fm_amount = Clip(h * 4 - kQ15One, 0, kQ15One);
  // drive = max(2 h - 1, 0) max(1 - 16 f0, 0), Q31.
  const int32_t d1 = Clip(h * 2 - kQ15One, 0, kQ15One);
  const int64_t d2 = (1LL << 31) - (static_cast<int64_t>(f0) << 3);   // 1 - 16 f0, Q31
  const int32_t drive = d2 <= 0 ? 0
      : Mul(Q15ToQ31(d1), d2 >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(d2));
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;

  analog_bass_drum_.Render(sustain, trigger, parameters.accent, f0, parameters.timbre,
                           parameters.morph, attack_fm_amount, self_fm_amount, out, size);
  overdrive_.Process((1 << 30) + (drive >> 1), out, size);
  const int32_t m2 = MulQ15(parameters.morph, parameters.morph);
  const int32_t dirtiness = sustain ? h : Q(0.4, 15) - MulQ15(Q(0.25, 15), m2);
  synthetic_bass_drum_.Render(sustain, trigger, parameters.accent, f0, parameters.timbre,
                              parameters.morph, dirtiness, Clip(h * 2, 0, kQ15One),
                              Clip(h * 2 - kQ15One, 0, kQ15One), aux, size);
}

}  // namespace op1plaits
