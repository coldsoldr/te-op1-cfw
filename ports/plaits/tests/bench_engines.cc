// The fixed-point engines at the OP-1 configuration (PLAITS_SAMPLE_RATE =
// 44100, PLAITS_BLOCK_SIZE blocks, 64 samples per OP-1 call), on Blackfin
// (bfin-elf-run) and on the host:
//   - "checksum" lines: a hash of every output sample per engine and
//     scenario; the Blackfin build must print the same as the host build;
//   - "cycles" lines (Blackfin only): simulated cycles per 64-sample call
//     (one 128-sample OP-1 render call, before the 2x upsampler) and the
//     MHz for one voice at 689 calls per second.
//
// Usage: bench_engines [engine] [seconds]

#include <cstdio>
#include <cstdlib>

#include "../../rings/harness/cycles.h"
#include "fixed/engines.h"
#include "harness/scenarios.h"

namespace {

const int kCallSize = 64;   // samples per OP-1 call at 44.1 kHz
const int kKinds[] = { harness::KIND_NOTES, harness::KIND_COMBO, harness::KIND_TRIGGER };

op1plaits::EngineHost host;
int32_t out[kCallSize];
int32_t aux[kCallSize];

uint32_t Hash(uint32_t h, int32_t x) {
  h ^= static_cast<uint32_t>(x);
  h *= 16777619u;
  return h;
}

}  // namespace

int main(int argc, char** argv) {
  harness::EnableCycleCounter();
  int first = 0, last = op1plaits::kNumEngines - 1;
  if (argc >= 2 && atoi(argv[1]) >= 0) first = last = atoi(argv[1]);
  float seconds = argc >= 3 ? static_cast<float>(atof(argv[2])) : 0.5f;
  const int32_t block = op1plaits::kBlockSize;
  const int32_t blocks_per_second = op1plaits::kSampleRate / block;
  const int32_t calls = static_cast<int32_t>(seconds * op1plaits::kSampleRate / kCallSize);
  const double calls_per_second = static_cast<double>(op1plaits::kSampleRate) / kCallSize;
  for (int engine = first; engine <= last; ++engine) {
    if (!op1plaits::EngineConverted(engine)) continue;
    uint64_t worst = 0, total = 0;
    int32_t n = 0;
    for (int kind : kKinds) {
      host.Init(engine);
      uint32_t h = 2166136261u;
      uint64_t kind_total = 0, kind_max = 0;
      int32_t b = 0;
      const int32_t total_blocks = calls * (kCallSize / block);
      op1plaits::FixedParameters params[kCallSize];
      for (int32_t c = 0; c < calls; ++c) {
        // The scenario's parameters first (its divisions aren't the engine's).
        for (int32_t s = 0; s < kCallSize / block; ++s) {
          harness::BlockParams bp;
          harness::ScenarioParams(kind, b++, blocks_per_second, total_blocks, &bp);
          op1plaits::FixedParameters& p = params[s];
          p.trigger = bp.trigger;
          p.note = bp.note << 8;
          p.harmonics = bp.harmonics;
          p.timbre = bp.timbre;
          p.morph = bp.morph;
          p.accent = bp.accent;
        }
        const uint64_t start = harness::ReadCycles();
        for (int32_t s = 0; s < kCallSize / block; ++s) {
          host.Render(params[s], out + s * block, aux + s * block, block);
        }
        const uint64_t elapsed = harness::ReadCycles() - start;
        kind_total += elapsed;
        if (elapsed > kind_max) kind_max = elapsed;
        for (int i = 0; i < kCallSize; ++i) {
          h = Hash(h, out[i]);
          h = Hash(h, aux[i]);
        }
      }
      printf("checksum %2d %-12s %-7s %08x\n", engine, harness::kEngineNames[engine],
             harness::kKindNames[kind], static_cast<unsigned>(h));
#ifdef __bfin__
      printf("cycles   %2d %-12s %-7s avg %7.0f max %7lu per call  => %5.1f MHz avg, %5.1f peak\n",
             engine, harness::kEngineNames[engine], harness::kKindNames[kind],
             static_cast<double>(kind_total) / calls, static_cast<unsigned long>(kind_max),
             static_cast<double>(kind_total) / calls * calls_per_second / 1e6,
             static_cast<double>(kind_max) * calls_per_second / 1e6);
#endif
      total += kind_total;
      n += calls;
      if (kind_max > worst) worst = kind_max;
    }
#ifdef __bfin__
    printf("summary  %2d %-12s avg %5.1f MHz per voice, peak %5.1f MHz (6 voices avg: %5.1f MHz)\n",
           engine, harness::kEngineNames[engine],
           static_cast<double>(total) / n * calls_per_second / 1e6,
           static_cast<double>(worst) * calls_per_second / 1e6,
           6.0 * static_cast<double>(total) / n * calls_per_second / 1e6);
#endif
  }
  return 0;
}
