// Cycle costs of the core on Blackfin (bfin-elf-run), in OP-1 conditions:
// 64-sample calls at 44.1 kHz, cheap integer input (the harness's float
// input generator is soft-float on Blackfin and very slow). Prints the MHz
// one instance needs per configuration.
//
//   profile_core.elf [CONFIG-PREFIX] [BLOCKS]
//
// With an instruction trace (bfin-elf-run --trace-insn=on), harness/
// profile.py and profile_lines.py show where the time goes.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "harness/cycles.h"
#include "src/core/clouds_core.h"

namespace {

op1clouds::CloudsCore core;

struct Config {
  const char* name;
  int mode;
  int mono;
  float position, size, pitch, density, texture, feedback, reverb, spread;
};

// clang-format off
const Config kConfigs[] = {
  // name           mode mono  pos   size  pitch dens  tex   fb    rev   spr
  { "ld_plain",       2, 0,   0.0f, 0.5f, -7.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f },
  { "ld_full",        2, 0,   0.2f, 0.5f, -7.0f, 0.5f, 0.3f, 0.3f, 0.5f, 0.0f },
  { "ld_mono_plain",  2, 1,   0.0f, 0.5f, -7.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f },
  { "gr_rand",        0, 0,   0.3f, 0.5f,  0.0f, 0.8f, 0.5f, 0.0f, 0.0f, 0.0f },
  { "gr_dense",       0, 0,   0.3f, 0.6f,  0.0f, 0.95f, 0.5f, 0.0f, 0.0f, 0.0f },
  { "gr_full",        0, 0,   0.3f, 0.5f,  7.0f, 0.8f, 0.9f, 0.3f, 0.5f, 0.5f },
  { "gr_mono_rand",   0, 1,   0.3f, 0.5f,  0.0f, 0.8f, 0.5f, 0.0f, 0.0f, 0.0f },
};
// clang-format on

int32_t Q16(float x) { return static_cast<int32_t>(x * 65536.0f); }

}  // namespace

int main(int argc, char** argv) {
  const char* prefix = argc > 1 ? argv[1] : "";
  const int blocks = argc > 2 ? atoi(argv[2]) : 300;
  harness::EnableCycleCounter();
  for (const Config& c : kConfigs) {
    if (strncmp(c.name, prefix, strlen(prefix)) != 0) continue;
    core.Init(c.mono ? 1 : 2);
    core.set_playback_mode(c.mode);
    op1clouds::CloudsParams* p = core.mutable_parameters();
    p->position = Q16(c.position);
    p->size = Q16(c.size);
    p->pitch = Q16(c.pitch);
    p->density = Q16(c.density);
    p->texture = Q16(c.texture);
    p->dry_wet = 65536;
    p->stereo_spread = Q16(c.spread);
    p->feedback = Q16(c.feedback);
    p->reverb = Q16(c.reverb);
    p->freeze = false;
    p->trigger = false;
    int16_t l[64], r[64];
    uint32_t x = 1;
    harness::CycleStats stats;
    for (int b = 0; b < blocks; ++b) {
      for (int i = 0; i < 64; ++i) {
        x = x * 1664525u + 1013904223u;
        l[i] = static_cast<int16_t>(static_cast<int32_t>(x) >> 18);
        r[i] = static_cast<int16_t>(static_cast<int32_t>(x) >> 19);
      }
      const uint64_t t0 = harness::ReadCycles();
      if (c.mono) {
        core.ProcessMono(l, 64);
      } else {
        core.ProcessStereo(l, r, 64);
      }
      // After a second of warm-up (grains accumulate).
      if (b >= blocks / 3) stats.Add(harness::ReadCycles() - t0);
    }
    stats.Print(c.name, 64);
    if (c.mode == 0) printf("%-26s active grains at the end: %ld\n", "", static_cast<long>(core.granular()->active_grains()));
  }
  return 0;
}
