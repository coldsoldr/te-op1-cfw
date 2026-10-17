// Plaits' waveshaping engine (plaits/dsp/engine/waveshaping_engine.cc) in
// fixed point: a slope oscillator through a crossfaded waveshaper and a
// wavefolder (OUT), and a triangle through a sine shaper / second folder
// (AUX).

#ifndef OP1PLAITS_FIXED_WAVESHAPING_ENGINE_H_
#define OP1PLAITS_FIXED_WAVESHAPING_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/oscillator.h"

namespace op1plaits {

class WaveshapingEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  OscillatorFixed slope_;
  OscillatorFixed triangle_;
  int32_t previous_shape_;              // Q30
  int32_t previous_wavefolder_gain_;    // Q31
  int32_t previous_overtone_gain_;      // Q31
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_WAVESHAPING_ENGINE_H_
