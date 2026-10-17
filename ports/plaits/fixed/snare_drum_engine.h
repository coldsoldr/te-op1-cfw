// Plaits' snare drum engine (plaits/dsp/engine/snare_drum_engine.cc,
// drums/analog_snare_drum.h, drums/synthetic_snare_drum.h) in fixed point:
// OUT = the 808-style model (five resonant modes + filtered noise), AUX = the
// 909-style model (two coupled oscillators + filtered noise).

#ifndef OP1PLAITS_FIXED_SNARE_DRUM_ENGINE_H_
#define OP1PLAITS_FIXED_SNARE_DRUM_ENGINE_H_

#include "fixed/bass_drum_engine.h"
#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

class AnalogSnareDrumFixed {
 public:
  static const int kNumModes = 5;
  void Init();
  // f0 Q32; the rest Q15; out Q26.
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t tone,
              int32_t decay, int32_t snappy, int32_t* out, size_t size);

 private:
  int32_t pulse_remaining_samples_;
  int32_t pulse_;          // Q24
  int32_t pulse_height_;   // Q24
  int32_t pulse_lp_;       // Q24
  int32_t noise_envelope_; // Q30
  int32_t sustain_gain_;   // Q31
  SvfFixed resonator_[kNumModes];
  SvfFixed noise_filter_;
  SineOscillatorFixed oscillator_[kNumModes];
};

class SyntheticSnareDrumFixed {
 public:
  void Init();
  // f0 Q32; the rest Q15; out Q26.
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t fm_amount,
              int32_t decay, int32_t snappy, int32_t* out, size_t size);

 private:
  int32_t phase_[2];        // Q30, signed (upstream folds the phase below 0)
  int32_t drum_amplitude_;  // Q31
  int32_t snare_amplitude_;
  int32_t fm_;
  int32_t sustain_gain_;
  int32_t hold_counter_;
  OnePoleFixed drum_lp_;
  OnePoleFixed snare_hp_;
  SvfFixed snare_lp_;
};

class SnareDrumEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  AnalogSnareDrumFixed analog_snare_drum_;
  SyntheticSnareDrumFixed synthetic_snare_drum_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_SNARE_DRUM_ENGINE_H_
