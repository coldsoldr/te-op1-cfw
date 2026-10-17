// Plaits' wavetable engine (plaits/dsp/engine/wavetable_engine.cc) in fixed
// point: 4 banks of 8 x 8 integrated waves, trilinear interpolation, a
// differentiator; AUX is OUT quantised to 5 bits.

#ifndef OP1PLAITS_FIXED_WAVETABLE_ENGINE_H_
#define OP1PLAITS_FIXED_WAVETABLE_ENGINE_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

class WavetableEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  uint32_t phase_;

  int32_t x_pre_lp_;   // Q24 (0..7)
  int32_t y_pre_lp_;
  int32_t z_pre_lp_;

  int32_t x_lp_;       // Q24
  int32_t y_lp_;
  int32_t z_lp_;
  int32_t previous_x_;
  int32_t previous_y_;
  int32_t previous_z_;
  int32_t previous_f0_;    // Q32


  // Differentiator (Q12 of the int16 wave scale).
  int32_t diff_previous_;
  int32_t diff_lp_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_WAVETABLE_ENGINE_H_
