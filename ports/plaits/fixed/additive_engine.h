// Plaits' additive engine (plaits/dsp/engine/additive_engine.cc and
// oscillator/harmonic_oscillator.h) in fixed point: 24 harmonics on OUT, 8
// organ harmonics on AUX, a spectral envelope set by TIMBRE (centroid), MORPH
// (slope) and HARMONICS (bumps).

#ifndef OP1PLAITS_FIXED_ADDITIVE_ENGINE_H_
#define OP1PLAITS_FIXED_ADDITIVE_ENGINE_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

const int kHarmonicBatchSize = 12;
const int kNumHarmonics = 36;
const int kNumHarmonicOscillators = kNumHarmonics / kHarmonicBatchSize;

// HarmonicOscillator<12>: amplitudes Q31 in, Q29 out (replaces or adds).
class HarmonicOscillatorFixed {
 public:
  void Init() {
    phase_ = 0;
    frequency_ = 0;
    for (int i = 0; i < kHarmonicBatchSize; ++i) amplitude_[i] = 0;
  }
  template<int first_harmonic_index>
  void Render(uint32_t frequency, const int32_t* amplitudes, int32_t* out, size_t size);

 private:
  uint32_t phase_;
  int32_t frequency_;
  int32_t amplitude_[kHarmonicBatchSize];
};

class AdditiveEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() {
    for (int i = 0; i < kNumHarmonics; ++i) amplitudes_[i] = 0;
  }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  void UpdateAmplitudes(int32_t centroid, int32_t slope, int32_t bumps, int32_t* amplitudes,
                        const int* harmonic_indices, int num_harmonics);

  HarmonicOscillatorFixed harmonic_oscillator_[kNumHarmonicOscillators];
  int32_t amplitudes_[kNumHarmonics];   // Q27 (upstream allocates them)
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_ADDITIVE_ENGINE_H_
