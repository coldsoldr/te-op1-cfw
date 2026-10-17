// Plaits' hi-hat engine (plaits/dsp/engine/hi_hat_engine.cc, drums/hi_hat.h)
// in fixed point: OUT = six square oscillators (808 metallic noise), a
// resonant band-pass, swing VCA; AUX = three ring-modulated oscillator pairs,
// a two-stage envelope, linear VCA. Both add clocked noise and end with a
// high-pass. Signals in Q26 throughout.

#ifndef OP1PLAITS_FIXED_HI_HAT_ENGINE_H_
#define OP1PLAITS_FIXED_HI_HAT_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/oscillator.h"
#include "fixed/svf.h"

namespace op1plaits {

class SquareNoiseFixed {
 public:
  void Init() { for (int i = 0; i < 6; ++i) phase_[i] = 0; }
  void Render(uint32_t f0, int32_t* temp_1, int32_t* temp_2, int32_t* out, size_t size);
 private:
  uint32_t phase_[6];
};

class RingModNoiseFixed {
 public:
  void Init() { for (int i = 0; i < 6; ++i) oscillator_[i].Init(); }
  void Render(uint32_t f0, int32_t* temp_1, int32_t* temp_2, int32_t* out, size_t size);
 private:
  OscillatorFixed oscillator_[6];
};

template<typename MetallicNoiseSource, bool swing_vca, bool resonance, bool two_stage_envelope>
class HiHatFixed {
 public:
  void Init() {
    envelope_ = 0;
    noise_clock_ = 0;
    noise_sample_ = 0;
    sustain_gain_ = 0;
    metallic_noise_.Init();
    noise_coloration_svf_.Init();
    hpf_.Init();
  }
  // f0 Q32; the rest Q15; out Q26.
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t tone,
              int32_t decay, int32_t noisiness, int32_t* temp_1, int32_t* temp_2,
              int32_t* out, size_t size);

 private:
  int32_t envelope_;       // Q30
  uint32_t noise_clock_;   // Q31 in [0, 2)
  int32_t noise_sample_;   // Q26
  int32_t sustain_gain_;   // Q31
  MetallicNoiseSource metallic_noise_;
  SvfFixed noise_coloration_svf_;
  SvfFixed hpf_;
};

class HiHatEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  HiHatFixed<SquareNoiseFixed, true, true, false> hi_hat_1_;
  HiHatFixed<RingModNoiseFixed, false, false, true> hi_hat_2_;
  int32_t* temp_buffer_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_HI_HAT_ENGINE_H_
