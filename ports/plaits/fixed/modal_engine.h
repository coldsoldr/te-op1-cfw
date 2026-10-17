// Plaits' modal resonator engine (plaits/dsp/engine/modal_engine.cc,
// physical_modelling/modal_voice.cc, physical_modelling/resonator.cc) in
// fixed point: an excitation (filtered impulse or dust) through 24 band-pass
// modes (TPT state-variable filters, coefficients per block).
//
// Mode states and signals Q24 (the modal output reaches ~+-20). The
// resonance is kept as r = 1 / Q in Q31 (Q reaches ~10^6 at full damping).

#ifndef OP1PLAITS_FIXED_MODAL_ENGINE_H_
#define OP1PLAITS_FIXED_MODAL_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

const int kMaxNumModes = 24;

class ResonatorFixed {
 public:
  void Init(int32_t position_q31, int resolution);
  // f0 Q32; structure, brightness, damping Q31; in Q24; out Q24 (adds).
  void Process(uint32_t f0, int32_t structure, int32_t brightness, int32_t damping,
               const int32_t* in, int32_t* out, size_t size);
  int resolution() const { return resolution_; }
  void set_resolution(int r) { resolution_ = r > kMaxNumModes ? kMaxNumModes : r; }

 private:
  int resolution_;
  int32_t mode_amplitude_[kMaxNumModes];   // Q31
  int32_t state_1_[kMaxNumModes];          // Q24
  int32_t state_2_[kMaxNumModes];
};

class ModalVoiceFixed {
 public:
  void Init();
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t structure,
              int32_t brightness, int32_t damping, int32_t* temp, int32_t* out, int32_t* aux,
              size_t size);
  ResonatorFixed* resonator() { return &resonator_; }

 private:
  // The excitation filter (ResonatorSvf<1>, low-pass), states Q24.
  int32_t excitation_state_1_;
  int32_t excitation_state_2_;
  ResonatorFixed resonator_;
};

class ModalEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset();
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);
  ModalVoiceFixed* voice() { return &voice_; }

 private:
  ModalVoiceFixed voice_;
  int32_t* temp_buffer_;
  int32_t harmonics_lp_;   // Q31
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_MODAL_ENGINE_H_
