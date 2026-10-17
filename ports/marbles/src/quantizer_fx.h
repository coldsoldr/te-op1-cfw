// Marbles' weighted-scale quantizer in fixed point (upstream:
// marbles/random/quantizer.{h,cc}). Voltages Q16 (1 V = 65536); the amount
// (2 * steps - 1) Q18. The hysteresis-quantized level is exact (the float
// arithmetic it replaces is exact for these inputs); the note search is
// integer, so the selected degree is upstream's except when the input lies
// within the Q16 rounding of a scale degree's midpoint (|dv| < ~2e-5 V).
//
// Only base intervals of 1 V (every Marbles preset scale) are supported:
// a general interval would need a division per note.
//
// One deliberate difference: a level whose weight threshold no degree
// reaches (Y's default scale, steps above ~0.57) makes upstream read past
// voltage_[] (undefined behaviour); the port quantizes to all degrees then.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_QUANTIZER_FX_H_
#define OP1MARBLES_FX_QUANTIZER_FX_H_

#include <stdint.h>

#include "src/tables_fx.h"

namespace op1marbles {
namespace fx {

const int kNumThresholds = 7;

// stmlib's HysteresisQuantizer for Process(value, num_steps) (hysteresis
// 0.25), value in Q18.
class HysteresisQuantizer {
 public:
  void Init() { quantized_value_ = 0; }
  int Process(int32_t value_q18, int num_steps) {
    int32_t v = value_q18 * (num_steps - 1);
    int32_t fb = v > (quantized_value_ << 18) ? -(1 << 16) : (1 << 16);
    int32_t x = v + fb + (1 << 17);
    int q = x >= 0 ? (x >> 18) : -((-x) >> 18);  // int(): toward zero
    if (q < 0) q = 0;
    if (q > num_steps - 1) q = num_steps - 1;
    quantized_value_ = q;
    return q;
  }
 private:
  int quantized_value_;
};

class Quantizer {
 public:
  void Init(const ScaleFx& scale);
  // value: Q16 volts; amount: Q18 in [-1, 1]. Returns Q16 volts.
  int32_t Process(int32_t value, int32_t amount, bool hysteresis);

 private:
  struct Level {
    uint16_t bitmask;
    uint8_t first;
    uint8_t last;
  };
  int32_t voltage_[kMaxDegrees];
  Level level_[kNumThresholds];
  int32_t feedback_[kNumThresholds];
  int32_t base_interval_;
  int num_degrees_;
  HysteresisQuantizer level_quantizer_;
};

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_QUANTIZER_FX_H_
