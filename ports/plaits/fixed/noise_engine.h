// Plaits' filtered noise engine (plaits/dsp/engine/noise_engine.cc) in fixed
// point: clocked noise through an LP-BP-HP state-variable filter (OUT) and a
// pair of band-passes (AUX).
//
// Upstream recomputes the filter coefficients and the input gain every
// sample from the interpolated frequencies and resonance; here they're
// computed at each block's end and interpolated linearly (the same while the
// parameters hold).

#ifndef OP1PLAITS_FIXED_NOISE_ENGINE_H_
#define OP1PLAITS_FIXED_NOISE_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

// plaits::ClockedNoise: sample-and-hold noise clocked at `frequency`, with
// polyBLEP on the steps; raw noise above a quarter of the sample rate.
class ClockedNoiseFixed {
 public:
  void Init() {
    phase_ = 0;
    sample_ = 0;
    next_sample_ = 0;
    frequency_ = Q(0.001, 31);
  }
  // frequency: Q31 (unsigned, up to 1.0 = 2^31). out: Q29.
  void Render(bool sync, uint32_t frequency, int32_t* out, size_t size);

 private:
  uint32_t phase_;   // Q31 in [0, 2)
  int32_t sample_;   // Q29
  int32_t next_sample_;
  int32_t frequency_;
};

class NoiseEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  ClockedNoiseFixed clocked_noise_[2];
  SvfFixed lp_hp_filter_;
  SvfFixed bp_filter_[2];

  // Previous block's coefficients, for the interpolation.
  int32_t previous_g0_;     // Q27
  int32_t previous_g1_;     // Q27
  int32_t previous_r_;      // Q29
  int32_t previous_h0_;     // Q31
  int32_t previous_h1_;     // Q31
  int32_t previous_gain_;   // Q27
  int32_t previous_mode_;   // Q31
  bool first_;              // the first block starts at its own coefficients

  int32_t* temp_buffer_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_NOISE_ENGINE_H_
