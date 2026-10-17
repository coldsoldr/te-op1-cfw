// Drives Marbles' float T and X/Y generators the way marbles.cc does (minus
// the CV reader and UI), with a deterministic random stream: the hardware RNG
// never writes into the RandomStream, so every word comes from the LCG
// fallback seeded with the scenario's seed.
//
// Include after defining MARBLES_NS (marbles or op1marbles) and including
// that namespace's headers.

#ifndef PORTS_MARBLES_HARNESS_FLOAT_DRIVER_H_
#define PORTS_MARBLES_HARNESS_FLOAT_DRIVER_H_

#include <cstdint>
#include <cstring>

#include "harness/scenarios.h"

namespace harness {

// One output sample: the three T gates and the four voltages.
#ifndef PORTS_MARBLES_HARNESS_FRAME_DEFINED
#define PORTS_MARBLES_HARNESS_FRAME_DEFINED
struct Frame {
  uint8_t gates;     // bit 0 T1, bit 1 T2 (master ramp < 0.5), bit 2 T3
  float voltage[4];  // X1, X2, X3, Y in volts
};
#endif

class FloatDriver {
 public:
  void Init(uint32_t seed) {
    random_generator_.Init(seed);
    random_stream_.Init(&random_generator_);
    t_generator_.Init(&random_stream_, static_cast<float>(kSampleRate));
    xy_generator_.Init(&random_stream_, static_cast<float>(kSampleRate));
    for (int i = 0; i < 6; ++i) {
      xy_generator_.LoadScale(i, kScales[i]);
    }
    memset(clock_, 0, sizeof(clock_));
  }

  void SetParams(const Params& p) { params_ = p; }
  void Render() { Process(params_, frames_); }
  void Output(Frame* out) const {
    for (int i = 0; i < kSubsteps; ++i) out[i] = frames_[i];
  }

  // Renders kSubsteps samples (one OP-1 LFO call).
  void Process(const Params& p, Frame* out) {
    using namespace MARBLES_NS;
    const size_t n = kSubsteps;
    Ramps ramps;
    ramps.master = &ramp_buffer_[0];
    ramps.external = &ramp_buffer_[n];
    ramps.slave[0] = &ramp_buffer_[n * 2];
    ramps.slave[1] = &ramp_buffer_[n * 3];

    t_generator_.set_model(TGeneratorModel(p.t_model));
    t_generator_.set_range(TGeneratorRange(p.t_range));
    t_generator_.set_rate(p.t_rate);
    t_generator_.set_bias(p.t_bias);
    t_generator_.set_jitter(p.t_jitter);
    t_generator_.set_deja_vu(p.t_deja_vu == DV_LOCKED
        ? 0.5f : (p.t_deja_vu == DV_ON ? p.deja_vu : 0.0f));
    t_generator_.set_length(p.length);
    t_generator_.set_pulse_width_mean(p.pw_mean);
    t_generator_.set_pulse_width_std(p.pw_std);
    bool t_reset = false;
    t_generator_.Process(false, &t_reset, clock_, ramps, gates_, n);

    GroupSettings x, y;
    x.control_mode = ControlMode(p.x_control_mode);
    x.voltage_range = VoltageRange(p.x_range % 3);
    x.register_mode = false;
    x.register_value = 0.0f;
    x.spread = p.x_spread;
    x.bias = p.x_bias;
    x.steps = p.x_steps;
    x.deja_vu = p.x_deja_vu == DV_LOCKED
        ? 0.5f : (p.x_deja_vu == DV_ON ? p.deja_vu : 0.0f);
    x.length = p.length;
    x.ratio.p = 1;
    x.ratio.q = 1;
    y.control_mode = CONTROL_MODE_IDENTICAL;
    y.voltage_range = VoltageRange(p.y_range);
    y.register_mode = false;
    y.register_value = 0.0f;
    y.spread = p.y_spread;
    y.bias = p.y_bias;
    y.steps = p.y_steps;
    y.deja_vu = 0.0f;
    y.length = 1;
    y.ratio = kYDividerRatios[static_cast<uint16_t>(p.y_divider) * 12 >> 8];
    y.scale_index = x.scale_index = p.x_scale;
    bool x_reset = false;
    xy_generator_.Process(ClockSource(p.clock_source), x, y, &x_reset, clock_,
                          ramps, voltages_, n);

    for (size_t i = 0; i < n; ++i) {
      out[i].gates = (gates_[2 * i] ? 1 : 0)
          | (ramps.master[i] < 0.5f ? 2 : 0)
          | (gates_[2 * i + 1] ? 4 : 0);
      for (int j = 0; j < 4; ++j) {
        out[i].voltage[j] = voltages_[4 * i + j];
      }
    }
  }

 private:
  static const MARBLES_NS::Scale kScales[6];
  static const MARBLES_NS::Ratio kYDividerRatios[12];

  MARBLES_NS::RandomGenerator random_generator_;
  MARBLES_NS::RandomStream random_stream_;
  MARBLES_NS::TGenerator t_generator_;
  MARBLES_NS::XYGenerator xy_generator_;
  stmlib::GateFlags clock_[kSubsteps];
  float ramp_buffer_[kSubsteps * 4];
  bool gates_[kSubsteps * 2];
  float voltages_[kSubsteps * 4];
  Params params_;
  Frame frames_[kSubsteps];
};

const MARBLES_NS::Scale FloatDriver::kScales[6] =
#include "upstream/scales_data.inc"
;

// marbles.cc's y_divider_ratios.
const MARBLES_NS::Ratio FloatDriver::kYDividerRatios[12] = {
  { 1, 64 }, { 1, 48 }, { 1, 32 }, { 1, 24 }, { 1, 16 }, { 1, 12 },
  { 1, 8 }, { 1, 6 }, { 1, 4 }, { 1, 3 }, { 1, 2 }, { 1, 1 },
};

}  // namespace harness

#endif  // PORTS_MARBLES_HARNESS_FLOAT_DRIVER_H_
