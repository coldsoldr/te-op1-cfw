// Plaits' virtual analog engine (plaits/dsp/engine/virtual_analog_engine.cc,
// VA_VARIANT 2) in fixed point: OUT = variable square (TIMBRE) + variable saw
// (MORPH), detuned by HARMONICS; AUX = two hard-synced variable waveshapes.

#ifndef OP1PLAITS_FIXED_VIRTUAL_ANALOG_ENGINE_H_
#define OP1PLAITS_FIXED_VIRTUAL_ANALOG_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/variable_shape_oscillator.h"

namespace op1plaits {

class VirtualAnalogEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  VariableShapeOscillatorFixed primary_;
  VariableShapeOscillatorFixed auxiliary_;
  VariableShapeOscillatorFixed sync_;
  VariableSawOscillatorFixed variable_saw_;

  int32_t auxiliary_amount_;   // Q31 (square gain)
  int32_t xmod_amount_;        // Q31 (saw gain)
  int32_t* temp_buffer_;
};

// ComputeDetuning(harmonics): Q15 in, Q16 semitones out (shared with tests).
int32_t ComputeDetuning(int32_t detune_q15);

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_VIRTUAL_ANALOG_ENGINE_H_
