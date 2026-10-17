#include "fixed/additive_engine.h"

namespace op1plaits {

template<int first_harmonic_index>
void HarmonicOscillatorFixed::Render(uint32_t frequency, const int32_t* amplitudes,
                                     int32_t* out, size_t size) {
  if (frequency > (1u << 31)) frequency = 1u << 31;
  int32_t value[kHarmonicBatchSize];
  int32_t increment[kHarmonicBatchSize];
  for (int i = 0; i < kHarmonicBatchSize; ++i) {
    uint64_t f = static_cast<uint64_t>(frequency) * (first_harmonic_index + i);
    if (f > (1u << 31)) f = 1u << 31;
    // amplitude * (1 - 2 f): 2 f in Q31 is f in Q32.
    const int32_t target = Mul(amplitudes[i], static_cast<int32_t>(0x7fffffffu - static_cast<uint32_t>(f)));
    value[i] = amplitude_[i];
    increment[i] = DivideBySize(target - amplitude_[i], size);
    amplitude_[i] = target;
  }
  Interpolator fm(&frequency_, static_cast<int32_t>(frequency == (1u << 31) ? 0x7fffffffu : frequency),
                  size);
  uint32_t phase = phase_;
  while (size--) {
    phase += static_cast<uint32_t>(fm.Next());
    const int32_t two_x = Sine(phase);   // 2 sin, Q30
    int32_t previous, current;          // Q29
    if (first_harmonic_index == 1) {
      previous = 1 << 29;
      current = two_x >> 2;   // sin, Q29
    } else {
      const uint32_t k = first_harmonic_index;
      previous = Sine(phase * (k - 1) + (1u << 30)) >> 2;
      current = Sine(phase * k) >> 2;
    }
    int32_t sum = 0;
    for (int i = 0; i < kHarmonicBatchSize; ++i) {
      value[i] += increment[i];
      sum += Mul(value[i], current);
      const int32_t temp = current;
      current = (Mul(two_x, current) << 1) - previous;
      previous = temp;
    }
    if (first_harmonic_index == 1) {
      *out++ = sum;
    } else {
      *out++ += sum;
    }
  }
  phase_ = phase;
}

namespace {

const int kIntegerHarmonics[24] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
};
const int kOrganHarmonics[8] = { 0, 1, 2, 3, 5, 7, 9, 11 };

inline int32_t Q27ToQ31(int32_t x) {
  return x >= (1 << 27) ? INT32_MAX : (x <= -(1 << 27) ? INT32_MIN : x << 4);
}

}  // namespace

void AdditiveEngineFixed::Init(Allocator* allocator) {
  (void)allocator;
  for (int i = 0; i < kNumHarmonicOscillators; ++i) harmonic_oscillator_[i].Init();
  Reset();
}

// centroid: Q15; slope: Q30; bumps: Q16. amplitudes: Q27.
void AdditiveEngineFixed::UpdateAmplitudes(int32_t centroid, int32_t slope, int32_t bumps,
                                           int32_t* amplitudes, const int* harmonic_indices,
                                           int num_harmonics) {
  static constexpr FFloat kOne(1.0);
  const int32_t n = (num_harmonics - 1) << 16;   // Q16
  // margin = (1 / slope - 1) / (1 + bumps), Q16.
  const FFloat s = FFloat::FromQ(slope, 30);
  const FFloat margin_f = (FFloat::Reciprocal(s) - kOne) / (kOne + FFloat::FromQ(bumps, 16));
  const int32_t margin = margin_f.ToQ(16);
  const int32_t center = Mul64(centroid, n + margin, 15) - (margin >> 1);

  int32_t sum = Q(0.001, 24);
  for (int i = 0; i < num_harmonics; ++i) {
    const int32_t order = Mul64(Abs((i << 16) - center), slope, 30);   // Q16
    int32_t gain = 0;   // 0.001 gain, Q27
    if (order < (1 << 16)) {
      // gain = (2 (1 - order))^2 (1 + sin(0.25 + order bumps)), squared twice;
      // 0.001 gain = (0.1778 * that before the two squarings)^4.
      const int32_t x = ((1 << 16) - order) << 14;                        // 2 (1 - order), Q29
      const int32_t x2 = Mul(x, x);                                       // Q27
      const uint32_t b = static_cast<uint32_t>(order) * static_cast<uint32_t>(bumps) + (1u << 30);
      const int32_t bump_factor = (1 << 30) + (Sine(b) >> 1);              // Q30
      const int32_t y = Mul(x2, bump_factor);                             // Q26 (<= 8)
      const int32_t z = Mul(y << 1, Q31(0.17782794100389228));            // Q27
      const int32_t z2 = Mul(z, z) << 4;                                   // Q27
      gain = Mul(z2, z2) << 4;                                             // Q27
    }
    const int j = harmonic_indices[i];
    // ONE_POLE(amplitudes[j], gain, 0.001).
    amplitudes[j] += gain - Mul(amplitudes[j], Q31(0.001));
    sum += amplitudes[j] >> 3;   // Q24
  }
  // amplitudes *= 1 / sum: sum = x 2^(8 - n) with x in [0.5, 1).
  const int nz = CountLeadingZeros(static_cast<uint32_t>(sum));
  const int32_t r = ReciprocalNormalized(static_cast<int32_t>((static_cast<uint32_t>(sum) << nz) >> 1));
  // a / sum = a (1 / x) 2^(nz - 8): Mul(a, r) is a (1 / x) / 2.
  const int s2 = nz - 7;
  for (int i = 0; i < num_harmonics; ++i) {
    int32_t& a = amplitudes[harmonic_indices[i]];
    const int32_t m = Mul(a, r);
    a = s2 >= 0 ? ShlSat(m, s2) : m >> (-s2);
  }
}

void AdditiveEngineFixed::Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
                                 size_t size, bool* already_enveloped) {
  (void)already_enveloped;
  const uint32_t f0 = NoteToFrequency(parameters.note);
  const int32_t centroid = parameters.timbre;
  const int32_t raw_bumps = parameters.harmonics << 15;   // Q30
  const int32_t morph = parameters.morph << 15;           // Q30
  // raw_slope = (1 - 0.6 raw_bumps) morph; slope = 0.01 + 1.99 raw_slope^3;
  // bumps = 16 raw_bumps^2.
  const int32_t raw_slope = MulQ31((1 << 30) - MulQ31(raw_bumps, Q31(0.6)), morph) << 1;   // Q30
  const int32_t rs2 = MulQ31(raw_slope, raw_slope) << 1;
  const int32_t rs3 = MulQ31(rs2, raw_slope) << 1;
  const int32_t slope = Q(0.01, 30) + (MulQ31(rs3, Q31(1.99 / 2)) << 1);
  const int32_t bumps = (MulQ31(raw_bumps, raw_bumps) << 1) >> 10;   // Q16

  int32_t amplitudes_q31[kNumHarmonics];
  UpdateAmplitudes(centroid, slope, bumps, &amplitudes_[0], kIntegerHarmonics, 24);
  for (int i = 0; i < 24; ++i) amplitudes_q31[i] = Q27ToQ31(amplitudes_[i]);
  harmonic_oscillator_[0].Render<1>(f0, &amplitudes_q31[0], out, size);
  harmonic_oscillator_[1].Render<13>(f0, &amplitudes_q31[12], out, size);
  UpdateAmplitudes(centroid, slope, bumps, &amplitudes_[24], kOrganHarmonics, 8);
  for (int i = 24; i < 36; ++i) amplitudes_q31[i] = Q27ToQ31(amplitudes_[i]);
  harmonic_oscillator_[2].Render<1>(f0, &amplitudes_q31[24], aux, size);
  for (size_t i = 0; i < size; ++i) {
    out[i] >>= (29 - kAudioBits);
    aux[i] >>= (29 - kAudioBits);
  }
}

}  // namespace op1plaits
