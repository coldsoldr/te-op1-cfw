// Scenario driver for the Rings DSP. The same source builds against either
// the unmodified upstream code (the float reference) or the port in src/
// (-DPORT), so both are exercised identically.
//
//   render OUTDIR [SCENARIO...]   writes OUTDIR/<scenario>.wav (32-bit float)
//   built with -DBENCH: runs every scenario and prints cycles per block
//   (for bfin-elf-run; no file output).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifdef PORT
#include "src/part.h"
#include "src/range_stats.h"
namespace R = op1rings;
static const char kBuildName[] = "port";
OP1RINGS_RANGE_DEFINE
#else
#include "rings/dsp/part.h"
namespace R = rings;
static const char kBuildName[] = "upstream float reference";
#endif
#include "stmlib/utils/random.h"

#include "harness/cycles.h"
#include "harness/scenarios.h"
#include "harness/wav.h"

using namespace harness;

static uint16_t reverb_buffer[32768];
static R::Part part;

static const size_t kBlock = R::kMaxBlockSize;
static const float kRate = R::kSampleRate;
static void Run(const Scenario& s, FloatWavWriter* wav, CycleStats* stats,
                float seconds_override) {
  stmlib::Random::Seed(0x21);
  memset(reverb_buffer, 0, sizeof(reverb_buffer));
  part.Init(reverb_buffer);
  part.set_polyphony(s.polyphony);
  part.set_model(static_cast<R::ResonatorModel>(s.model));

  Scenario run = s;
  if (seconds_override > 0.0f) run.seconds = seconds_override;
  size_t num_blocks = NumBlocks(run, kRate, kBlock);
  float impulse = 0.0f;
  float in[kBlock], out[kBlock], aux[kBlock];

  for (size_t b = 0; b < num_blocks; ++b) {
    BlockControl c = GetBlockControl(run, b, num_blocks, kRate, kBlock);
    FillInput(run, c, &impulse, in, kBlock);

    R::PerformanceState performance;
    memset(&performance, 0, sizeof(performance));
    performance.strum = c.strum;
    performance.internal_exciter = run.internal_exciter;
    performance.note = c.note;
    performance.tonic = 0.0f;
    performance.fm = 0.0f;
    performance.chord = 0;

    R::Patch patch;
    patch.structure = c.structure;
    patch.brightness = c.brightness;
    patch.damping = c.damping;
    patch.position = c.position;

    uint64_t t0 = ReadCycles();
    part.Process(performance, patch, in, out, aux, kBlock);
    uint64_t t1 = ReadCycles();
    if (stats) stats->Add(t1 - t0);
    if (wav) wav->Write(out, aux, kBlock);
  }
}

int main(int argc, char** argv) {
#ifdef BENCH
  EnableCycleCounter();
  float seconds = argc > 1 ? static_cast<float>(atof(argv[1])) : 0.25f;
  printf("%s, %d Hz, block %d, %.2f s per scenario\n",
         kBuildName, static_cast<int>(kRate), static_cast<int>(kBlock), seconds);
  for (int i = 0; i < kNumScenarios; ++i) {
    CycleStats stats;
    Run(kScenarios[i], NULL, &stats, seconds);
    stats.Print(kScenarios[i].name, kBlock);
  }
  return 0;
#else
  if (argc < 2) {
    fprintf(stderr, "usage: %s OUTDIR [SCENARIO...]\n", argv[0]);
    return 1;
  }
  for (int i = 0; i < kNumScenarios; ++i) {
    const Scenario& s = kScenarios[i];
    bool wanted = argc == 2;
    for (int a = 2; a < argc; ++a) wanted |= strcmp(argv[a], s.name) == 0;
    if (!wanted) continue;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", argv[1], s.name);
    FloatWavWriter wav;
    if (!wav.Open(path, static_cast<uint32_t>(kRate))) {
      fprintf(stderr, "cannot write %s\n", path);
      return 1;
    }
    Run(s, &wav, NULL, 0.0f);
    printf("wrote %s\n", path);
#ifdef OP1RINGS_RANGE_STATS
    op1rings::RangeStats::Print(s.name);
    op1rings::RangeStats::Reset();
#endif
  }
  return 0;
#endif
}
