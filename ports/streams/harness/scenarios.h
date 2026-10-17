// Render scenarios: one per processor mode and use, mono and stereo.

#ifndef OP1STREAMS_HARNESS_SCENARIOS_H_
#define OP1STREAMS_HARNESS_SCENARIOS_H_

#include "harness/signals.h"
#include "src/core/streams_core.h"

namespace harness {

struct Scenario {
  const char* name;
  Material material;
  bool stereo;
  // Overrides on DefaultControls (-1 = keep).
  int function, alternate, split, filter_mode, detector;
  int shape, mod, sense, mix, attack, decay, response, output;
};

using op1streams::Controls;

const int kSeconds = 4;

// function: 0 envelope, 1 vactrol, 2 follower, 3 compressor, 4 filter
// controller, 5 Lorenz. filter: 0 LP, 1 LP resonant, 2 BP, 3 BP resonant.
// detector: 0 raw, 1 peak envelope.
static const Scenario kScenarios[] = {
  // name                  material   st  fn alt spl flt det  shape    mod  sense    mix att  dec   resp  out
  { "comp_drums",          MAT_DRUMS, 0,  3, 0,  0,  0,  0, 12000,     0,     0, 32767, -1,  -1,    -1,  -1 },
  { "comp_drums_makeup",   MAT_DRUMS, 0,  3, 0,  0,  0,  0, 12000, 26000,     0, 32767, -1,  -1,    -1,  -1 },
  { "comp_soft_parallel",  MAT_DRUMS, 0,  3, 1,  0,  0,  0,  8000,  4000,     0, 16384, -1,  -1,    -1,  -1 },
  { "comp_riff",           MAT_RIFF,  0,  3, 0,  0,  0,  0,  8000,     0,     0, 32767, -1,  -1,    -1,  -1 },
  { "comp_limiter",        MAT_MIX,   0,  3, 0,  0,  0,  0, 20000, 32767,     0, 32767, -1,  -1,    -1,  -1 },
  { "comp_split_st",       MAT_MIX,   1,  3, 0,  1,  0,  0, 10000, 20000,     0, 32767, 6000, 14000, -1, -1 },
  { "vactrol_plonk",       MAT_RIFF,  0,  1, 1,  0,  0,  1,  9000, 26000, 12000, 32767, -1,  -1,    -1,  -1 },
  { "vactrol_lpg",         MAT_RIFF,  0,  1, 0,  0,  0,  1, 12000, 22000, 12000, 32767, -1,  -1,    -1,  -1 },
  { "vactrol_backwards",   MAT_MIX,   0,  1, 0,  0,  0,  1, 28000, 20000, 10000, 32767, -1,  -1,    -1,  -1 },
  { "vactrol_plonk_st",    MAT_DRUMS, 1,  1, 1,  0,  0,  1,  9000, 26000, 12000, 32767, -1,  -1,    -1,  -1 },
  { "follower_wah",        MAT_RIFF,  0,  2, 0,  0,  1,  0,  8000, 30000,  6000, 32767, -1,  -1,    -1,  -1 },
  { "follower_wah_bp",     MAT_RIFF,  0,  2, 0,  0,  3,  0,  8000, 30000,  6000, 32767, -1,  -1,    -1,  -1 },
  { "follower_cutoff",     MAT_RIFF,  0,  2, 1,  0,  0,  0,  8000, 30000,  6000, 32767, -1,  -1,    -1,  -1 },
  { "follower_wah_st",     MAT_MIX,   1,  2, 0,  0,  1,  0,  8000, 30000,  6000, 32767, -1,  -1,    -1,  -1 },
  { "env_transient",       MAT_MIX,   0,  0, 0,  0,  0,  1,  6000, 20000,  8000, 32767, -1,  -1,    -1,  -1 },
  { "env_transient_exp",   MAT_MIX,   0,  0, 0,  0,  0,  1,  6000, 20000,  8000, 32767, -1,  -1,     0,  -1 },
  { "env_raw",             MAT_RIFF,  0,  0, 0,  0,  0,  0,  6000, 20000,  8000, 32767, -1,  -1,    -1,  -1 },
  { "env_ar",              MAT_RIFF,  0,  0, 1,  0,  0,  1, 20000, 24000,  8000, 32767, -1,  -1,    -1,  -1 },
  { "env_transient_st",    MAT_MIX,   1,  0, 0,  0,  0,  1,  6000, 20000,  8000, 32767, -1,  -1,    -1,  -1 },
  { "filter_ctrl_bp",      MAT_RIFF,  0,  4, 1,  0,  3,  0, 20000, 24000,     0, 32767, -1,  -1,    -1,  -1 },
  { "lorenz_pad",          MAT_PAD,   0,  5, 0,  0,  1,  0, 16000, 20000,     0, 32767, -1,  -1,    -1,  -1 },
};

const int kNumScenarios = sizeof(kScenarios) / sizeof(kScenarios[0]);

inline void ScenarioControls(const Scenario& s, Controls* c) {
  op1streams::DefaultControls(c);
#define OVERRIDE(field) if (s.field >= 0) c->field = s.field;
  OVERRIDE(function) OVERRIDE(alternate) OVERRIDE(split) OVERRIDE(filter_mode)
  OVERRIDE(detector) OVERRIDE(shape) OVERRIDE(mod) OVERRIDE(sense) OVERRIDE(mix)
  OVERRIDE(attack) OVERRIDE(decay) OVERRIDE(response) OVERRIDE(output)
#undef OVERRIDE
}

}  // namespace harness

#endif  // OP1STREAMS_HARNESS_SCENARIOS_H_
