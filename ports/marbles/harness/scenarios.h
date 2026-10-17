// Scenarios for the Marbles harness: control settings, optional linear
// sweeps of one control, seed and length. Shared by the float driver
// (upstream or the copy) and the fixed-point port.
//
// Every continuous control is quantized to a multiple of 1/65536 (and the
// rate to 1/256 semitone) before it reaches either implementation, so the
// float build and the fixed-point port see exactly the same values (a
// multiple of 2^-16 below 2 is exact in a float).

#ifndef PORTS_MARBLES_HARNESS_SCENARIOS_H_
#define PORTS_MARBLES_HARNESS_SCENARIOS_H_

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace harness {

// The OP-1 calls an LFO's process once per 64-sample block at 44.1 kHz
// (docs/lfo-interface.md): 689.0625 calls per second. The port runs
// kSubsteps Marbles samples per call: 1 by default (Marbles at 689.0625 Hz,
// one value per OP-1 block); build with -DOP1MARBLES_SUBSTEPS=4 for
// 2756.25 Hz (finer clock timing, 4x the per-sample cost).
#ifndef OP1MARBLES_SUBSTEPS
#define OP1MARBLES_SUBSTEPS 1
#endif
const double kTickRate = 44100.0 / 64.0;
const int kSubsteps = OP1MARBLES_SUBSTEPS;
const double kSampleRate = kTickRate * kSubsteps;

enum DejaVuMode { DV_OFF = 0, DV_ON = 1, DV_LOCKED = 2 };

struct Params {
  // T section.
  int t_model;       // marbles::TGeneratorModel, 0..6
  int t_range;       // 0 = 0.25x, 1 = 1x, 2 = 4x
  int t_deja_vu;     // DejaVuMode
  float t_rate;      // semitones re 2 Hz (x range), -60..60 on the pot
  float t_bias;      // 0..1
  float t_jitter;    // 0..1
  float pw_mean;     // 0..1 (Marbles' t_pulse_width_mean / 256)
  float pw_std;      // 0..1
  // Shared deja vu.
  float deja_vu;     // 0..1, after Marbles' 12 o'clock deadband
  int length;        // 1..16
  // X section.
  int x_deja_vu;     // DejaVuMode
  int x_control_mode;  // 0 identical, 1 bump, 2 tilt
  int x_range;       // 0 narrow (0..2 V), 1 positive (0..5 V), 2 full (+-5 V)
  int x_scale;       // 0..5
  int clock_source;  // 0 T1/T2/T3, 1 T1, 2 T2, 3 T3 (external not used)
  float x_spread, x_bias, x_steps;
  // Y section.
  int y_range;
  float y_spread, y_bias, y_steps;
  int y_divider;     // 0..255, as Marbles' state.y_divider
};

enum SweepField {
  SW_NONE, SW_T_RATE, SW_T_BIAS, SW_T_JITTER, SW_DEJA_VU, SW_LENGTH,
  SW_X_SPREAD, SW_X_BIAS, SW_X_STEPS
};

struct Scenario {
  const char* name;
  uint32_t seed;
  float seconds;
  Params p;
  int sweep;
  float from, to;
};

inline float Q16(float x) { return std::floor(x * 65536.0f + 0.5f) / 65536.0f; }
inline float Q8(float x) { return std::floor(x * 256.0f + 0.5f) / 256.0f; }

inline Params Defaults() {
  Params p;
  p.t_model = 0; p.t_range = 1; p.t_deja_vu = DV_OFF;
  p.t_rate = 12.0f; p.t_bias = 0.5f; p.t_jitter = 0.0f;
  p.pw_mean = 0.5f; p.pw_std = 0.0f;
  p.deja_vu = 0.0f; p.length = 8;
  p.x_deja_vu = DV_OFF; p.x_control_mode = 0; p.x_range = 2; p.x_scale = 0;
  p.clock_source = 0;
  p.x_spread = 0.5f; p.x_bias = 0.5f; p.x_steps = 0.75f;
  p.y_range = 2; p.y_spread = 0.5f; p.y_bias = 0.5f; p.y_steps = 0.5f;
  p.y_divider = 160;
  return p;
}

// Parameters for one block: the scenario's settings with the sweep applied
// at position t (0..1), quantized.
inline Params At(const Scenario& s, double t) {
  Params p = s.p;
  float v = static_cast<float>(s.from + (s.to - s.from) * t);
  switch (s.sweep) {
    case SW_T_RATE: p.t_rate = v; break;
    case SW_T_BIAS: p.t_bias = v; break;
    case SW_T_JITTER: p.t_jitter = v; break;
    case SW_DEJA_VU: p.deja_vu = v; break;
    case SW_LENGTH: p.length = static_cast<int>(v); break;
    case SW_X_SPREAD: p.x_spread = v; break;
    case SW_X_BIAS: p.x_bias = v; break;
    case SW_X_STEPS: p.x_steps = v; break;
    default: break;
  }
  p.t_rate = Q8(p.t_rate);
  p.t_bias = Q16(p.t_bias); p.t_jitter = Q16(p.t_jitter);
  p.pw_mean = Q16(p.pw_mean); p.pw_std = Q16(p.pw_std);
  p.deja_vu = Q16(p.deja_vu);
  p.x_spread = Q16(p.x_spread); p.x_bias = Q16(p.x_bias); p.x_steps = Q16(p.x_steps);
  p.y_spread = Q16(p.y_spread); p.y_bias = Q16(p.y_bias); p.y_steps = Q16(p.y_steps);
  if (p.length < 1) p.length = 1;
  if (p.length > 16) p.length = 16;
  return p;
}

inline int NumScenarios();
inline Scenario GetScenario(int i);

#define HP(field, value) p.field = value
inline Scenario GetScenario(int i) {
  Scenario s;
  s.seconds = 30.0f;
  s.seed = 0x1234u + 7919u * static_cast<uint32_t>(i);
  s.sweep = SW_NONE; s.from = s.to = 0.0f;
  Params p = Defaults();
  switch (i) {
    case 0: s.name = "comp_bernoulli"; break;
    case 1: s.name = "clusters"; HP(t_model, 1); HP(t_bias, 0.8f); HP(t_jitter, 0.3f);
      HP(x_steps, 0.2f); HP(x_spread, 0.3f); break;
    case 2: s.name = "drums"; HP(t_model, 2); HP(t_bias, 0.7f); HP(t_rate, 18.0f);
      HP(x_steps, 0.5f); break;
    case 3: s.name = "indep_bernoulli"; HP(t_model, 3); HP(t_bias, 0.3f); HP(t_jitter, 0.6f);
      HP(x_spread, 0.9f); HP(x_bias, 0.2f); break;
    case 4: s.name = "divider_sweep"; HP(t_model, 4); s.sweep = SW_T_BIAS; s.from = 0.0f; s.to = 1.0f;
      break;
    case 5: s.name = "three_states"; HP(t_model, 5); HP(t_bias, 0.25f); HP(x_steps, 1.0f);
      HP(x_scale, 2); break;
    case 6: s.name = "markov"; HP(t_model, 6); HP(t_bias, 0.75f); HP(deja_vu, 0.3f);
      HP(t_deja_vu, DV_ON); break;
    case 7: s.name = "dejavu_locked"; HP(t_deja_vu, DV_LOCKED); HP(x_deja_vu, DV_LOCKED);
      HP(length, 5); HP(t_model, 3); break;
    case 8: s.name = "dejavu_sweep"; HP(t_model, 3); HP(t_deja_vu, DV_ON); HP(x_deja_vu, DV_ON);
      s.sweep = SW_DEJA_VU; s.from = 0.0f; s.to = 1.0f; break;
    case 9: s.name = "jitter_sweep"; s.sweep = SW_T_JITTER; s.from = 0.0f; s.to = 1.0f; break;
    case 10: s.name = "steps_sweep"; s.sweep = SW_X_STEPS; s.from = 0.0f; s.to = 1.0f; break;
    case 11: s.name = "spread_sweep"; HP(x_bias, 0.65f); s.sweep = SW_X_SPREAD; s.from = 0.0f;
      s.to = 1.0f; break;
    case 12: s.name = "bias_sweep"; HP(x_spread, 0.6f); s.sweep = SW_X_BIAS; s.from = 0.0f;
      s.to = 1.0f; break;
    case 13: s.name = "clock_t2_bump"; HP(clock_source, 2); HP(x_control_mode, 1);
      HP(x_spread, 0.7f); HP(x_bias, 0.4f); break;
    case 14: s.name = "clock_t1_tilt"; HP(clock_source, 1); HP(x_control_mode, 2); HP(x_range, 1);
      HP(x_steps, 0.3f); HP(t_model, 3); break;
    case 15: s.name = "range4x_fast"; HP(t_range, 2); HP(t_rate, 12.0f); HP(x_steps, 0.6f);
      HP(t_model, 3); break;
    case 16: s.name = "rate_sweep"; s.sweep = SW_T_RATE; s.from = -48.0f; s.to = 36.0f; break;
    case 17: s.name = "pulse_width"; HP(t_model, 3); HP(pw_mean, 0.3f); HP(pw_std, 0.4f); break;
    // Y steps stay below ~0.57: above, upstream's Y quantizer reads past its
    // array (src/quantizer_fx.h).
    case 18: s.name = "y_divider"; HP(y_divider, 128); HP(y_steps, 0.3f); HP(y_spread, 0.4f);
      HP(clock_source, 3); break;
    case 19: s.name = "length_sweep"; HP(deja_vu, 0.25f); HP(t_deja_vu, DV_ON); HP(x_deja_vu, DV_ON);
      s.sweep = SW_LENGTH; s.from = 1.0f; s.to = 16.99f; break;
    case 20: s.name = "steps_scales"; HP(x_steps, 0.9f); HP(x_scale, 3); HP(x_range, 0);
      HP(t_rate, 24.0f); break;
    case 21: s.name = "raag_dejavu"; HP(x_steps, 0.95f); HP(x_scale, 4); HP(x_deja_vu, DV_ON);
      HP(deja_vu, 0.8f); HP(t_deja_vu, DV_ON); HP(t_model, 1); break;
    default: s.name = 0; break;
  }
  s.p = p;
  return s;
}
#undef HP

inline int NumScenarios() { return 22; }

}  // namespace harness

#endif  // PORTS_MARBLES_HARNESS_SCENARIOS_H_
