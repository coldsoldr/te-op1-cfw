// Marbles' quantizer in fixed point. See quantizer_fx.h.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#include "src/quantizer_fx.h"

namespace op1marbles {
namespace fx {

void Quantizer::Init(const ScaleFx& scale) {
  int n = scale.num_degrees;
  if (!n || n > kMaxDegrees || scale.base_interval != 65536) {
    return;
  }
  num_degrees_ = n;
  base_interval_ = scale.base_interval;

  uint8_t second_largest_threshold = 0;
  for (int i = 0; i < n; ++i) {
    voltage_[i] = scale.degree[i].voltage;
    if (scale.degree[i].weight != 255 &&
        scale.degree[i].weight >= second_largest_threshold) {
      second_largest_threshold = scale.degree[i].weight;
    }
  }
  uint8_t thresholds[kNumThresholds] = { 0, 16, 32, 64, 128, 192, 255 };
  if (second_largest_threshold > 192) {
    thresholds[kNumThresholds - 2] = second_largest_threshold;
  }
  for (int t = 0; t < kNumThresholds; ++t) {
    uint16_t bitmask = 0;
    uint8_t first = 0xff;
    uint8_t last = 0;
    for (int i = 0; i < n; ++i) {
      if (scale.degree[i].weight >= thresholds[t]) {
        bitmask |= 1 << i;
        if (first == 0xff) first = i;
        last = i;
      }
    }
    level_[t].bitmask = bitmask;
    level_[t].first = first;
    level_[t].last = last;
  }
  level_quantizer_.Init();
  for (int t = 0; t < kNumThresholds; ++t) feedback_[t] = 0;
}

int32_t Quantizer::Process(int32_t value, int32_t amount, bool hysteresis) {
  int level = level_quantizer_.Process(amount, kNumThresholds + 1);
  int32_t quantized_voltage = value;

  if (level > 0) {
    level -= 1;
    int32_t raw_value = value;
    if (hysteresis) {
      value += feedback_[level];
    }
    // MAKE_INTEGRAL_FRACTIONAL with upstream's adjustment for negatives
    // (base interval 1 V).
    int32_t note_integral = value >= 0 ? (value >> 16) : -((-value) >> 16);
    int32_t note_fractional = value - note_integral * 65536;
    if (value < 0) {
      note_integral -= 1;
      note_fractional += 65536;
    }
    Level l = level_[level];
    if (!l.bitmask) {
      // No degree reaches this weight (Y's default one-degree scale with
      // weight 0). Upstream then reads voltage_[0xff], past the array
      // (undefined behaviour); the port uses every degree instead.
      l = level_[0];
    }
    int32_t a = voltage_[l.last] - base_interval_;
    int32_t b = voltage_[l.first] + base_interval_;
    uint16_t bitmask = l.bitmask;
    for (int i = 0; i < num_degrees_; ++i) {
      if (bitmask & 1) {
        int32_t v = voltage_[i];
        if (note_fractional > v) {
          a = v;
        } else {
          b = v;
          break;
        }
      }
      bitmask >>= 1;
    }
    quantized_voltage = 2 * note_fractional < a + b ? a : b;
    quantized_voltage += note_integral * base_interval_;
    feedback_[level] = (quantized_voltage - raw_value) / 4;
  }
  return quantized_voltage;
}

}  // namespace fx
}  // namespace op1marbles
