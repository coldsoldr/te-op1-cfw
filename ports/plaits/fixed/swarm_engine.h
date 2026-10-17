// Plaits' swarm engine (plaits/dsp/engine/swarm_engine.{h,cc}) in fixed
// point: eight voices, each a grain envelope driving a saw (OUT) and a sine
// (AUX) with randomised glides.

#ifndef OP1PLAITS_FIXED_SWARM_ENGINE_H_
#define OP1PLAITS_FIXED_SWARM_ENGINE_H_

#include "fixed/fixed_math.h"

namespace op1plaits {

const int kNumSwarmVoices = 8;

class GrainEnvelopeFixed {
 public:
  void Init() {
    from_ = 0;
    interval_ = 1 << 30;
    phase_ = 1 << 28;
    fm_ = 0;
    amplitude_ = 1 << 30;
    previous_size_ratio_ = 0;
    filter_coefficient_ = 0;
  }
  // rate: Q31.
  void Step(int32_t rate, bool burst_mode, bool start_burst);
  // size_ratio: Q24. Q30 out.
  int32_t frequency(int32_t size_ratio) const;
  // Q31 out.
  int32_t amplitude(int32_t size_ratio);

 private:
  int32_t from_;        // Q30
  int32_t interval_;    // Q30
  int32_t phase_;       // Q28
  int32_t fm_;          // Q27
  int32_t amplitude_;   // Q31
  int32_t previous_size_ratio_;   // Q24
  int32_t filter_coefficient_;    // Q31
};

class AdditiveSawOscillatorFixed {
 public:
  void Init() {
    phase_ = 0;
    next_sample_ = 0;
    frequency_ = static_cast<int32_t>(Q32(0.01));
    gain_ = 0;
  }
  // frequency Q32; level Q31; adds into out (Q29).
  void Render(uint32_t frequency, int32_t level, int32_t* out, size_t size);
 private:
  uint32_t phase_;
  int32_t next_sample_;   // Q30
  int32_t frequency_;
  int32_t gain_;
};

class FastSineOscillatorFixed {
 public:
  void Init() {
    x_ = 1 << 30;
    y_ = 0;
    epsilon_ = 0;
    amplitude_ = 0;
  }
  // frequency Q32; amplitude Q31; adds into out (Q29).
  void Render(uint32_t frequency, int32_t amplitude, int32_t* out, size_t size);
 private:
  int32_t x_;          // Q30
  int32_t y_;
  int32_t epsilon_;    // Q30
  int32_t amplitude_;  // Q31
};

class SwarmVoiceFixed {
 public:
  void Init(int32_t rank) {
    rank_ = rank;
    envelope_.Init();
    saw_.Init();
    sine_.Init();
  }
  void Render(uint32_t f0, int32_t density, bool burst_mode, bool start_burst, int32_t spread,
              int32_t size_ratio, int32_t* saw, int32_t* sine, size_t size);
 private:
  int32_t rank_;   // Q30
  GrainEnvelopeFixed envelope_;
  AdditiveSawOscillatorFixed saw_;
  FastSineOscillatorFixed sine_;
};

class SwarmEngineFixed {
 public:
  void Init(Allocator* allocator) { (void)allocator; }
  void Reset();
  void Render(const FixedParameters& parameters, int32_t* out, int32_t* aux,
              size_t size, bool* already_enveloped);
 private:
  SwarmVoiceFixed swarm_voice_[kNumSwarmVoices];
};

}  // namespace op1plaits

#endif  // OP1PLAITS_FIXED_SWARM_ENGINE_H_
