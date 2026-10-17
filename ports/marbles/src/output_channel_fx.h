// Marbles' X/Y output channel (voltage generation, quantizer, steps/smooth)
// and its lag processor, in fixed point (upstream: marbles/random/
// output_channel.{h,cc}, lag_processor.{h,cc}).
//
// Voltages are Q16 volts at the interface; the lag processor's state is Q24
// volts. Spread, bias Q16; steps Q16 (interpolated across the block in Q18,
// which is exact: upstream's ParameterInterpolator divides by the block
// size, a power of two here).
//
// Not ported: register (external CV / ASR) mode. The OP-1 has no CV input;
// the shift register could later sample the played note (docs/marbles-port.md).
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_OUTPUT_CHANNEL_FX_H_
#define OP1MARBLES_FX_OUTPUT_CHANNEL_FX_H_

#include <stdint.h>

#include "src/quantizer_fx.h"
#include "src/random_fx.h"

namespace op1marbles {
namespace fx {

struct ScaleOffset {
  int32_t scale;   // integer volts per unit: 2, 5 or 10
  int32_t offset;  // integer volts
};

class LagProcessor {
 public:
  void Init() {
    ramp_start_ = 0;
    ramp_value_ = 0;
    lp_state_ = 0;
    previous_phase_ = 0;
  }
  inline void ResetRamp() { ramp_start_ = ramp_value_; }
  // value Q16 volts, smoothness Q18 in (0, 1], phase Q32. Returns Q16 volts.
  int32_t Process(int32_t value, int32_t smoothness, uint32_t phase);

 private:
  int32_t ramp_start_;  // Q24 volts
  int32_t ramp_value_;
  int32_t lp_state_;
  uint32_t previous_phase_;
};

class OutputChannel {
 public:
  void Init();
  void LoadScale(int i, const ScaleFx& scale) { quantizer_[i].Init(scale); }
  // size: 1, 2, 4, 8 or 16. phase: Q32 ramp; output: Q16 volts, strided.
  void Process(RandomSequence* random_sequence, const uint32_t* phase,
               int32_t* output, int size, int stride);

  inline void set_spread(int32_t spread) { spread_ = spread; }
  inline void set_bias(int32_t bias) { bias_ = bias; }
  inline void set_scale_index(int i) { scale_index_ = i; }
  inline void set_steps(int32_t steps) { steps_ = steps; }
  inline void set_scale_offset(const ScaleOffset& so) { scale_offset_ = so; }

  inline int32_t Quantize(int32_t voltage, int32_t amount_q18) {
    return quantizer_[scale_index_].Process(voltage, amount_q18, false);
  }

 private:
  int32_t GenerateNewVoltage(RandomSequence* random_sequence);

  int32_t spread_;  // Q16
  int32_t bias_;    // Q16
  int32_t steps_;   // Q16
  int scale_index_;
  int32_t previous_steps_;  // Q18
  uint32_t previous_phase_;
  int32_t previous_voltage_;
  int32_t voltage_;
  int32_t quantized_voltage_;
  ScaleOffset scale_offset_;
  LagProcessor lag_processor_;
  Quantizer quantizer_[6];
};

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_OUTPUT_CHANNEL_FX_H_
