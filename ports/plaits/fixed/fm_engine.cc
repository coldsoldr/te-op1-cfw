#include "fixed/fm_engine.h"

namespace op1plaits {

namespace {

const int kOversampling = 4;

// SinePM: upstream adds floor((pm + 32) * 2^26) * 64 to the phase, i.e. pm
// (in cycles) with 2^-26 resolution; pm_q26 is pm in Q26.
inline int32_t SinePM(uint32_t phase, int32_t pm_q26) {
  return Sine(phase + (static_cast<uint32_t>(pm_q26) << 6));
}

}  // namespace

void FMEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  sub_phase_ = 0;
  const int32_t a0 = static_cast<int32_t>(NoteToFrequency(33 << 16));   // = a0
  previous_carrier_frequency_ = a0;
  previous_modulator_frequency_ = a0;
  previous_amount_ = 0;
  previous_feedback_ = 0;
  previous_sample_ = 0;
  sub_fir_ = 0;
  carrier_fir_ = 0;
}

void FMEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                           size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  // 4x oversampling: two octaves down.
  const int32_t note = parameters.note - (24 << 16);

  // Interpolate(lut_fm_frequency_quantizer, harmonics, 128).
  const int32_t index = parameters.harmonics << 7;   // Q15
  const int32_t i = index >> 15;
  const int32_t a = lut_fm_frequency_quantizer_q16[i];
  const int32_t b = lut_fm_frequency_quantizer_q16[i + 1];
  const int32_t ratio = a + Mul64(b - a, index & 0x7fff, 15);

  const int32_t modulator_note = note + ratio;
  uint32_t target_modulator_frequency = NoteToFrequency(modulator_note);
  if (target_modulator_frequency > 0x7fffffffu) target_modulator_frequency = 0x7fffffffu;

  // Reduce the maximum FM index for high pitched notes, to prevent aliasing.
  int32_t hf_taming = 65536 - Mul64(modulator_note - (72 << 16), Q31(0.025), 31);   // Q16
  hf_taming = Clip(hf_taming, 0, 65536);
  hf_taming = Mul64(hf_taming, hf_taming, 16);

  Interpolator carrier_frequency(
      &previous_carrier_frequency_, static_cast<int32_t>(NoteToFrequency(note)), size);
  Interpolator modulator_frequency(
      &previous_modulator_frequency_, static_cast<int32_t>(target_modulator_frequency), size);
  // 2 timbre^2 hf_taming, Q29.
  const int32_t timbre_2 = MulQ15(parameters.timbre, parameters.timbre);
  Interpolator amount_modulation(&previous_amount_, Mul64(timbre_2, hf_taming, 1), size);
  // 2 morph - 1, Q31.
  int32_t feedback = (2 * parameters.morph - kQ15One);
  feedback = feedback >= kQ15One ? INT32_MAX : feedback << 16;
  Interpolator feedback_modulation(&previous_feedback_, feedback, size);

  // lut_4x_downsampler_fir in Q15 (16 x 16 multiplies).
  static const int16_t fir[4] = {
    Q(0.02442415, 15), Q(0.09297315, 15), Q(0.16712938, 15), Q(0.21547332, 15),
  };
  int32_t carrier_head = carrier_fir_;
  int32_t sub_head = sub_fir_;
  int32_t previous_sample = previous_sample_;
  uint32_t modulator_phase = modulator_phase_;
  uint32_t carrier_phase = carrier_phase_;
  uint32_t sub_phase = sub_phase_;

  while (size--) {
    const int32_t amount = amount_modulation.Next();
    const int32_t fb = feedback_modulation.Next();
    const int32_t fb_2 = Mul(fb, fb);
    const int32_t phase_feedback = fb < 0 ? fb_2 >> 1 : 0;    // Q31
    const int32_t modulator_fb = fb > 0 ? fb_2 >> 2 : 0;       // Q31
    const uint32_t carrier_increment = static_cast<uint32_t>(carrier_frequency.Next());
    const int32_t mf = modulator_frequency.Next();
    int32_t carrier_tail = 0;
    int32_t sub_tail = 0;
    for (int j = 0; j < kOversampling; ++j) {
      const int32_t x = Mul(previous_sample, phase_feedback) << 1;   // Q31
      modulator_phase += static_cast<uint32_t>(mf + Mul(mf, x));
      carrier_phase += carrier_increment;
      sub_phase += carrier_increment >> 1;
      const int32_t modulator = SinePM(
          modulator_phase, Mul(modulator_fb, previous_sample) >> 4);
      const int32_t carrier = SinePM(carrier_phase, Mul(amount, modulator) >> 3);
      const int32_t sub = SinePM(sub_phase, Mul(amount, carrier) >> 5);
      previous_sample += Mul((carrier >> 1) - previous_sample, Q31(0.05));
      carrier_head += Mul16(carrier, fir[3 - j]) >> 1;
      carrier_tail += Mul16(carrier, fir[j]) >> 1;
      sub_head += Mul16(sub, fir[3 - j]) >> 1;
      sub_tail += Mul16(sub, fir[j]) >> 1;
    }
    *out++ = carrier_head >> (30 - kAudioBits);
    *aux++ = sub_head >> (30 - kAudioBits);
    carrier_head = carrier_tail;
    sub_head = sub_tail;
  }
  carrier_fir_ = carrier_head;
  sub_fir_ = sub_head;
  previous_sample_ = previous_sample;
  modulator_phase_ = modulator_phase;
  carrier_phase_ = carrier_phase;
  sub_phase_ = sub_phase;
}

}  // namespace op1plaits
