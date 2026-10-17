#include "fixed/speech_engine.h"

namespace op1plaits {

namespace {

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

// a0 (upstream: 55 Hz / the corrected sample rate) as an FFloat.
#if PLAITS_SAMPLE_RATE == 48000
constexpr double kA0 = 55.0 / static_cast<double>(47872.34f);
#else
constexpr double kA0 = 55.0 / 44100.0;
#endif

// 1 / p for an integer 0 < p < 2^24, Q32 (saturated).
inline uint32_t ReciprocalInteger(uint32_t p) {
  if (p <= 1) return 0xffffffffu;
  const int n = CountLeadingZeros(p);
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>((p << n) >> 1));   // Q30 of 1 / x
  // p = x 2^(32 - n): 1 / p = (1 / x) 2^(n - 32); Q32 = r 2^(n - 30).
  return n >= 30 ? static_cast<uint32_t>(r) << (n - 30) : static_cast<uint32_t>(r) >> (30 - n);
}

}  // namespace

// --- Naive formant synth -------------------------------------------------------

void NaiveSpeechSynthFixed::Init() {
  pulse_.Init();
  click_duration_ = 0;
  for (int i = 0; i < kNaiveSpeechNumFormants; ++i) filter_[i].Init();
  pulse_coloration_.Init();
  pulse_coloration_.set_f_q<FREQUENCY_DIRTY>(Q32(800.0 / kSampleRateD), 1 << 15);
}

void NaiveSpeechSynthFixed::Render(bool click, uint32_t frequency, int32_t phoneme,
                                   int32_t vocal_register, int32_t* excitation, int32_t* output,
                                   size_t size) {
  if (click) click_duration_ = static_cast<uint32_t>(kSampleRateD * 0.05);
  click_duration_ -= click_duration_ < size ? click_duration_ : size;
  if (click_duration_) frequency >>= 1;

  // Excitation: a band-passed impulse train, x 4 (Q23).
  pulse_.Render<OSCILLATOR_SHAPE_IMPULSE_TRAIN>(frequency, 1u << 31, excitation, size);
  for (size_t i = 0; i < size; ++i) excitation[i] >>= 6;   // Q29 -> Q23
  pulse_coloration_.Process<FILTER_MODE_BAND_PASS>(excitation, excitation, size);
  for (size_t i = 0; i < size; ++i) excitation[i] <<= 2;

  // p = phoneme * (5 - 1.001), r = register * (5 - 1.001), Q16.
  const int32_t p = Mul64(phoneme, Q(kNaiveSpeechNumPhonemes - 1.001, 16), 31);
  const int32_t r = Mul64(vocal_register, Q(kNaiveSpeechNumRegisters - 1.001, 16), 31);
  const int pi = p >> 16, ri = r >> 16;
  const int32_t pf = p & 0xffff, rf = r & 0xffff;   // Q16

  for (size_t i = 0; i < size; ++i) output[i] = 0;
  for (int i = 0; i < kNaiveSpeechNumFormants; ++i) {
    const uint8_t* p0r0 = naive_speech_phonemes[pi][ri][i];
    const uint8_t* p0r1 = naive_speech_phonemes[pi][ri + 1][i];
    const uint8_t* p1r0 = naive_speech_phonemes[pi + 1][ri][i];
    const uint8_t* p1r1 = naive_speech_phonemes[pi + 1][ri + 1][i];
    // Frequencies (semitones) and amplitudes, bilinear, Q16.
    const int32_t p0r_f = (p0r0[0] << 16) + (p0r1[0] - p0r0[0]) * rf;
    const int32_t p1r_f = (p1r0[0] << 16) + (p1r1[0] - p1r0[0]) * rf;
    int32_t f = p0r_f + Mul64(p1r_f - p0r_f, pf, 16);
    const int32_t p0r_a = (p0r0[1] << 16) + (p0r1[1] - p0r0[1]) * rf;
    const int32_t p1r_a = (p1r0[1] << 16) + (p1r1[1] - p1r0[1]) * rf;
    const int32_t a16 = p0r_a + Mul64(p1r_a - p0r_a, pf, 16);   // amplitude * 256, Q16
    const int32_t a = a16 << 7;   // / 256, Q31
    if (f >= (160 << 16)) f = 160 << 16;
    // f = a0 SemitonesToRatio(f - 33).
    static constexpr FFloat kA0F(kA0);
    uint32_t fq = static_cast<uint32_t>((kA0F * SemitonesToRatio(f - (33 << 16))).ToQ(31)) << 1;
    if (fq > Q32(0.49)) fq = Q32(0.49);   // the tan approximation's range
    if (click_duration_ && i == 0) fq >>= 1;
    filter_[i].set_f_q<FREQUENCY_DIRTY>(fq, 20 << 16);
    for (size_t j = 0; j < size; ++j) {
      output[j] += Mul(filter_[i].Process<FILTER_MODE_BAND_PASS>(excitation[j]), a);
    }
  }
}

// --- SAM formant synth -----------------------------------------------------------

void SAMSpeechSynthFixed::Init() {
  phase_ = 0;
  frequency_ = 0;
  pulse_next_sample_ = 0;
  pulse_lp_ = 0;
  formant_phase_[0] = formant_phase_[1] = formant_phase_[2] = 0;
  consonant_samples_ = 0;
  consonant_index_ = 0;
}

void SAMSpeechSynthFixed::Render(bool consonant, uint32_t frequency, int32_t vowel,
                                 int32_t formant_shift, int32_t* excitation, int32_t* output,
                                 size_t size) {
  if (frequency >= (1u << 28)) frequency = 1u << 28;   // 0.0625
  if (consonant) {
    consonant_samples_ = static_cast<uint32_t>(kSampleRateD * 0.05);
    // r = int((vowel + 3 frequency + 7 formant_shift) * 8), Q24 sums.
    const int32_t s = (vowel >> 7) + 3 * static_cast<int32_t>(frequency >> 8) + 7 * (formant_shift >> 7);
    consonant_index_ = (s >> 21) & (kSAMNumConsonants - 1);   // * 8, % 8
  }
  consonant_samples_ -= consonant_samples_ < size ? consonant_samples_ : size;

  // phoneme: consonant or vowel * (9 - 1.0001), Q16.
  const int32_t phoneme = consonant_samples_
      ? ((consonant_index_ + kSAMNumVowels) << 16)
      : Mul64(vowel, Q(kSAMNumVowels - 1.0001, 16), 31);
  const int pi = phoneme >> 16;
  const int32_t pf = (phoneme & 0xffff) << 15;   // Q31
  // formant_shift = 1 + 2.5 shift; f * 8 * shift * 2^32 / SR (Q32).
  static constexpr FFloat kScale(8.0 / kSampleRateD);
  static constexpr FFloat kOne(1.0), kTwoAndHalf(2.5);
  const FFloat shift = kOne + kTwoAndHalf * FFloat::FromQ(formant_shift, 31);
  uint32_t formant_frequency[kSAMNumFormants];
  int32_t formant_amplitude[kSAMNumFormants];
  for (int i = 0; i < kSAMNumFormants; ++i) {
    const int32_t f1 = sam_speech_phonemes[pi][i][0];
    const int32_t f2 = sam_speech_phonemes[pi + 1][i][0];
    const int32_t f = (f1 << 16) + Mul64(f2 - f1, pf, 15);   // Q16
    formant_frequency[i] = static_cast<uint32_t>((FFloat::FromQ(f, 16) * kScale * shift).ToQ(31)) << 1;
    const int32_t a1 = sam_formant_amplitude_q31[sam_speech_phonemes[pi][i][1]];
    const int32_t a2 = sam_formant_amplitude_q31[sam_speech_phonemes[pi + 1][i][1]];
    formant_amplitude[i] = a1 + Mul(a2 - a1, pf);
  }

  Interpolator fm(&frequency_, static_cast<int32_t>(frequency), size);
  int32_t pulse_next_sample = pulse_next_sample_;
  while (size--) {
    int32_t pulse_this_sample = pulse_next_sample;
    pulse_next_sample = 0;
    const uint32_t f = static_cast<uint32_t>(fm.Next());
    const uint32_t old = phase_;
    phase_ += f;
    if (phase_ < old) {
      const int32_t t = phase_ < f ? Fraction(phase_, f) : INT32_MAX;
      for (int k = 0; k < kSAMNumFormants; ++k) {
        formant_phase_[k] = static_cast<uint32_t>(Mul(t, static_cast<int32_t>(formant_frequency[k])));
      }
      pulse_this_sample -= ThisBlepSample(t) >> 3;   // Q31 -> Q28
      pulse_next_sample -= NextBlepSample(t) >> 3;
    } else {
      for (int k = 0; k < kSAMNumFormants; ++k) formant_phase_[k] += formant_frequency[k];
    }
    pulse_next_sample += static_cast<int32_t>(phase_ >> 4);   // Q28
    const int32_t d = pulse_this_sample - (1 << 27) - pulse_lp_;
    const int32_t k = f >= (1u << 28) ? INT32_MAX : static_cast<int32_t>(f << 3);   // min(16 f, 1)
    pulse_lp_ += Mul(k, d);
    *excitation++ = d >> 5;   // Q23
    int32_t s = 0;
    for (int i = 0; i < kSAMNumFormants; ++i) {
      // SineRaw; amplitudes < 0.5 each: the sum (< 1.5) in Q30.
      s += Mul(lut_sine_q31[formant_phase_[i] >> 23], formant_amplitude[i]) >> 1;
    }
    s = Mul(s, static_cast<int32_t>(0x7fffffffu - (phase_ >> 1)));   // (1 - phase)
    *output++ = s >> 7;   // Q30 -> Q23
  }
  pulse_next_sample_ = pulse_next_sample;
}

// --- LPC synth ---------------------------------------------------------------

void LPCSpeechSynthFixed::Init() {
  phase_ = 0;
  f_ = 0;
  frequency_ = static_cast<int32_t>(Q32(0.0125));
  noise_energy_ = 0;
  pulse_energy_ = 0;
  next_sample_ = 0;
  excitation_pulse_sample_index_ = 0;
  for (int i = 0; i < kLPCOrder; ++i) k_[i] = 0;
  for (int i = 0; i <= kLPCOrder; ++i) s_[i] = 0;
}

void LPCSpeechSynthFixed::ComputeFrequency(int32_t prosody_amount, FFloat pitch_shift) {
  // f = (base_f0 + (frequency - base_f0) prosody) pitch_shift, clamped to
  // [0, 0.5] (Q32), base_f0 = 100 / 8000. Upstream computes it on every
  // Render() call; frequency_ only changes in PlayFrame(), so once per block
  // is the same. pitch_shift stays an FFloat: a Q16 pitch shift (1e-5
  // relative) drifts the pitch phase audibly against the reference within
  // a second (the upper harmonics of the voice).
  const int32_t base_f0 = static_cast<int32_t>(Q32(100.0 / 8000.0));
  const int32_t d = frequency_ - base_f0;
  const int32_t f0 = base_f0 + Mul(d, prosody_amount);
  f_ = f0 <= 0 ? 0 : static_cast<uint32_t>((FFloatFromQ32(static_cast<uint32_t>(f0)) * pitch_shift).ToQ(32));
}

void LPCSpeechSynthFixed::Render(int32_t* excitation, int32_t* output) {
  const uint32_t f = f_;
  const uint32_t old = phase_;
  phase_ += f;
  int32_t this_sample = next_sample_;
  int32_t next_sample = 0;
  if (phase_ < old) {
    const int32_t reset_time = phase_ < f ? Fraction(phase_, f) : INT32_MAX;
    const int reset_sample = reset_time >> 26;   // int(32 reset_time)
    int32_t discontinuity = 0;
    if (excitation_pulse_sample_index_ < 640) {
      excitation_pulse_sample_index_ -= reset_sample;
      if (excitation_pulse_sample_index_ < 0) excitation_pulse_sample_index_ = 0;
      const int32_t s = lut_lpc_excitation_pulse_i8[excitation_pulse_sample_index_];
      discontinuity = Mul(pulse_energy_, s << 24);   // s / 128 energy, Q26
    }
    this_sample -= Mul(discontinuity, ThisBlepSample(reset_time));
    next_sample -= Mul(discontinuity, NextBlepSample(reset_time));
    excitation_pulse_sample_index_ = reset_sample;
  }

  int32_t e[11];
  e[10] = static_cast<int16_t>(Random::GetWord() >> 16) > 0 ? noise_energy_ : -noise_energy_;
  if (excitation_pulse_sample_index_ < 640) {
    const int32_t s = lut_lpc_excitation_pulse_i8[excitation_pulse_sample_index_];
    next_sample += Mul(pulse_energy_, s << 24);
    excitation_pulse_sample_index_ += 32;
  }
  e[10] += this_sample;
  e[10] += e[10] >> 1;   // * 1.5
  for (int i = 9; i >= 0; --i) e[i] = e[i + 1] - Mul(k_[i], s_[i]);
  e[0] = Clip(e[0], -(2 << 26), 2 << 26);
  for (int i = 9; i >= 1; --i) s_[i] = s_[i - 1] + Mul(k_[i - 1], e[i - 1]);
  s_[0] = e[0];
  *excitation = e[10];
  *output = e[0];
  next_sample_ = next_sample;
}

void LPCSpeechSynthFixed::PlayFrame(const LPCFrame& f1, const LPCFrame& f2, int32_t blend) {
  const int32_t frequency_1 = f1.period == 0 ? frequency_ : static_cast<int32_t>(ReciprocalInteger(f1.period));
  const int32_t frequency_2 = f2.period == 0 ? frequency_ : static_cast<int32_t>(ReciprocalInteger(f2.period));
  frequency_ = frequency_1 + Mul(frequency_2 - frequency_1, blend);
  const int32_t energy_1 = f1.energy << 18;   // / 256, Q26
  const int32_t energy_2 = f2.energy << 18;
  const int32_t noise_1 = f1.period == 0 ? energy_1 : 0;
  const int32_t noise_2 = f2.period == 0 ? energy_2 : 0;
  noise_energy_ = noise_1 + Mul(noise_2 - noise_1, blend);
  const int32_t pulse_1 = f1.period != 0 ? energy_1 : 0;
  const int32_t pulse_2 = f2.period != 0 ? energy_2 : 0;
  pulse_energy_ = pulse_1 + Mul(pulse_2 - pulse_1, blend);
  // k0, k1 / 32768; k2..k9 / 128 (Q31).
  const int32_t a[kLPCOrder] = { f1.k0 << 16, f1.k1 << 16, f1.k2 << 24, f1.k3 << 24, f1.k4 << 24,
                                 f1.k5 << 24, f1.k6 << 24, f1.k7 << 24, f1.k8 << 24, f1.k9 << 24 };
  const int32_t b[kLPCOrder] = { f2.k0 << 16, f2.k1 << 16, f2.k2 << 24, f2.k3 << 24, f2.k4 << 24,
                                 f2.k5 << 24, f2.k6 << 24, f2.k7 << 24, f2.k8 << 24, f2.k9 << 24 };
  // |b - a| reaches 1.9 (k0 from -0.98 to 0.85): the difference in Q30.
  for (int i = 0; i < kLPCOrder; ++i) {
    k_[i] = a[i] + (Mul((b[i] >> 1) - (a[i] >> 1), blend) << 1);
  }
}

void LPCSpeechSynthFixed::PlayFrame(const LPCFrame* frames, int integral, int32_t fractional) {
  PlayFrame(frames[integral], frames[integral + 1], fractional);
}

// --- LPC word banks --------------------------------------------------------------

// The banks are decoded at table-generation time (resources/gen_tables.cc,
// with upstream's decoder): Load() only selects one.

void LPCSpeechSynthWordBankFixed::Init(Allocator* allocator) {
  (void)allocator;
  Reset();
}

void LPCSpeechSynthWordBankFixed::Reset() {
  loaded_bank_ = -1;
  num_frames_ = 0;
  num_words_ = 0;
  frames_ = lpc_word_bank_frames;
  word_boundaries_ = lpc_word_bank_boundaries[0];
}

bool LPCSpeechSynthWordBankFixed::Load(int bank) {
  if (bank == loaded_bank_ || bank >= kLPCSpeechSynthNumWordBanks) return false;
  const LPCWordBankFixed& b = lpc_word_banks[bank];
  frames_ = lpc_word_bank_frames + b.first_frame;
  num_frames_ = b.num_frames;
  num_words_ = b.num_words;
  word_boundaries_ = lpc_word_bank_boundaries[bank];
  loaded_bank_ = bank;
  return true;
}

void LPCSpeechSynthWordBankFixed::GetWordBoundaries(int32_t address, int* start, int* end) const {
  if (num_words_ == 0) {
    *start = *end = -1;
  } else {
    int word = static_cast<int>((static_cast<int64_t>(address) * num_words_) >> 31);
    if (word >= num_words_) word = num_words_ - 1;
    *start = word_boundaries_[word];
    *end = word_boundaries_[word + 1] - 1;
  }
}

// --- LPC controller ------------------------------------------------------------



void LPCSpeechSynthControllerFixed::Init(LPCSpeechSynthWordBankFixed* word_bank) {
  word_bank_ = word_bank;
  clock_phase_ = 0;
  playback_frame_ = -1;
  last_playback_frame_ = -1;
  remaining_frame_samples_ = 0;
  sample_[0] = sample_[1] = 0;
  next_sample_[0] = next_sample_[1] = 0;
  gain_ = 0;
  synth_.Init();
}

void LPCSpeechSynthControllerFixed::Render(bool free_running, bool trigger, int bank,
                                           uint32_t frequency, int32_t prosody_amount,
                                           int32_t speed, int32_t address, int32_t formant_shift,
                                           int32_t gain, int32_t* excitation, int32_t* output,
                                           size_t size) {
  // rate_ratio = SemitonesToRatio((formant_shift - 0.5) 36); rate = ratio / 6.
  const int32_t shift_semis = Mul64(formant_shift - (1 << 30), 36 << 16, 31);
  const FFloat rate_ratio = SemitonesToRatio(shift_semis);
  static constexpr FFloat kSixth(1.0 / 6.0);
  const uint32_t rate = static_cast<uint32_t>((rate_ratio * kSixth).ToQ31());
  // pitch_shift = frequency / (rate_ratio 100 / corrected SR).
#if PLAITS_SAMPLE_RATE == 48000
  static constexpr FFloat kF0Norm(100.0 / static_cast<double>(47872.34f));
#else
  static constexpr FFloat kF0Norm(100.0 / 44100.0);
#endif
  const FFloat pitch_shift = FFloatFromQ32(frequency) / (rate_ratio * kF0Norm);
  // time_stretch = SemitonesToRatio(-24 speed + (shift < 0.4 ? (shift - 0.4)
  // -45 : shift > 0.6 ? (shift - 0.6) -45 : 0)).
  int32_t ts_semis = -Mul64(speed, 24 << 16, 31);
  if (formant_shift < Q31(0.4)) {
    ts_semis += Mul64(Q31(0.4) - formant_shift, 45 << 16, 31);
  } else if (formant_shift > Q31(0.6)) {
    ts_semis -= Mul64(formant_shift - Q31(0.6), 45 << 16, 31);
  }
  const FFloat time_stretch = SemitonesToRatio(ts_semis);

  if (bank != -1) {
    if (word_bank_->Load(bank)) {
      playback_frame_ = -1;
      last_playback_frame_ = -1;
    }
  }
  const LPCFrame* frames;
  int num_frames;
  if (bank == -1) {
    frames = lpc_phoneme_frames;
    num_frames = kLPCSpeechSynthNumVowels;
  } else {
    frames = word_bank_->frames();
    num_frames = word_bank_->num_frames();
  }

  if (trigger) {
    if (bank == -1) {
      // A pseudo-random consonant for the duration of a frame:
      // int((address + 3 shift + 7 frequency) 8) % 10.
      const int32_t s = (address >> 7) + 3 * (formant_shift >> 7) +
                        7 * static_cast<int32_t>(frequency >> 8);   // Q24
      int r = s >> 21;
      while (r >= kLPCSpeechSynthNumConsonants) r -= kLPCSpeechSynthNumConsonants;
      playback_frame_ = r + kLPCSpeechSynthNumVowels;
      last_playback_frame_ = playback_frame_ + 1;
    } else {
      word_bank_->GetWordBoundaries(address, &playback_frame_, &last_playback_frame_);
    }
    remaining_frame_samples_ = 0;
  }

  if (playback_frame_ == -1 && remaining_frame_samples_ == 0) {
    // address * (num_frames - 1.0001): Q24 x Q24 -> Q48. The fraction
    // blends the reflection coefficients; a Q16 fraction (as first written)
    // was coarse enough to make resonant frames ring audibly differently.
    const int64_t span = (static_cast<int64_t>(num_frames) << 24) - Q(1.0001, 24);
    const int64_t position = static_cast<int64_t>(address >> 7) * span;
    synth_.PlayFrame(frames, static_cast<int>(position >> 48),
                     static_cast<int32_t>((position >> 17) & 0x7fffffff));
  } else {
    if (remaining_frame_samples_ == 0) {
      synth_.PlayFrame(frames, playback_frame_, 0);
      static constexpr FFloat kFrameSamples(kSampleRateD / 40.0);
      remaining_frame_samples_ = static_cast<uint32_t>((kFrameSamples * time_stretch).Floor());
      ++playback_frame_;
      if (playback_frame_ >= last_playback_frame_) {
        const bool back_to_scan_mode = bank == -1 || free_running;
        playback_frame_ = back_to_scan_mode ? -1 : last_playback_frame_;
      }
    }
    remaining_frame_samples_ -= remaining_frame_samples_ < size ? remaining_frame_samples_ : size;
  }

  synth_.ComputeFrequency(prosody_amount, pitch_shift);
  Interpolator gain_modulation(&gain_, gain, size);
  while (size--) {
    int32_t this_sample[2] = { next_sample_[0], next_sample_[1] };
    next_sample_[0] = next_sample_[1] = 0;
    clock_phase_ += rate;
    if (clock_phase_ >= (1u << 31)) {
      clock_phase_ -= 1u << 31;
      const int32_t reset_time = clock_phase_ < rate ? Fraction(clock_phase_, rate) : INT32_MAX;
      int32_t new_sample[2];
      synth_.Render(&new_sample[0], &new_sample[1]);
      for (int c = 0; c < 2; ++c) {
        const int32_t discontinuity = new_sample[c] - sample_[c];   // Q26
        this_sample[c] += Mul(discontinuity, ThisBlepSample(reset_time));
        next_sample_[c] += Mul(discontinuity, NextBlepSample(reset_time));
        sample_[c] = new_sample[c];
      }
    }
    next_sample_[0] += sample_[0];
    next_sample_[1] += sample_[1];
    const int32_t g = gain_modulation.Next();
    *excitation++ = Mul(this_sample[0], g) >> 3;   // Q26 -> Q23
    *output++ = Mul(this_sample[1], g) >> 3;
  }
}

// --- Engine ------------------------------------------------------------------------

void SpeechEngineFixed::Init(Allocator* allocator) {
  sam_speech_synth_.Init();
  naive_speech_synth_.Init();
  word_bank_.Init(allocator);
  lpc_speech_synth_controller_.Init(&word_bank_);
  word_bank_quantized_ = 0;
  temp_buffer_[0] = allocator->Allocate<int32_t>(kMaxBlockSize);
  temp_buffer_[1] = allocator->Allocate<int32_t>(kMaxBlockSize);
  prosody_amount_ = 0;
  speed_ = 0;
}

void SpeechEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                               size_t size, bool* already_enveloped) {
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const int32_t timbre = Q15ToQ31(parameters.timbre);
  const int32_t morph = Q15ToQ31(parameters.morph);
  // group = harmonics * 6, Q16.
  const int32_t group = parameters.harmonics * 12;
  const bool rising_edge_only = parameters.trigger == TRIGGER_RISING_EDGE;

  if (group <= (2 << 16)) {
    *already_enveloped = false;
    int32_t blend = group;   // Q16, 0..2
    if (group <= (1 << 16)) {
      naive_speech_synth_.Render(rising_edge_only, f0, morph, timbre, aux, out, size);
    } else {
      lpc_speech_synth_controller_.Render(
          parameters.trigger & TRIGGER_UNPATCHED, parameters.trigger & TRIGGER_RISING_EDGE, -1,
          f0, 0, 0, morph, timbre, INT32_MAX, aux, out, size);
      blend = (2 << 16) - blend;
    }
    sam_speech_synth_.Render(rising_edge_only, f0, morph, timbre, temp_buffer_[0],
                             temp_buffer_[1], size);
    // blend = smoothstep(smoothstep(blend)), Q31.
    int32_t b = blend >= (1 << 16) ? INT32_MAX : blend << 15;
    for (int k = 0; k < 2; ++k) {
      const int32_t b2 = MulQ31(b, b);
      b = MulQ31(b2, INT32_MAX - b) + MulQ31(b2, INT32_MAX - b) + b2;   // b^2 (3 - 2 b)
    }
    for (size_t i = 0; i < size; ++i) {
      aux[i] += Mul(temp_buffer_[0][i] - aux[i], b);
      out[i] += Mul(temp_buffer_[1][i] - out[i], b);
    }
  } else {
    // Change phonemes / words for LPC: HysteresisQuantizer2(6, 0.1, false)
    // on (group - 2) * 0.275.
    const int32_t value = Mul64(group - (2 << 16), Q(0.275 * 6, 16), 16) - (1 << 15);   // Q16
    const int32_t hysteresis = value > (word_bank_quantized_ << 16) ? -Q(0.1, 16) : Q(0.1, 16);
    const int32_t x = value + hysteresis + (1 << 15);
    int q = x >= 0 ? (x >> 16) : -((-x) >> 16);
    word_bank_quantized_ = Clip(q, 0, kLPCSpeechSynthNumWordBanks);
    const int word_bank = word_bank_quantized_ - 1;
    const bool replay_prosody = word_bank >= 0 && !(parameters.trigger & TRIGGER_UNPATCHED);
    *already_enveloped = replay_prosody;
    lpc_speech_synth_controller_.Render(
        parameters.trigger & TRIGGER_UNPATCHED, parameters.trigger & TRIGGER_RISING_EDGE,
        word_bank, f0, prosody_amount_, speed_, morph, timbre,
        replay_prosody ? Q15ToQ31(parameters.accent) : INT32_MAX, aux, out, size);
  }
  for (size_t i = 0; i < size; ++i) {
    out[i] = ShlSat(out[i], kAudioBits - 23);
    aux[i] = ShlSat(aux[i], kAudioBits - 23);
  }
}

}  // namespace op1plaits
