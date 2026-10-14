// Renders the harness scenarios through op1rings::RingsVoice (the fixed-point
// OP-1 voice), for comparison with the upstream reference. Only modal
// scenarios with the internal exciter apply.
//
//   render_voice OUTDIR [SCENARIO...]   writes OUTDIR/<scenario>.wav (float)
//   built with -DBENCH: cycles per sub-block for each scenario (bfin-elf-run)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "harness/cycles.h"
#include "harness/scenarios.h"
#include "harness/wav.h"
#include "src/fixed.h"
#include "src/op1_voice.h"

using namespace harness;
using namespace op1rings;

#ifndef OP1RINGS_SAMPLE_RATE
#define OP1RINGS_SAMPLE_RATE 44100
#endif

static const float kRate = OP1RINGS_SAMPLE_RATE;
static const size_t kBlock = kControlBlockSize;
static RingsVoice voice;

static bool Applies(const Scenario& s) {
  return s.model == MODEL_MODAL && s.internal_exciter && s.polyphony == 1;
}

// Phase increment of a MIDI note: f / fs * 2^32 (computed on the host; the
// OP-1 provides these).
static uint32_t PhaseIncrement(float note) {
#ifdef __bfin__
  // Avoid float maths on the target bench: a table of the notes used.
  static const struct { int note; uint32_t inc; } kTable[] = {
    { 28, 0 }, { 33, 0 }, { 36, 0 }, { 40, 0 }, { 43, 0 }, { 48, 0 },
    { 55, 0 }, { 60, 0 }, { 67, 0 }, { 72, 0 }, { 84, 0 },
  };
  (void)kTable;
  return static_cast<uint32_t>(4294967296.0 * 440.0 / kRate) >> 3;  // ~A1
#else
  double f = 440.0 * pow(2.0, (note - 69.0) / 12.0) / kRate;
  return static_cast<uint32_t>(f * 4294967296.0);
#endif
}

static void Run(const Scenario& s, FloatWavWriter* wav, CycleStats* stats,
                float seconds_override) {
  voice.Init(60);
  Scenario run = s;
  if (seconds_override > 0.0f) run.seconds = seconds_override;
  size_t num_blocks = NumBlocks(run, kRate, kBlock);
  uint32_t inc[kBlock];
  int32_t out[kBlock], aux[kBlock];
  float out_f[kBlock], aux_f[kBlock];
  for (size_t b = 0; b < num_blocks; ++b) {
    BlockControl c = GetBlockControl(run, b, num_blocks, kRate, kBlock);
    if (c.strum) voice.NoteOn();
    uint32_t increment = PhaseIncrement(c.note);
    for (size_t i = 0; i < kBlock; ++i) inc[i] = increment;
    VoicePatch patch;
    patch.structure = FloatToQ31(c.structure);
    patch.brightness = FloatToQ31(c.brightness);
    patch.damping = FloatToQ31(c.damping);
    patch.position = FloatToQ31(c.position);
    uint64_t t0 = ReadCycles();
    voice.Render(inc, patch, out, aux, kBlock);
    uint64_t t1 = ReadCycles();
    if (stats) stats->Add(t1 - t0);
    if (wav) {
      for (size_t i = 0; i < kBlock; ++i) {
        out_f[i] = Q27ToFloat(out[i]);
        aux_f[i] = Q27ToFloat(aux[i]);
      }
      wav->Write(out_f, aux_f, kBlock);
    }
  }
}

int main(int argc, char** argv) {
#ifdef BENCH
  EnableCycleCounter();
  float seconds = argc > 1 ? static_cast<float>(atof(argv[1])) : 0.25f;
  printf("RingsVoice, %d Hz, sub-block %d\n", static_cast<int>(kRate), static_cast<int>(kBlock));
  for (int i = 0; i < kNumScenarios; ++i) {
    if (!Applies(kScenarios[i])) continue;
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
    if (!Applies(s)) continue;
    bool wanted = argc == 2;
    for (int a = 2; a < argc; ++a) wanted |= strcmp(argv[a], s.name) == 0;
    if (!wanted) continue;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", argv[1], s.name);
    FloatWavWriter wav;
    if (!wav.Open(path, static_cast<uint32_t>(kRate))) return 1;
    Run(s, &wav, NULL, 0.0f);
    printf("wrote %s\n", path);
  }
  return 0;
#endif
}
