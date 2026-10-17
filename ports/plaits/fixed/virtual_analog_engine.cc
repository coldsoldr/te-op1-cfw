#include "fixed/virtual_analog_engine.h"

namespace op1plaits {

namespace {

const int32_t kIntervals[5] = {
  0, Q(7.01, 16), Q(12.01, 16), Q(19.01, 16), Q(24.01, 16),
};

// x^2 (3 - 2 x), Q30 in and out.
inline int32_t Squash(int32_t x) {
  const int32_t x2 = MulQ31(x, x) << 1;            // Q30
  const int32_t t = (3 << 28) - (x >> 1);          // 3 - 2 x, Q28
  return MulQ31(x2, t) << 3;                       // Q27 -> Q30
}

// Q15 parameter -> Q31 (1.0 -> INT32_MAX).
inline int32_t Q15ToQ31(int32_t x) {
  return x >= kQ15One ? INT32_MAX : (x <= 0 ? 0 : x << 16);
}

}  // namespace

int32_t ComputeDetuning(int32_t detune) {
  // detune = 2.05 detune - 1.025, clamped to [-1, 1] (Q16).
  int32_t d = Mul64(detune, Q(2.05, 16), 15) - Q(1.025, 16);
  d = Clip(d, -65536, 65536);
  const bool negative = d < 0;
  d = Mul64(negative ? -d : d, Q(3.9999, 16), 16);
  const int32_t integral = d >> 16;
  const int32_t fractional = (d & 0xffff) << 14;   // Q30
  const int32_t a = kIntervals[integral];
  const int32_t b = kIntervals[integral + 1];
  const int32_t r = a + Mul64(b - a, Squash(Squash(fractional)), 30);
  return negative ? -r : r;
}

void VirtualAnalogEngineFixed::Init(Allocator* allocator) {
  primary_.Init();
  auxiliary_.Init();
  auxiliary_.set_master_phase(1u << 29);   // 0.25
  sync_.Init();
  variable_saw_.Init();
  auxiliary_amount_ = 0;
  xmod_amount_ = 0;
  temp_buffer_ = allocator->Allocate<int32_t>(kMaxBlockSize);
}

void VirtualAnalogEngineFixed::Render(const FixedParameters& parameters, int32_t* out,
                                      int32_t* aux, size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const int32_t note = parameters.note;
  const int32_t timbre = parameters.timbre;
  const int32_t morph = parameters.morph;
  const int32_t sync_amount = MulQ15(timbre, timbre);   // Q15
  const int32_t auxiliary_detune = ComputeDetuning(parameters.harmonics);
  const uint32_t primary_f = NoteToFrequency(note);
  const uint32_t auxiliary_f = NoteToFrequency(note + auxiliary_detune);
  const uint32_t primary_sync_f = NoteToFrequency(note + sync_amount * 96);   // * 48, Q16
  const uint32_t auxiliary_sync_f = NoteToFrequency(note + auxiliary_detune + sync_amount * 96);

  const int32_t morph_q31 = Q15ToQ31(morph);
  const int32_t timbre_q31 = Q15ToQ31(timbre);

  // shape = clamp(1.5 morph, 0, 1).
  const int32_t shape = Q15ToQ31((morph * 3) >> 1);
  // pw = clamp(0.5 + (morph - 0.66) 1.46, 0.5, 0.995), Q31.
  int32_t pw = (1 << 30) + (Mul(morph_q31 - Q31(0.66), Q31(1.46 / 2)) << 1);
  pw = Clip(pw, 1 << 30, Q31(0.995));

  // Render monster sync to AUX.
  primary_.Render<true>(primary_f, primary_sync_f, static_cast<uint32_t>(pw), shape, out, size);
  auxiliary_.Render<true>(auxiliary_f, auxiliary_sync_f, static_cast<uint32_t>(pw), shape, aux, size);
  for (size_t i = 0; i < size; ++i) {
    aux[i] = (aux[i] >> 1) - (out[i] >> 1);   // the difference can reach 2
  }

  // Render double varishape to OUT.
  // square_pw = clamp(1.3 timbre - 0.15, 0.005, 0.5): 0.65 t in Q31 is 1.3 t
  // in Q30.
  int32_t square_pw = Mul(timbre_q31, Q31(0.65)) - Q(0.15, 30);
  square_pw = Clip(square_pw, Q(0.005, 30), 1 << 29) << 1;   // Q31

  int32_t square_sync_ratio = 0;   // Q16 semitones
  if (timbre >= kQ15One / 2) {
    const int32_t d = timbre - kQ15One / 2;   // Q15
    square_sync_ratio = ((d * d) >> 14) * 192;   // (t - 0.5)^2 * 4 * 48
  }
  const int32_t square_gain = timbre >= kQ15One / 8 ? INT32_MAX : timbre << 19;   // min(8 t, 1)

  // saw_pw = (morph < 0.5 ? morph + 0.5 : 1 - 2 (morph - 0.5)) * 1.1, clamped
  // to [0.005, 1].
  int32_t saw_pw = morph < kQ15One / 2
      ? (morph_q31 >> 1) + (1 << 29)                       // Q30
      : (1 << 30) - (morph_q31 - (1 << 30));              // Q30
  saw_pw = Mul(saw_pw, Q31(0.55)) << 1;                    // * 1.1, Q30
  saw_pw = Clip(saw_pw, Q(0.005, 30), 1 << 30);
  saw_pw = saw_pw >= (1 << 30) ? INT32_MAX : saw_pw << 1;  // Q31

  // saw_shape = clamp(10 - 21 morph, 0, 1); saw_gain = clamp(8 (1 - morph),
  // 0.02, 1).
  const int32_t saw_shape_q16 = Clip((10 << 16) - 42 * morph, 0, 65536);
  const int32_t saw_shape = saw_shape_q16 >= 65536 ? INT32_MAX : saw_shape_q16 << 15;
  const int32_t saw_gain_q16 = Clip(16 * (kQ15One - morph), Q(0.02, 16), 65536);
  const int32_t saw_gain = saw_gain_q16 >= 65536 ? INT32_MAX : saw_gain_q16 << 15;

  const uint32_t square_sync_f = NoteToFrequency(note + square_sync_ratio);

  sync_.Render<true>(primary_f, square_sync_f, static_cast<uint32_t>(square_pw), INT32_MAX,
                     temp_buffer_, size);
  variable_saw_.Render(auxiliary_f, static_cast<uint32_t>(saw_pw), saw_shape, out, size);

  // norm = 1 / max(square_gain, saw_gain): the larger gain becomes 1.
  int32_t square_target, saw_target;
  if (square_gain >= saw_gain) {
    square_target = Q31(0.3);
    saw_target = Mul(Fraction(static_cast<uint32_t>(saw_gain), static_cast<uint32_t>(square_gain) + 1),
                     Q31(0.5));
  } else {
    saw_target = Q31(0.5);
    square_target = Mul(Fraction(static_cast<uint32_t>(square_gain), static_cast<uint32_t>(saw_gain) + 1),
                        Q31(0.3));
  }
  Interpolator square_gain_modulation(&auxiliary_amount_, square_target, size);
  Interpolator saw_gain_modulation(&xmod_amount_, saw_target, size);

  for (size_t i = 0; i < size; ++i) {
    const int32_t s = Mul(out[i], saw_gain_modulation.Next()) +
                      Mul(square_gain_modulation.Next(), temp_buffer_[i]);
    out[i] = s >> (30 - kAudioBits);
    aux[i] >>= (30 - kAudioBits);
  }
}

}  // namespace op1plaits
