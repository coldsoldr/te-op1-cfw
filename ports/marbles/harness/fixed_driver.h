// Drives the fixed-point port exactly as harness/float_driver.h drives
// upstream: same scenario controls (converted to the port's Q formats, which
// is exact: the harness quantizes them to multiples of 2^-16 and 2^-8),
// same seed, same block size.

#ifndef PORTS_MARBLES_HARNESS_FIXED_DRIVER_H_
#define PORTS_MARBLES_HARNESS_FIXED_DRIVER_H_

#include <cmath>
#include <cstdint>

#include "harness/scenarios.h"
#include "src/random_fx.h"
#include "src/t_generator_fx.h"
#include "src/tables_fx.h"
#include "src/x_y_generator_fx.h"

namespace harness {

#ifndef PORTS_MARBLES_HARNESS_FRAME_DEFINED
#define PORTS_MARBLES_HARNESS_FRAME_DEFINED
struct Frame {
  uint8_t gates;     // bit 0 T1, bit 1 T2 (master ramp < 0.5), bit 2 T3
  float voltage[4];  // X1, X2, X3, Y in volts
};
#endif

inline int32_t ToQ16(float x) { return static_cast<int32_t>(x * 65536.0f + (x >= 0 ? 0.5f : -0.5f)); }
inline int32_t ToQ8(float x) { return static_cast<int32_t>(x * 256.0f + (x >= 0 ? 0.5f : -0.5f)); }

// 1.0f / sr as upstream computes it, held exactly as m * 2^e.
inline op1marbles::fx::Float24 OneHertz() {
  float f = 1.0f / static_cast<float>(kSampleRate);
  int e;
  double m = std::frexp(static_cast<double>(f), &e);  // f = m * 2^e, 0.5 <= m < 1
  op1marbles::fx::Float24 r;
  r.m = static_cast<uint32_t>(std::ldexp(m, 24));  // exact: 24-bit mantissa
  r.e = e - 24;
  return r;
}

class FixedDriver {
 public:
  void Init(uint32_t seed) {
    using namespace op1marbles::fx;
    random_generator_.Init(seed);
    random_stream_.Init(&random_generator_);
    t_generator_.Init(&random_stream_, OneHertz());
    xy_generator_.Init(&random_stream_);
    for (int i = 0; i < 6; ++i) {
      xy_generator_.LoadScale(i, preset_scales[i]);
    }
  }

  // Raw outputs (Q16 volts) of the last Process call, for the comparisons.
  const int32_t* voltages() const { return voltages_; }
  const uint32_t* master_ramp() const { return ramp_buffer_; }

  // Converts the scenario's controls to the port's formats (soft-float on
  // Blackfin: kept out of the timed Render()).
  void SetParams(const Params& p) {
    using namespace op1marbles::fx;
    const int32_t dv = ToQ16(p.deja_vu);
    t_model_ = p.t_model;
    t_range_ = p.t_range;
    t_rate_ = ToQ8(p.t_rate);
    t_bias_ = ToQ16(p.t_bias);
    t_jitter_ = ToQ16(p.t_jitter);
    t_deja_vu_ = p.t_deja_vu == DV_LOCKED ? kQ16One / 2 : (p.t_deja_vu == DV_ON ? dv : 0);
    length_ = p.length;
    pw_mean_ = ToQ16(p.pw_mean);
    pw_std_ = ToQ16(p.pw_std);
    clock_source_ = p.clock_source;
    x_.control_mode = ControlMode(p.x_control_mode);
    x_.voltage_range = VoltageRange(p.x_range % 3);
    x_.spread = ToQ16(p.x_spread);
    x_.bias = ToQ16(p.x_bias);
    x_.steps = ToQ16(p.x_steps);
    x_.deja_vu = p.x_deja_vu == DV_LOCKED ? kQ16One / 2 : (p.x_deja_vu == DV_ON ? dv : 0);
    x_.length = p.length;
    x_.ratio.p = 1;
    x_.ratio.q = 1;
    y_.control_mode = CONTROL_MODE_IDENTICAL;
    y_.voltage_range = VoltageRange(p.y_range);
    y_.spread = ToQ16(p.y_spread);
    y_.bias = ToQ16(p.y_bias);
    y_.steps = ToQ16(p.y_steps);
    y_.deja_vu = 0;
    y_.length = 1;
    static const int kYDividerQ[12] = { 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1 };
    y_.ratio.p = 1;
    y_.ratio.q = kYDividerQ[static_cast<uint16_t>(p.y_divider) * 12 >> 8];
    y_.scale_index = x_.scale_index = p.x_scale;
  }

  // One OP-1 LFO call: kSubsteps Marbles samples (what the device would run).
  void Render() {
    using namespace op1marbles::fx;
    const int n = kSubsteps;
    Ramps ramps;
    ramps.master = &ramp_buffer_[0];
    ramps.external = &ramp_buffer_[n];
    ramps.slave[0] = &ramp_buffer_[n * 2];
    ramps.slave[1] = &ramp_buffer_[n * 3];
    t_generator_.set_model(TGeneratorModel(t_model_));
    t_generator_.set_range(TGeneratorRange(t_range_));
    t_generator_.set_rate(t_rate_);
    t_generator_.set_bias(t_bias_);
    t_generator_.set_jitter(t_jitter_);
    t_generator_.set_deja_vu(t_deja_vu_);
    t_generator_.set_length(length_);
    t_generator_.set_pulse_width_mean(pw_mean_);
    t_generator_.set_pulse_width_std(pw_std_);
    bool t_reset = false;
    t_generator_.Process(&t_reset, ramps, gates_, n);
    bool x_reset = false;
    xy_generator_.Process(ClockSource(clock_source_), x_, y_, &x_reset, ramps,
                          voltages_, n);
  }

  void Output(Frame* out) const {
    for (int i = 0; i < kSubsteps; ++i) {
      out[i].gates = (gates_[2 * i] ? 1 : 0)
          | (ramp_buffer_[i] < 0x80000000u ? 2 : 0)
          | (gates_[2 * i + 1] ? 4 : 0);
      for (int j = 0; j < 4; ++j) {
        out[i].voltage[j] = static_cast<float>(voltages_[4 * i + j]) / 65536.0f;
      }
    }
  }

  void Process(const Params& p, Frame* out) {
    SetParams(p);
    Render();
    Output(out);
  }

 private:
  op1marbles::fx::RandomGenerator random_generator_;
  op1marbles::fx::RandomStream random_stream_;
  op1marbles::fx::TGenerator t_generator_;
  op1marbles::fx::XYGenerator xy_generator_;
  uint32_t ramp_buffer_[kSubsteps * 4];
  bool gates_[kSubsteps * 2];
  int32_t voltages_[kSubsteps * 4];
  int t_model_, t_range_, length_, clock_source_;
  int32_t t_rate_, t_bias_, t_jitter_, t_deja_vu_, pw_mean_, pw_std_;
  op1marbles::fx::GroupSettings x_, y_;
};

}  // namespace harness

#endif  // PORTS_MARBLES_HARNESS_FIXED_DRIVER_H_
