// Marbles' X/Y generator (three X channels and Y, clock routing, deja vu
// sharing) in fixed point (upstream: marbles/random/x_y_generator.{h,cc}).
//
// Not ported: the external X clock (RampExtractor) and register mode; see
// output_channel_fx.h and t_generator_fx.h.
//
// Based on Mutable Instruments Marbles (MIT, (c) 2015 Emilie Gillet).

#ifndef OP1MARBLES_FX_X_Y_GENERATOR_FX_H_
#define OP1MARBLES_FX_X_Y_GENERATOR_FX_H_

#include <stdint.h>

#include "src/output_channel_fx.h"
#include "src/ramps_fx.h"
#include "src/random_fx.h"
#include "src/t_generator_fx.h"

namespace op1marbles {
namespace fx {

enum VoltageRange {
  VOLTAGE_RANGE_NARROW,    // 0..2 V
  VOLTAGE_RANGE_POSITIVE,  // 0..5 V
  VOLTAGE_RANGE_FULL       // -5..5 V
};

enum ClockSource {
  CLOCK_SOURCE_INTERNAL_T1_T2_T3,
  CLOCK_SOURCE_INTERNAL_T1,
  CLOCK_SOURCE_INTERNAL_T2,
  CLOCK_SOURCE_INTERNAL_T3,
};

enum ControlMode {
  CONTROL_MODE_IDENTICAL,
  CONTROL_MODE_BUMP,
  CONTROL_MODE_TILT
};

const int kNumXChannels = 3;
const int kNumYChannels = 1;
const int kNumChannels = kNumXChannels + kNumYChannels;

struct GroupSettings {
  ControlMode control_mode;
  VoltageRange voltage_range;
  int32_t spread;   // Q16
  int32_t bias;     // Q16
  int32_t steps;    // Q16
  int32_t deja_vu;  // Q16
  int scale_index;
  int length;
  Ratio ratio;
};

class XYGenerator {
 public:
  void Init(RandomStream* random_stream);
  // output: kNumChannels interleaved Q16 voltages per sample (X1 X2 X3 Y).
  void Process(ClockSource clock_source, const GroupSettings& x_settings,
               const GroupSettings& y_settings, bool* reset,
               const Ramps& ramps, int32_t* output, int size);
  void LoadScale(int scale_index, const ScaleFx& scale) {
    for (int i = 0; i < kNumXChannels; ++i) {
      output_channel_[i].LoadScale(scale_index, scale);
    }
  }

 private:
  RandomSequence random_sequence_[kNumChannels];
  OutputChannel output_channel_[kNumChannels];
  RampDivider ramp_divider_;
  bool use_shifted_sequences_[kNumChannels];
};

}  // namespace fx
}  // namespace op1marbles

#endif  // OP1MARBLES_FX_X_Y_GENERATOR_FX_H_
