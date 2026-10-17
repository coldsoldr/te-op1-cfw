#include "fixed/chord_engine.h"

#include "fixed/dsp.h"

namespace op1plaits {

namespace {

const int32_t kFadePoint[kChordNumVoices] = {
  Q31(0.55), Q31(0.47), Q31(0.49), Q31(0.51), Q31(0.53),
};

const int kRegistrationTableSize = 8;
const int32_t kRegistrations[kRegistrationTableSize][kChordNumHarmonics * 2] = {
  { 0, INT32_MAX, 0, 0, 0, 0 },                                  // Square
  { INT32_MAX, 0, 0, 0, 0, 0 },                                  // Saw
  { Q31(0.5), 0, Q31(0.5), 0, 0, 0 },                            // Saw + saw
  { Q31(0.33), 0, Q31(0.33), 0, Q31(0.33), 0 },                  // Full saw
  { Q31(0.33), 0, 0, Q31(0.33), 0, Q31(0.33) },                  // Full saw + square hybrid
  { Q31(0.5), 0, 0, 0, 0, Q31(0.5) },                            // Saw + high square harmo
  { 0, Q31(0.5), 0, 0, 0, Q31(0.5) },                            // Square + high square harmo
  { 0, Q31(0.1), Q31(0.1), 0, Q31(0.2), Q31(0.6) },              // Saw+square + high harmo
};

#define WAVE(bank, row, column) &wav_integrated_waves[(bank * 64 + row * 8 + column) * 132]
const int16_t* const kWavetable[] = {
  WAVE(2, 6, 1), WAVE(2, 6, 6), WAVE(2, 6, 4), WAVE(0, 6, 0), WAVE(0, 6, 1),
  WAVE(0, 6, 2), WAVE(0, 6, 7), WAVE(2, 4, 7), WAVE(2, 4, 6), WAVE(2, 4, 5),
  WAVE(2, 4, 4), WAVE(2, 4, 3), WAVE(2, 4, 2), WAVE(2, 4, 1), WAVE(2, 4, 0),
};
#undef WAVE

// registration (Q31, 0..1) * (8 - 1.001): integral and fractional parts,
// then the lerp between two rows of the table.
void ComputeRegistration(int32_t registration, int32_t* amplitudes) {
  const int32_t r = Mul64(registration, Q(kRegistrationTableSize - 1.001, 24), 31);   // Q24
  const int32_t integral = r >> 24;
  const int32_t fractional = (r & 0xffffff) << 7;
  for (int i = 0; i < kChordNumHarmonics * 2; ++i) {
    const int32_t a = kRegistrations[integral][i];
    const int32_t b = kRegistrations[integral + 1][i];
    amplitudes[i] = a + MulQ31(b - a, fractional);
  }
}

inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

}  // namespace

void ChordEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  for (int i = 0; i < kChordNumVoices; ++i) {
    divide_down_voice_[i].Init();
    wavetable_voice_[i].Init();
  }
  chord_index_ = 0;
  morph_lp_ = 0;
  timbre_lp_ = 0;
}

int ChordEngineFixed::ComputeChordInversion(int32_t inversion, int32_t* ratios,
                                            int32_t* amplitudes) const {
  const int32_t* base_ratio = &chord_ratios_q28[chord_index_ * kChordNumNotes];
  // inversion * kChordNumNotes * kChordNumVoices (20), Q24.
  const int32_t x = Mul64(inversion, 20, 7);
  const int32_t inversion_integral = x >> 24;
  const int32_t inversion_fractional = (x & 0xffffff) << 7;   // Q31

  const int num_rotations = inversion_integral / kChordNumNotes;
  const int rotated_note = inversion_integral % kChordNumNotes;
  const int32_t kBaseGain = Q31(0.25);

  int mask = 0;
  for (int i = 0; i < kChordNumNotes; ++i) {
    // transposition = 0.25 * 2^((3 + integral - i) / 4); ratios in Q24.
    const int e = (kChordNumNotes - 1 + inversion_integral - i) / kChordNumNotes;
    // (i - num_rotations + 5) % 5 and (target - 1 + 5) % 5, without libgcc's
    // division (Blackfin has no divide instruction).
    int target_voice = i - num_rotations + kChordNumVoices;
    if (target_voice >= kChordNumVoices) target_voice -= kChordNumVoices;
    const int previous_voice = target_voice == 0 ? kChordNumVoices - 1 : target_voice - 1;
    const int32_t ratio = base_ratio[i] >> (6 - e);
    if (i == rotated_note) {
      ratios[target_voice] = ratio;
      ratios[previous_voice] = ratio * 2;
      amplitudes[previous_voice] = MulQ31(kBaseGain, inversion_fractional);
      amplitudes[target_voice] = MulQ31(kBaseGain, INT32_MAX - inversion_fractional);
    } else if (i < rotated_note) {
      ratios[previous_voice] = ratio;
      amplitudes[previous_voice] = kBaseGain;
    } else {
      ratios[target_voice] = ratio;
      amplitudes[target_voice] = kBaseGain;
    }
    if (i == 0) {
      if (i >= rotated_note) mask |= 1 << target_voice;
      if (i <= rotated_note) mask |= 1 << previous_voice;
    }
  }
  return mask;
}

void ChordEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                              size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  OnePole(morph_lp_, Q15ToQ31(parameters.morph), Q31(0.1));
  OnePole(timbre_lp_, Q15ToQ31(parameters.timbre), Q31(0.1));

  // chords_.set_chord(harmonics): HysteresisQuantizer2(11, 0.075, false) on
  // harmonics * 1.02 (Q16 arithmetic).
  {
    const int32_t value = Mul64(parameters.harmonics, Q(1.02 * kChordNumChords, 16), 15) - (1 << 15);
    const int32_t hysteresis = value > (chord_index_ << 16) ? -Q(0.075, 16) : Q(0.075, 16);
    const int32_t x = value + hysteresis + (1 << 15);
    int q = x >= 0 ? (x >> 16) : -((-x) >> 16);
    chord_index_ = Clip(q, 0, kChordNumChords - 1);
  }

  int32_t harmonics[kChordNumHarmonics * 2 + 2];
  int32_t note_amplitudes[kChordNumVoices];
  // registration = max(1 - 2.15 morph_lp, 0).
  const int32_t registration = morph_lp_ >= Q31(1.0 / 2.15)
      ? 0 : INT32_MAX - (Mul(morph_lp_, Q31(2.15 / 4)) << 2);
  ComputeRegistration(registration, harmonics);
  harmonics[kChordNumHarmonics * 2] = 0;

  int32_t ratios[kChordNumVoices];
  const int aux_note_mask = ComputeChordInversion(timbre_lp_, ratios, note_amplitudes);

  int32_t* sum_out = out;
  int32_t* sum_aux = aux;
  for (size_t i = 0; i < size; ++i) sum_out[i] = sum_aux[i] = 0;   // Q28 accumulators

  // f0 * 0.998.
  const uint32_t f0 = static_cast<uint32_t>(
      (static_cast<uint64_t>(NoteToFrequency(parameters.note)) * Q31(0.998)) >> 31);
  // waveform = max((morph_lp - 0.535) * 2.15, 0).
  int32_t waveform = morph_lp_ - Q31(0.535);
  waveform = waveform <= 0 ? 0 : static_cast<int32_t>((static_cast<int64_t>(waveform) * Q(2.15, 16)) >> 16);

  for (int note = 0; note < kChordNumVoices; ++note) {
    // wavetable_amount = clamp(50 (morph_lp - fade_point), 0, 1).
    int64_t wt = (static_cast<int64_t>(morph_lp_) - kFadePoint[note]) * 50;
    const int32_t wavetable_amount = wt <= 0 ? 0 : (wt >= INT32_MAX ? INT32_MAX : static_cast<int32_t>(wt));
    int32_t divide_down_amount = INT32_MAX - wavetable_amount;
    int32_t* destination = (1 << note) & aux_note_mask ? sum_aux : sum_out;

    uint64_t nf = (static_cast<uint64_t>(f0) * static_cast<uint32_t>(ratios[note])) >> 24;
    const uint32_t note_f0 = nf > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(nf);
    // divide_down_gain = clamp(4 - 32 note_f0, 0, 1): 32 f in Q27 is f's Q32.
    int64_t dd = (static_cast<int64_t>(4) << 27) - static_cast<int64_t>(nf);
    dd = dd < 0 ? 0 : (dd > (1 << 27) ? (1 << 27) : dd);
    const int32_t divide_down_gain = dd >= (1 << 27) ? INT32_MAX : static_cast<int32_t>(dd << 4);
    divide_down_amount = Mul(divide_down_amount, divide_down_gain);

    if (wavetable_amount) {
      const uint32_t wf = static_cast<uint32_t>(
          (static_cast<uint64_t>(note_f0) * Q(1.004, 30)) >> 30 > 0xffffffffu
              ? 0xffffffffu
              : (static_cast<uint64_t>(note_f0) * Q(1.004, 30)) >> 30);
      wavetable_voice_[note].Render(
          wf, Mul(note_amplitudes[note], wavetable_amount), waveform, kWavetable,
          destination, size);
    }
    if (divide_down_amount) {
      divide_down_voice_[note].Render(
          note_f0, harmonics, Mul(note_amplitudes[note], divide_down_amount), destination, size);
    }
  }

  for (size_t i = 0; i < size; ++i) {
    const int32_t a = sum_aux[i];
    out[i] = (sum_out[i] + a) >> (28 - kAudioBits);
    aux[i] = (a >> (28 - kAudioBits)) * 3;
  }
}

}  // namespace op1plaits
