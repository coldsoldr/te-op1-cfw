// Plaits' inharmonic string engine (plaits/dsp/engine/string_engine.cc,
// physical_modelling/string_voice.cc, physical_modelling/string.cc) in fixed
// point: three Karplus-Strong strings (played round robin) with dispersion or
// a curved bridge, a damping low-pass, a DC blocker and a noise / dust
// exciter.
//
// String signals are Q24 (upstream clamps them to +-20); the Hermite read
// runs in Q23. Delay lengths Q16 samples.

#ifndef OP1PLAITS_FIXED_STRING_ENGINE_H_
#define OP1PLAITS_FIXED_STRING_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

const int kStringDelayLineSize = 1024;
const int kNumStrings = 3;

class StringFixed {
 public:
  void Init(Allocator* allocator);
  void Reset();
  // f0 Q32; non_linearity Q31 (signed); brightness, damping Q31. in: Q24;
  // out: Q24 (adds).
  void Process(uint32_t f0, int32_t non_linearity, int32_t brightness, int32_t damping,
               const int32_t* in, int32_t* out, size_t size);

 private:
  template<bool dispersion>
  void ProcessInternal(uint32_t f0, int32_t non_linearity, int32_t brightness, int32_t damping,
                       const int32_t* in, int32_t* out, size_t size);

  int32_t* string_;     // kStringDelayLineSize words, Q24
  int32_t* stretch_;    // kStringDelayLineSize / 4
  uint32_t string_write_ptr_;
  uint32_t stretch_write_ptr_;

  SvfFixed iir_damping_filter_;
  int32_t dc_x_;        // DC blocker (Q24)
  int32_t dc_y_;

  int32_t delay_;           // Q16
  int32_t dispersion_noise_;   // Q31
  int32_t curved_bridge_;      // Q24

  int32_t src_phase_;       // Q30, [0, 4)
  int32_t out_sample_[2];   // Q24
};

class StringVoiceFixed {
 public:
  void Init(Allocator* allocator);
  void Reset();
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t structure,
              int32_t brightness, int32_t damping, int32_t* temp, int32_t* out, int32_t* aux,
              size_t size);

 private:
  SvfFixed excitation_filter_;
  StringFixed string_;
  uint32_t remaining_noise_samples_;
};

class StringEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset();
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  StringVoiceFixed voice_[kNumStrings];
  uint32_t f0_[kNumStrings];
  uint32_t f0_delay_[16];   // upstream's DelayLine<float, 16> of f0
  uint32_t f0_delay_write_ptr_;
  int active_string_;
  int32_t* temp_buffer_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_STRING_ENGINE_H_
