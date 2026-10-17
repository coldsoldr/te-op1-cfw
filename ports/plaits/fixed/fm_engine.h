// Plaits' FM engine (plaits/dsp/engine/fm_engine.cc) in fixed point: two
// operators with feedback and a sub-oscillator, 4x oversampled.

#ifndef OP1PLAITS_FIXED_FM_ENGINE_H_
#define OP1PLAITS_FIXED_FM_ENGINE_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

class FMEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  uint32_t carrier_phase_;
  uint32_t modulator_phase_;
  uint32_t sub_phase_;

  int32_t previous_carrier_frequency_;     // Q32
  int32_t previous_modulator_frequency_;   // Q32
  int32_t previous_amount_;                // Q29
  int32_t previous_feedback_;              // Q31
  int32_t previous_sample_;                // Q30

  int32_t sub_fir_;       // downsampler state, Q30
  int32_t carrier_fir_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_FM_ENGINE_H_
