#include "fixed/waveshaping_engine.h"

#include "fixed/dsp.h"

namespace op1plaits {

namespace {

const int16_t* const kWaveshapers[6] = {
  lut_ws_inverse_tan, lut_ws_inverse_sin, lut_ws_linear, lut_ws_bump,
  lut_ws_double_bump, lut_ws_double_bump,
};

// Tame(f0, harmonics, order): (1 - (f0 h - max_f) / (0.5 - max_f))^3 with
// max_f = 0.5 / order, clamped to [0, 1]. f0 Q32, h Q16; Q24 result.
// inverse_range = 1 / (0.5 - max_f), Q24.
inline int32_t Tame(uint32_t f0, int32_t h_q16, int32_t max_f_q24, int32_t inverse_range_q24) {
  const int32_t f = Mul64(static_cast<int32_t>(f0 >> 8), h_q16, 16);   // Q24
  int32_t amount = (1 << 24) - Mul64(f - max_f_q24, inverse_range_q24, 24);
  amount = Clip(amount, 0, 1 << 24);
  return Mul64(Mul64(amount, amount, 24), amount, 24);
}

}  // namespace

void WaveshapingEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  slope_.Init();
  triangle_.Init();
  previous_shape_ = 0;
  previous_wavefolder_gain_ = 0;
  previous_overtone_gain_ = 0;
}

void WaveshapingEngineFixed::Render(const FixedParameters& parameters, int32_t* out,
                                    int32_t* aux, size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  // pw = 0.5 + 0.45 morph (Q32).
  const uint32_t pw = (1u << 31) + static_cast<uint32_t>(
      (static_cast<uint64_t>(parameters.morph) * 1932735283u) >> 15);

  // Start from bandlimited slope signal (Q30, in the output buffers).
  slope_.Render<OSCILLATOR_SHAPE_SLOPE>(f0, pw, out, size);
  // Upstream: a slope with pw = 0.5, i.e. this triangle (same arithmetic).
  triangle_.Render<OSCILLATOR_SHAPE_TRIANGLE>(f0, 1u << 31, aux, size);

  // Try to estimate how rich the spectrum is, and reduce the range of the
  // waveshaping control accordingly.
  const int32_t morph_offset = Abs(parameters.morph - kQ15One / 2);
  const int32_t slope = (3 << 16) + 10 * morph_offset;                  // Q16
  const int32_t shape_amount = 2 * Abs(parameters.harmonics - kQ15One / 2);   // Q15
  // order 16: max_f = 1/32, 1 / (0.5 - 1/32) = 32/15; order 12: 1/24, 12/5.5.
  const int32_t shape_amount_attenuation = Tame(f0, slope, Q(1.0 / 32.0, 24), Q(32.0 / 15.0, 24));
  const int32_t wavefolder_gain = parameters.timbre;
  const int32_t inner = (3 << 24) + 5 * Mul64(shape_amount, shape_amount_attenuation, 15);   // Q24
  const int32_t wavefolder_gain_attenuation = Tame(
      f0, Mul64(slope, inner, 24), Q(1.0 / 24.0, 24), Q(12.0 / 5.5, 24));

  // Apply waveshaper / wavefolder.
  Interpolator shape_modulation(
      &previous_shape_,
      (1 << 29) + Mul64(parameters.harmonics - kQ15One / 2, shape_amount_attenuation, 9),
      size);
  Interpolator wf_gain_modulation(
      &previous_wavefolder_gain_,
      Q31(0.03) + Mul(Mul64Sat(wavefolder_gain, wavefolder_gain_attenuation, 8), Q31(0.46)),
      size);
  const int32_t overtone_gain = (parameters.timbre * (2 * kQ15One - parameters.timbre)) >> 15;
  int32_t overtone_target = (overtone_gain * (2 * kQ15One - overtone_gain)) >> 15;   // Q15
  overtone_target = overtone_target >= kQ15One ? INT32_MAX : overtone_target << 16;
  Interpolator overtone_gain_modulation(&previous_overtone_gain_, overtone_target, size);

  for (size_t i = 0; i < size; ++i) {
    // shape * 3.9999 in Q28: integral and fractional parts.
    int32_t shape = Mul(shape_modulation.Next(), Q31(3.9999 / 4.0));
    if (shape < 0) shape = 0;   // a rounded ramp may dip a few LSB below 0
    const int32_t shape_integral = shape >> 28;
    const int32_t shape_fractional = (shape & ((1 << 28) - 1)) >> 13;   // Q15
    const int16_t* shape_1 = kWaveshapers[shape_integral];
    const int16_t* shape_2 = kWaveshapers[shape_integral + 1];

    // ws_index = 127 out + 128, Q16; upstream's integral/fractional split
    // truncates towards zero (the fraction is negative below 0).
    const int32_t ws_index = (out[i] >> 14) * 127 + (128 << 16);
    int32_t ws_integral = ws_index >= 0 ? ws_index >> 16 : -((-ws_index) >> 16);
    const int32_t ws_fractional = (ws_index - (ws_integral << 16)) >> 1;   // Q15, signed
    ws_integral &= 255;

    // The folder after this has a gain of a few hundred, so the shaper's
    // output keeps 23 fractional bits (the tables are Q15).
    const int32_t x0 = shape_1[ws_integral];
    const int32_t x = (x0 << 8) + (((shape_1[ws_integral + 1] - x0) * ws_fractional) >> 7);
    const int32_t y0 = shape_2[ws_integral];
    const int32_t y = (y0 << 8) + (((shape_2[ws_integral + 1] - y0) * ws_fractional) >> 7);
    const int32_t mix = x + Mul(y - x, shape_fractional << 16);   // Q23

    // index = mix * gain + 0.5, Q30 (in (0, 1)).
    const int32_t index = Mul(mix << 7, wf_gain_modulation.Next()) + (1 << 29);
    const int32_t index_integral = index >> 21;               // * 512
    const int32_t index_fractional = (index & 0x1fffff) << 10;  // Q31
    const int32_t fold = InterpolateHermiteQ28(lut_fold_q30 + index_integral, index_fractional);
    const int32_t fold_2 = -InterpolateHermiteQ28(lut_fold_2_q30 + index_integral, index_fractional);

    const int32_t sine = Sine(static_cast<uint32_t>(aux[i]) + (1u << 31)) >> 3;   // Q28
    out[i] = fold >> (28 - kAudioBits);
    aux[i] = (sine + Mul(fold_2 - sine, overtone_gain_modulation.Next())) >> (28 - kAudioBits);
  }
}

}  // namespace op1plaits
