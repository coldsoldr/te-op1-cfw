#include "fixed/voice.h"

namespace op1plaits {

namespace {

const int kTriggerDelay = 5;

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

// ApplyModulations: amount *= max(|amount| - 0.05, 0.05) * 1.05; value =
// base + amount * modulation, clamped. Q15 amounts and values (note: Q16).
inline int32_t ModulationAmount(int32_t amount_q15) {
  const int32_t a = Abs(amount_q15) - Q(0.05, 15);
  const int32_t k = a > Q(0.05, 15) ? a : Q(0.05, 15);
  return Mul64(MulQ15(amount_q15, k), Q(1.05, 15), 15);   // Q15
}

// 1 / x for x in Q26 (> 1): Q31.
inline int32_t ReciprocalQ26(int32_t x) {
  const int n = CountLeadingZeros(static_cast<uint32_t>(x));   // x >= 2^26: n <= 5
  const int32_t r = ReciprocalNormalized(x << (n - 1));         // x = y 2^(6 - n)
  return r >> (5 - n);   // (1 / y) 2^(n - 6) in Q31: r 2^(n - 6 + 1)
}

// 16-bit output: Clip16(1 + trunc(-32767 v)), v in Q26.
inline int16_t ToInt16(int32_t v) {
  const int32_t a = v < 0 ? -v : v;
  const int32_t m = Mul(a, Q31(32767.0 / 65536.0)) >> 10;   // floor(|v| 32767)
  int32_t r = 1 + (v > 0 ? -m : m);
  return static_cast<int16_t>(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
}

}  // namespace

void LPGEnvelopeFixed::ProcessPing(int32_t attack, int32_t short_decay, int32_t decay_tail,
                                   int32_t hf) {
  if (ramp_up_) {
    const int64_t v = static_cast<int64_t>(vactrol_state_) + attack;
    if (v >= INT32_MAX) {
      vactrol_state_ = INT32_MAX;
      ramp_up_ = false;
    } else {
      vactrol_state_ = static_cast<int32_t>(v);
    }
  }
  ProcessLP(ramp_up_ ? vactrol_state_ : 0, short_decay, decay_tail, hf);
}

void LPGEnvelopeFixed::ProcessLP(int32_t level, int32_t short_decay, int32_t decay_tail,
                                 int32_t hf) {
  const int32_t vactrol_error = level - vactrol_state_;
  const int32_t vs2 = MulQ31(vactrol_state_, vactrol_state_);
  const int32_t vs4 = MulQ31(vs2, vs2);
  const int32_t tail = INT32_MAX - vactrol_state_;
  const int32_t tail_2 = MulQ31(tail, tail);
  const int32_t coefficient = vactrol_error > 0
      ? Q31(0.6) : short_decay + MulQ31(INT32_MAX - vs4, decay_tail);
  vactrol_state_ += MulQ31(coefficient, vactrol_error);
  gain_ = vactrol_state_;
  frequency_ = Q31(0.003) + MulQ31(Q31(0.3), vs4) + MulQ31(hf, Q31(0.04));
  hf_bleed_ = MulQ31(MulQ31(tail_2 + MulQ31(INT32_MAX - tail_2, hf), hf), hf);
}

void LimiterFixed::Process(int32_t pre_gain, int32_t* in_out, size_t size) {
  while (size--) {
    const int32_t s = ShlSat(MulRound(*in_out, pre_gain), 3);   // Q26
    // SLOPE(peak_, |s|, 0.05, 0.00002).
    const int32_t error = Abs(s) - peak_;
    peak_ += MulRound(error, error > 0 ? Q31(0.05) : Q31(0.00002));
    const int32_t gain = peak_ <= (1 << 26) ? INT32_MAX : ReciprocalQ26(peak_);
    *in_out++ = Mul(Mul(s, gain), Q31(0.8));
  }
}

void ChannelPostProcessorFixed::Process(int32_t gain, bool bypass_lpg, int32_t lpg_gain,
                                        int32_t lpg_frequency, int32_t lpg_hf_bleed,
                                        int32_t* in, int16_t* out, size_t size, size_t stride) {
  if (gain < 0) {
    limiter_.Process(-gain, in, size);
  }
  const int32_t post_gain = gain < 0 ? (1 << 28) : gain;   // Q28 (the -32767 is in ToInt16)
  if (!bypass_lpg) {
    Interpolator gain_modulation(&lpg_previous_gain_, MulQ31(post_gain, lpg_gain), size);
    lpg_filter_.set_f_q<FREQUENCY_DIRTY>(static_cast<uint32_t>(lpg_frequency) << 1, Q(0.4, 16));
    while (size--) {
      const int32_t s = ShlSat(MulRound(*in++, gain_modulation.Next()), 3);   // Q26
      const int32_t lp = lpg_filter_.Process<FILTER_MODE_LOW_PASS>(s);
      *out = ToInt16(lp + MulRound(s - lp, lpg_hf_bleed));
      out += stride;
    }
  } else {
    while (size--) {
      *out = ToInt16(ShlSat(MulRound(*in++, post_gain), 3));
      out += stride;
    }
  }
}

void VoiceFixed::Init() {
  engine_.Init(0);
  previous_engine_index_ = -1;
  out_post_processor_.Init();
  aux_post_processor_.Init();
  decay_envelope_ = 0;
  lpg_envelope_.Init();
  trigger_state_ = false;
  previous_note_ = 0;
  for (int i = 0; i < 8; ++i) trigger_delay_line_[i] = 0;
  trigger_delay_write_ptr_ = 0;
  trigger_delay_ = kTriggerDelay;
  channels_ = 3;
}

void VoiceFixed::Render(const VoicePatch& patch, const VoiceModulations& modulations,
                        int16_t* frames, size_t size) {
  // Delay the trigger by 1 ms (sequencers' CV lags their gate).
  trigger_delay_line_[trigger_delay_write_ptr_] = modulations.trigger;
  trigger_delay_write_ptr_ = (trigger_delay_write_ptr_ - 1) & 7;
  const int32_t trigger_value = trigger_delay_line_[(trigger_delay_write_ptr_ + trigger_delay_) & 7];

  const bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (trigger_value > Q(0.3, 15)) {
      trigger_state_ = true;
      if (!modulations.level_patched) lpg_envelope_.Trigger();
      decay_envelope_ = INT32_MAX;
    }
  } else if (trigger_value < Q(0.1, 15)) {
    trigger_state_ = false;
  }

  const int engine_index = patch.engine;
  if (engine_index != previous_engine_index_) {
    const uint32_t seed = Random::state();
    engine_.Init(engine_index);   // upstream: Reset() of an engine Init()ed at start
    Random::Seed(previous_engine_index_ < 0 ? 0x21 : seed);
    out_post_processor_.Reset();
    previous_engine_index_ = engine_index;
  }

  FixedParameters p;
  const bool rising_edge = trigger_state_ && !previous_trigger_state;
  const int32_t note = (modulations.note >> 1) + (previous_note_ >> 1);
  previous_note_ = modulations.note;
  const PostProcessing& pp = kPostProcessing[engine_index];

  if (modulations.trigger_patched) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) |
                (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }

  // short_decay = (200 kBlockSize) / SR * SemitonesToRatio(-96 decay), Q31.
  static constexpr FFloat kShortDecayRate(200.0 * PLAITS_BLOCK_SIZE / kSampleRateD);
  const int32_t short_decay = (kShortDecayRate * SemitonesToRatio(-patch.decay * 192)).ToQ31();
  decay_envelope_ = MulRound(decay_envelope_, INT32_MAX - (short_decay << 1));

  // compressed_level = clamp(1.3 level / (0.3 + |level|), 0, 1), Q15.
  int32_t compressed_level = 0;
  {
    const int32_t level = modulations.level;
    const int32_t num = Mul64(level, Q(1.3, 15), 15);
    const int32_t den = Q(0.3, 15) + Abs(level);
    if (num > 0) {
      compressed_level = num >= den ? kQ15One : static_cast<int32_t>(
          Fraction(static_cast<uint32_t>(num), static_cast<uint32_t>(den)) >> 16);
    }
  }
  p.accent = modulations.level_patched ? compressed_level : Q(0.8, 15);

  const bool use_internal_envelope = modulations.trigger_patched;
  p.harmonics = Clip(patch.harmonics + modulations.harmonics, 0, kQ15One);
  int32_t internal_envelope_amplitude = kQ15One;
  if (engine_index == 7) {
    internal_envelope_amplitude = Clip(2 * kQ15One - p.harmonics * 6, 0, kQ15One);
  }
  const int32_t envelope = decay_envelope_ >> 16;   // Q15
  // p.note: modulation in semitones (the internal envelope: amp env^2 48).
  {
    const int32_t amount = ModulationAmount(patch.frequency_modulation_amount);
    int32_t modulation;   // Q16
    if (modulations.frequency_patched) {
      modulation = modulations.frequency;
    } else if (use_internal_envelope) {
      modulation = Mul64(MulQ15(internal_envelope_amplitude, MulQ15(envelope, envelope)), 48 << 16, 15);
    } else {
      modulation = 1 << 16;
    }
    p.note = Clip(patch.note + note + Mul64(amount, modulation, 15), -(119 << 16), 120 << 16);
  }
  {
    // Upstream scales TIMBRE's internal envelope by
    // internal_envelope_amplitude_timbre, which stays 1 (speech included).
    const int32_t amount = ModulationAmount(patch.timbre_modulation_amount);
    const int32_t modulation = modulations.timbre_patched ? modulations.timbre
        : (use_internal_envelope ? envelope : 0);
    p.timbre = Clip(patch.timbre + MulQ15(amount, modulation), 0, kQ15One);
  }
  {
    const int32_t amount = ModulationAmount(patch.morph_modulation_amount);
    const int32_t modulation = modulations.morph_patched ? modulations.morph
        : (use_internal_envelope ? MulQ15(internal_envelope_amplitude, envelope) : 0);
    p.morph = Clip(patch.morph + MulQ15(amount, modulation), 0, kQ15One);
  }

  bool already_enveloped = pp.already_enveloped;
  engine_.Render(p, out_buffer_, aux_buffer_, size, &already_enveloped);

  const bool lpg_bypass = already_enveloped || patch.lpg_off ||
      (!modulations.level_patched && !modulations.trigger_patched);
  if (!lpg_bypass) {
    const int32_t hf = Q15ToQ31(patch.lpg_colour);
    static constexpr FFloat kTailRate(20.0 * PLAITS_BLOCK_SIZE / kSampleRateD);
    const int32_t decay_tail = (kTailRate * SemitonesToRatio(-patch.decay * 144 + patch.lpg_colour * 24)).ToQ31()
        - short_decay;
    if (modulations.level_patched) {
      lpg_envelope_.ProcessLP(Q15ToQ31(compressed_level), short_decay, decay_tail, hf);
    } else {
      // attack = NoteToFrequency(note) * kBlockSize * 2, Q31 (saturated).
      const uint64_t a = static_cast<uint64_t>(NoteToFrequency(p.note)) * PLAITS_BLOCK_SIZE;
      const int32_t attack = a >= 0x7fffffffull ? INT32_MAX : static_cast<int32_t>(a);
      lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
    }
  } else {
    lpg_envelope_.Init();
  }

  if (channels_ & 1) {
    out_post_processor_.Process(pp.out_gain, lpg_bypass, lpg_envelope_.gain(),
                                lpg_envelope_.frequency(), lpg_envelope_.hf_bleed(), out_buffer_,
                                frames, size, 2);
  }
  if (channels_ & 2) {
    aux_post_processor_.Process(pp.aux_gain, lpg_bypass, lpg_envelope_.gain(),
                                lpg_envelope_.frequency(), lpg_envelope_.hf_bleed(), aux_buffer_,
                                frames + 1, size, 2);
  }
}

}  // namespace op1plaits
