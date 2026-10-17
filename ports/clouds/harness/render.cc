// Renders the scenarios (harness/scenarios.h) through one of:
//   -DREF   upstream float Clouds (mutableinstruments/clouds/dsp)
//   -DCOPY  the float copy in src/dsp (namespace op1clouds): must be
//           bit-identical to REF
//   -DPORT  the fixed-point port (src/core, op1clouds::CloudsCore)
//
//   render OUTDIR [scenario-name-prefix]   -> OUTDIR/<scenario>.wav (int16 stereo)
//
// With -DBENCH (Blackfin, bfin-elf-run) nothing is written: it prints the
// cycles per 32-sample block for each scenario and a checksum of the output
// (the host build prints the same checksum, so Blackfin = host is checked
// by comparing them).
//
// Mono scenarios feed both reference channels the same mono signal
// (m = (l + r) / 2), so upstream's mono mix-down is exact, and the port
// processes that one channel.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "harness/scenarios.h"

#ifndef BENCH
#include "harness/wav16.h"
#endif
#include "harness/cycles.h"

#ifndef OP1CLOUDS_SAMPLE_RATE
#define OP1CLOUDS_SAMPLE_RATE 32000
#endif

#if defined(REF) || defined(COPY)

#ifdef REF
#include "clouds/dsp/granular_processor.h"
namespace cl = clouds;
#else
#include "src/dsp/granular_processor.h"
namespace cl = op1clouds;
#endif

namespace {

// Upstream's test buffers (clouds_test.cc).
uint8_t large_buffer[118784];
uint8_t small_buffer[65536 - 128];
// Static, so members upstream leaves uninitialised (the looper's
// loop_reset_) start at 0 in every build.
cl::GranularProcessor processor;

class Processor {
 public:
  void Init(int mono) {
    memset(&processor, 0, sizeof(processor));
    processor.Init(large_buffer, sizeof(large_buffer), small_buffer, sizeof(small_buffer));
    processor.set_num_channels(mono ? 1 : 2);
    processor.set_low_fidelity(false);
    processor.set_playback_mode(cl::PLAYBACK_MODE_LOOPING_DELAY);
    stmlib::Random::Seed(0x21);
    mono_ = mono;
    first_ = true;
  }

  void Process(const harness::FloatParams& fp, int mode, int16_t* l, int16_t* r, int size) {
    processor.set_playback_mode(static_cast<cl::PlaybackMode>(mode));
    if (first_) {
      processor.Prepare();
      first_ = false;
    }
    cl::Parameters* p = processor.mutable_parameters();
    p->position = fp.position;
    p->size = fp.size;
    p->pitch = fp.pitch;
    p->density = fp.density;
    p->texture = fp.texture;
    p->dry_wet = fp.dry_wet;
    p->stereo_spread = fp.stereo_spread;
    p->feedback = fp.feedback;
    p->reverb = fp.reverb;
    p->freeze = fp.freeze;
    p->trigger = fp.trigger;
    p->gate = false;
    cl::ShortFrame in[32], out[32];
    for (int i = 0; i < size; ++i) {
      in[i].l = l[i];
      in[i].r = mono_ ? l[i] : r[i];
    }
    processor.Process(in, out, size);
    processor.Prepare();
    for (int i = 0; i < size; ++i) {
      l[i] = out[i].l;
      r[i] = out[i].r;
    }
  }

 private:
  int mono_;
  bool first_;
};

}  // namespace

#elif defined(PORT)

#include "src/core/clouds_core.h"

namespace {

op1clouds::CloudsCore core;

int32_t ToQ16(float x) {
  return static_cast<int32_t>(x * 65536.0f + (x < 0.0f ? -0.5f : 0.5f));
}

class Processor {
 public:
  void Init(int mono) {
    core.Init(mono ? 1 : 2);
    mono_ = mono;
  }

  void Process(const harness::FloatParams& fp, int mode, int16_t* l, int16_t* r, int size) {
    op1clouds::CloudsParams* p = core.mutable_parameters();
    p->position = ToQ16(fp.position);
    p->size = ToQ16(fp.size);
    p->pitch = ToQ16(fp.pitch);
    p->density = ToQ16(fp.density);
    p->texture = ToQ16(fp.texture);
    p->dry_wet = ToQ16(fp.dry_wet);
    p->stereo_spread = ToQ16(fp.stereo_spread);
    p->feedback = ToQ16(fp.feedback);
    p->reverb = ToQ16(fp.reverb);
    p->freeze = fp.freeze;
    p->trigger = fp.trigger;
    core.set_playback_mode(mode);
    if (mono_) {
      core.ProcessMono(l, size);
      for (int i = 0; i < size; ++i) r[i] = l[i];
    } else {
      core.ProcessStereo(l, r, size);
    }
  }

 private:
  int mono_;
};

}  // namespace

#else
#error "define REF, COPY or PORT"
#endif

namespace {

Processor proc;

uint32_t Checksum(uint32_t h, const int16_t* x, int n) {
  for (int i = 0; i < n; ++i) {
    h ^= static_cast<uint16_t>(x[i]);
    h *= 16777619u;
  }
  return h;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef BENCH
  if (argc < 2) {
    fprintf(stderr, "usage: %s OUTDIR [prefix]\n", argv[0]);
    return 1;
  }
  const char* out_dir = argv[1];
  const char* prefix = argc > 2 ? argv[2] : "";
#else
  const char* prefix = argc > 1 ? argv[1] : "";
  const float max_seconds = argc > 2 ? static_cast<float>(atof(argv[2])) : 1.0f;
  harness::EnableCycleCounter();
#endif
  const int sample_rate = OP1CLOUDS_SAMPLE_RATE;
  for (int s = 0; s < harness::kNumScenarios; ++s) {
    const harness::Scenario& sc = harness::kScenarios[s];
    if (strncmp(sc.name, prefix, strlen(prefix)) != 0) continue;
    float seconds = sc.seconds;
#ifdef BENCH
    if (seconds > max_seconds) seconds = max_seconds;
#endif
    const int num_blocks = static_cast<int>(seconds * sample_rate) / 32;
    harness::InputGenerator input;
    input.Init(sc.input, sample_rate);
    proc.Init(sc.mono);
#ifndef BENCH
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", out_dir, sc.name);
    harness::Wav16Writer wav;
    if (!wav.Open(path, sample_rate)) {
      fprintf(stderr, "can't write %s\n", path);
      return 1;
    }
#endif
    harness::CycleStats stats;
    uint32_t checksum = 2166136261u;
    for (int b = 0; b < num_blocks; ++b) {
      harness::FloatParams p;
      int mode;
      harness::ScenarioParams(sc, b, num_blocks, sample_rate, &p, &mode);
      int16_t l[32], r[32];
      input.Render(l, r, 32);
      if (sc.mono) {
        for (int i = 0; i < 32; ++i) {
          l[i] = static_cast<int16_t>((l[i] + r[i]) >> 1);
          r[i] = l[i];
        }
      }
      const uint64_t t0 = harness::ReadCycles();
      proc.Process(p, mode, l, r, 32);
      stats.Add(harness::ReadCycles() - t0);
      checksum = Checksum(checksum, l, 32);
      checksum = Checksum(checksum, r, 32);
#ifndef BENCH
      wav.Write(l, r, 32);
#endif
    }
#ifdef BENCH
    stats.Print(sc.name, 32);
#endif
    printf("%-22s checksum %08x\n", sc.name, checksum);
  }
  return 0;
}
