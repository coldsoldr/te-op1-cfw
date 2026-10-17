#include "fixed/wavetable_engine.h"

#include "fixed/dsp.h"

namespace op1plaits {

namespace {

const int kNumWavesPerBank = 64;
const int kTableStride = 132;   // 128 samples + 4 for the interpolation

// Clamp(x, amount): (x - 0.5) * amount clamped to [-0.5, 0.5], + 0.5 (Q24).
inline int32_t ClampFraction16(int32_t x) {
  int32_t y = (x - (1 << 23)) * 16;
  y = Clip(y, -(1 << 23), 1 << 23);
  return y + (1 << 23);
}

// InterpolateWaveHermite on the int16 integrated waves: table[i .. i + 3],
// f Q31. Arithmetic in Q12 of the int16 scale (intermediates < 2^30).
inline int32_t ReadWave(int wave, int32_t p_integral, int32_t f) {
  const int16_t* t = wav_integrated_waves + wave * kTableStride + p_integral;
  const int32_t xm1 = t[0] << 12;
  const int32_t x0 = t[1] << 12;
  const int32_t x1 = t[2] << 12;
  const int32_t x2 = t[3] << 12;
  const int32_t c = (x1 - xm1) >> 1;
  const int32_t v = x0 - x1;
  const int32_t w = c + v;
  const int32_t a = w + v + ((x2 - x0) >> 1);
  const int32_t b_neg = w + a;
  return Mul(Mul(Mul(a, f) - b_neg, f) + c, f) + x0;
}

inline int WaveIndex(int x, int y, int z) {
  return wavetable_wave_map[x + y * 8 + z * kNumWavesPerBank];
}

// gain = (0.95 - f0) / (f0 * 131072), Q35 (saturates below f0 ~ 1.7e-4).
// f0 = x 2^-n with x in [0.5, 1): 1 / (f0 2^17) = (1 / x) 2^(n - 17).
inline int32_t Gain(uint32_t f0) {
  if (f0 < 700000u) return INT32_MAX;   // f0 < 1.63e-4: n <= 12
  const int n = CountLeadingZeros(f0);
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>((f0 << n) >> 1));   // Q30 of 1 / x
  const int32_t reciprocal = r >> (12 - n);                                     // Q35 of 1 / (f0 2^17)
  return Mul(reciprocal, Q31(0.95) - static_cast<int32_t>(f0 >> 1));
}

}  // namespace

void WavetableEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  phase_ = 0;
  x_lp_ = y_lp_ = z_lp_ = 0;
  x_pre_lp_ = y_pre_lp_ = z_pre_lp_ = 0;
  previous_x_ = previous_y_ = previous_z_ = 0;
  previous_f0_ = static_cast<int32_t>(NoteToFrequency(33 << 16));   // a0
  diff_previous_ = 0;
  diff_lp_ = 0;
}

void WavetableEngineFixed::Render(const FixedParameters& parameters, int32_t* out,
                                  int32_t* aux, size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);

  // ONE_POLE(x_pre_lp_, timbre * 6.9999, 0.2) etc. (Q24).
  OnePole(x_pre_lp_, Mul64(parameters.timbre, Q(6.9999, 24), 15), Q31(0.2));
  OnePole(y_pre_lp_, Mul64(parameters.morph, Q(6.9999, 24), 15), Q31(0.2));
  OnePole(z_pre_lp_, Mul64(parameters.harmonics, Q(6.9999, 24), 15), Q31(0.05));

  const int32_t x = x_pre_lp_;
  const int32_t y = y_pre_lp_;
  const int32_t z = z_pre_lp_;

  const int32_t quantization = Clip(z - (3 << 24), 0, 1 << 24);   // Q24
  // lp_coefficient = clamp(2 f0 (4 - 3 quantization), 0.01, 0.1); 2 f0 in
  // Q31 is f0 in Q32.
  int32_t lp_coefficient = Mul64Sat(static_cast<int32_t>(f0 > 0x7fffffffu ? 0x7fffffffu : f0),
                                    (4 << 24) - 3 * quantization, 24);
  lp_coefficient = Clip(lp_coefficient, Q31(0.01), Q31(0.1));

  int32_t xi = x >> 24, xf = x & 0xffffff;
  int32_t yi = y >> 24, yf = y & 0xffffff;
  int32_t zi = z >> 24, zf = z & 0xffffff;
  xf += Mul64(quantization, ClampFraction16(xf) - xf, 24);
  yf += Mul64(quantization, ClampFraction16(yf) - yf, 24);
  zf += Mul64(quantization, ClampFraction16(zf) - zf, 24);

  Interpolator x_modulation(&previous_x_, (xi << 24) + xf, size);
  Interpolator y_modulation(&previous_y_, (yi << 24) + yf, size);
  Interpolator z_modulation(&previous_z_, (zi << 24) + zf, size);
  Interpolator f0_modulation(&previous_f0_, static_cast<int32_t>(f0), size);
  // The gain follows the interpolated f0 per sample, as upstream (a
  // reciprocal per sample); while the note holds it's computed once.
  const bool gliding = f0_modulation.increment() != 0;
  const int32_t steady_gain = Gain(f0);

  uint32_t phase = phase_;
  int32_t diff_previous = diff_previous_;
  int32_t diff_lp = diff_lp_;
  while (size--) {
    const uint32_t f = static_cast<uint32_t>(f0_modulation.Next());
    const int32_t gain = gliding ? Gain(f) : steady_gain;
    // cutoff = min(128 f0, 1), Q31.
    const int32_t cutoff = f >= (1u << 25) ? INT32_MAX : static_cast<int32_t>(f << 6);

    OnePole(x_lp_, x_modulation.Next(), lp_coefficient);
    OnePole(y_lp_, y_modulation.Next(), lp_coefficient);
    OnePole(z_lp_, z_modulation.Next(), lp_coefficient);
    // A rounded ramp may dip a few LSB below 0: keep the indices in range.
    if (x_lp_ < 0) x_lp_ = 0;
    if (y_lp_ < 0) y_lp_ = 0;
    if (z_lp_ < 0) z_lp_ = 0;

    const int32_t x_integral = x_lp_ >> 24;
    const int32_t x_fractional = (x_lp_ & 0xffffff) << 7;   // Q31
    const int32_t y_integral = y_lp_ >> 24;
    const int32_t y_fractional = (y_lp_ & 0xffffff) << 7;
    const int32_t z_integral = z_lp_ >> 24;
    const int32_t z_fractional = (z_lp_ & 0xffffff) << 7;

    phase += f;
    const int32_t p_integral = static_cast<int32_t>(phase >> 25);
    const int32_t p_fractional = static_cast<int32_t>((phase << 7) >> 1);   // Q31

    const int x0 = x_integral;
    const int x1 = x_integral + 1;
    const int y0 = y_integral;
    const int y1 = y_integral + 1;
    int z0 = z_integral;
    int z1 = z_integral + 1;
    if (z0 >= 4) z0 = 7 - z0;
    if (z1 >= 4) z1 = 7 - z1;

    const int32_t x0y0z0 = ReadWave(WaveIndex(x0, y0, z0), p_integral, p_fractional);
    const int32_t x1y0z0 = ReadWave(WaveIndex(x1, y0, z0), p_integral, p_fractional);
    const int32_t xy0z0 = x0y0z0 + Mul(x1y0z0 - x0y0z0, x_fractional);
    const int32_t x0y1z0 = ReadWave(WaveIndex(x0, y1, z0), p_integral, p_fractional);
    const int32_t x1y1z0 = ReadWave(WaveIndex(x1, y1, z0), p_integral, p_fractional);
    const int32_t xy1z0 = x0y1z0 + Mul(x1y1z0 - x0y1z0, x_fractional);
    const int32_t xyz0 = xy0z0 + Mul(xy1z0 - xy0z0, y_fractional);
    const int32_t x0y0z1 = ReadWave(WaveIndex(x0, y0, z1), p_integral, p_fractional);
    const int32_t x1y0z1 = ReadWave(WaveIndex(x1, y0, z1), p_integral, p_fractional);
    const int32_t xy0z1 = x0y0z1 + Mul(x1y0z1 - x0y0z1, x_fractional);
    const int32_t x0y1z1 = ReadWave(WaveIndex(x0, y1, z1), p_integral, p_fractional);
    const int32_t x1y1z1 = ReadWave(WaveIndex(x1, y1, z1), p_integral, p_fractional);
    const int32_t xy1z1 = x0y1z1 + Mul(x1y1z1 - x0y1z1, x_fractional);
    const int32_t xyz1 = xy0z1 + Mul(xy1z1 - xy0z1, y_fractional);
    const int32_t mix = xyz0 + Mul(xyz1 - xyz0, z_fractional);   // Q12

    // Differentiator: lp += cutoff ((s - previous) - lp).
    diff_lp += Mul((mix - diff_previous) - diff_lp, cutoff);
    diff_previous = mix;
    // lp (Q12) * gain (Q35) -> Q18 -> Q26.
    const int32_t s = Mul(diff_lp << 2, gain) << (kAudioBits - 18);
    *out++ = s;
    // static_cast<int>(mix * 32) / 32: truncation towards zero to 1/32.
    const int32_t kStep = kAudioBits - 5;
    *aux++ = s >= 0 ? (s >> kStep) << kStep : -(((-s) >> kStep) << kStep);
  }
  phase_ = phase;
  diff_previous_ = diff_previous;
  diff_lp_ = diff_lp;
}

}  // namespace op1plaits
