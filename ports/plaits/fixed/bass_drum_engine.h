// Plaits' bass drum engine (plaits/dsp/engine/bass_drum_engine.cc,
// drums/analog_bass_drum.h, drums/synthetic_bass_drum.h, fx/overdrive.h) in
// fixed point: OUT = the 808-style analog model through an overdrive, AUX =
// the synthetic (909-ish) model.

#ifndef OP1PLAITS_FIXED_BASS_DRUM_ENGINE_H_
#define OP1PLAITS_FIXED_BASS_DRUM_ENGINE_H_

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace op1plaits {

// plaits::SineOscillator (the drums' free-running sustain oscillators).
class SineOscillatorFixed {
 public:
  void Init() { phase_ = 0; }
  // frequency Q32 (clamped to 0.5); Q31 out.
  inline int32_t Next(uint32_t frequency) {
    if (frequency >= (1u << 31)) frequency = 1u << 31;
    phase_ += frequency;
    return Sine(phase_);
  }
  // amplitude Q31: sin and cos scaled by it (Q31).
  inline void Next(uint32_t frequency, int32_t amplitude, int32_t* sin, int32_t* cos) {
    if (frequency >= (1u << 31)) frequency = 1u << 31;
    phase_ += frequency;
    *sin = Mul(amplitude, Sine(phase_));
    *cos = Mul(amplitude, Sine(phase_ + (1u << 30)));
  }
 private:
  uint32_t phase_;
};

class AnalogBassDrumFixed {
 public:
  void Init();
  // f0 Q32; accent, tone, decay, attack/self FM amounts Q15. out: Q26.
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t tone,
              int32_t decay, int32_t attack_fm_amount, int32_t self_fm_amount,
              int32_t* out, size_t size);

 private:
  int32_t pulse_remaining_samples_;
  int32_t fm_pulse_remaining_samples_;
  int32_t pulse_;          // Q24
  int32_t pulse_height_;   // Q24
  int32_t pulse_lp_;       // Q24
  int32_t fm_pulse_lp_;    // Q31
  int32_t retrig_pulse_;   // Q31
  int32_t lp_out_;         // Q25
  int32_t tone_lp_;        // Q25
  int32_t sustain_gain_;   // Q31
  SvfFixed resonator_;
  SineOscillatorFixed oscillator_;
};

class OverdriveFixed {
 public:
  void Init() { pre_gain_ = 0; post_gain_ = 0; }
  // drive Q31; in_out Q26.
  void Process(int32_t drive, int32_t* in_out, size_t size);
 private:
  int32_t pre_gain_;    // Q26
  int32_t post_gain_;   // Q29
};

class SyntheticBassDrumFixed {
 public:
  void Init();
  // f0 Q32; the rest Q15. out: Q26.
  void Render(bool sustain, bool trigger, int32_t accent, uint32_t f0, int32_t tone,
              int32_t decay, int32_t dirtiness, int32_t fm_envelope_amount,
              int32_t fm_envelope_decay, int32_t* out, size_t size);

 private:
  int32_t f0_;           // Q32
  uint32_t phase_;
  int32_t phase_noise_;  // Q31
  int32_t fm_;           // Q31
  int32_t fm_lp_;
  int32_t body_env_;
  int32_t body_env_lp_;
  int32_t transient_env_;
  int32_t transient_env_lp_;
  int32_t sustain_gain_;
  int32_t tone_lp_;      // Q28

  // SyntheticBassDrumClick / AttackNoise.
  int32_t click_lp_;     // Q30
  int32_t click_hp_;
  SvfFixed click_filter_;
  int32_t noise_lp_;     // Q31
  int32_t noise_hp_;

  int32_t body_env_pulse_width_;
  int32_t fm_pulse_width_;
};

class BassDrumEngineFixed {
 public:
  void Init(Allocator* allocator);
  void Reset() { }
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);

 private:
  AnalogBassDrumFixed analog_bass_drum_;
  SyntheticBassDrumFixed synthetic_bass_drum_;
  OverdriveFixed overdrive_;
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_BASS_DRUM_ENGINE_H_
