// Plaits' granular formant engine (plaits/dsp/engine/grain_engine.cc,
// oscillator/grainlet_oscillator.h, oscillator/z_oscillator.h) in fixed
// point: OUT = two grainlet oscillators (a shaped carrier windowing a formant
// sine), AUX = the "Z" oscillator (a formant with a phase-reset window).

#ifndef OP1PLAITS_FIXED_GRAIN_ENGINE_H_
#define OP1PLAITS_FIXED_GRAIN_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

class GrainletOscillatorFixed {
 public:
  void Init() {
    carrier_phase_ = 0;
    formant_phase_ = 0;
    next_sample_ = 0;
    carrier_frequency_ = 0;
    formant_frequency_ = 0;
    carrier_shape_ = 0;
    carrier_bleed_ = 0;
  }
  // Frequencies Q32; shape, bleed Q30 (0..1). out: Q30.
  void Render(uint32_t carrier_frequency, uint32_t formant_frequency, int32_t carrier_shape,
              int32_t carrier_bleed, int32_t* out, size_t size);

 private:
  uint32_t carrier_phase_;   // Q31, [0, 2)
  uint32_t formant_phase_;   // Q32
  int32_t next_sample_;      // Q30
  int32_t carrier_frequency_;
  int32_t formant_frequency_;
  int32_t carrier_shape_;
  int32_t carrier_bleed_;
};

class ZOscillatorFixed {
 public:
  void Init() {
    carrier_phase_ = 0;
    discontinuity_phase_ = 0;
    formant_phase_ = 0;
    next_sample_ = 0;
    carrier_frequency_ = 0;
    formant_frequency_ = 0;
    carrier_shape_ = 0;
    mode_ = 0;
  }
  // Frequencies Q32; shape, mode Q30 (0..1). out: Q28.
  void Render(uint32_t carrier_frequency, uint32_t formant_frequency, int32_t carrier_shape,
              int32_t mode, int32_t* out, size_t size);

 private:
  uint32_t carrier_phase_;         // Q31, [0, 2)
  uint32_t discontinuity_phase_;   // Q31, [0, 2)
  uint32_t formant_phase_;         // Q32
  int32_t next_sample_;            // Q28
  int32_t carrier_frequency_;
  int32_t formant_frequency_;
  int32_t carrier_shape_;
  int32_t mode_;
};

class GrainEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  GrainletOscillatorFixed grainlet_[2];
  ZOscillatorFixed z_oscillator_;
  OnePoleFixed dc_blocker_[2];
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_GRAIN_ENGINE_H_
