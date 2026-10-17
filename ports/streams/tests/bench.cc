// Checksums (host = Blackfin) and simulated cycles (bfin-elf-run) for the
// Streams port. Built per configuration (src/dsp/retime.h):
//   -DBENCH_PROCESSORS  the copied upstream processors through the drive run
//                       (harness/drive.h): one checksum per case, plus cycles
//                       per sample on Blackfin. With the unmodified
//                       configuration (31089 Hz, FAST=0) this is the
//                       bit-exactness check of upstream's integer code on the
//                       Blackfin; with the port's (44.1 kHz) the retimed one.
//   -DBENCH_CORE        the effect core over every scenario, mono or stereo
//                       as the scenario says, plus every scenario forced
//                       mono and stereo: checksums of the output, cycles per
//                       64-sample call.
// Lines starting "CHK" must be identical between the host and Blackfin
// builds (make test diffs them); "CYC" lines are Blackfin cycles.
//
//   bench [seconds]   (default 1)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/drive.h"
#include "harness/scenarios.h"
#include "harness/signals.h"
#include "src/core/streams_core.h"
#include "src/dsp/processor.h"

#include "../../rings/harness/cycles.h"

using namespace harness;

#ifdef BENCH_PROCESSORS
static op1streams::Processor processor;

struct ChecksumSink {
  uint32_t h;
  void operator()(int32_t, uint16_t g, uint16_t f) { h = Fnv(h, g | (static_cast<uint32_t>(f) << 16)); }
};

static void BenchProcessors(double seconds) {
  static const char* const kFunctions[] = {
    "envelope", "vactrol", "follower", "compressor", "filter_ctrl", "lorenz" };
  const int32_t rate = OP1STREAMS_SAMPLE_RATE;
  const int32_t samples = static_cast<int32_t>(seconds * rate);
  for (int k = 0; k < NumDriveCases(); ++k) {
    DriveCase c = GetDriveCase(k);
    memset(static_cast<void*>(&processor), 0, sizeof(processor));
    ChecksumSink sink = { 2166136261u };
    uint64_t t0 = ReadCycles();
    Drive(&processor, c, rate, samples, sink);
    uint64_t t1 = ReadCycles();
    printf("CHK proc rate %d fast %d stride %d %-12s %-9s %-6s %08lx\n", static_cast<int>(rate),
           OP1STREAMS_FAST, OP1STREAMS_CENTROID_STRIDE, kFunctions[c.function],
           c.alternate ? "alternate" : "normal", c.linked ? "linked" : "single",
           static_cast<unsigned long>(sink.h));
#ifdef __bfin__
    // Includes the test signal synthesis (~60-100 cycles per sample).
    printf("CYC proc %-12s %-9s %-6s %6.0f cycles/sample (incl. signal)\n", kFunctions[c.function],
           c.alternate ? "alternate" : "normal", c.linked ? "linked" : "single",
           static_cast<double>(t1 - t0) / samples);
#endif
  }
}
#endif  // BENCH_PROCESSORS

#ifdef BENCH_CORE
static op1streams::StreamsCore core;

// stereo: 0 = mono, 1 = stereo.
static void BenchCore(const Scenario& s, int stereo, double seconds) {
  Controls c;
  ScenarioControls(s, &c);
  Signal signal;
  signal.Init(s.material, 44100, stereo != 0);
  core.Init();
  const int32_t blocks = static_cast<int32_t>(seconds * 44100) / op1streams::kBlockSize;
  int16_t l[op1streams::kBlockSize], r[op1streams::kBlockSize];
  uint32_t h = 2166136261u;
  CycleStats stats;
  for (int32_t b = 0; b < blocks; ++b) {
    for (int i = 0; i < op1streams::kBlockSize; ++i) signal.Next(&l[i], &r[i]);
    uint64_t t0 = ReadCycles();
    if (stereo) {
      core.ProcessStereo(c, l, r, op1streams::kBlockSize);
    } else {
      core.ProcessMono(c, l, op1streams::kBlockSize);
    }
    uint64_t t1 = ReadCycles();
    if (b > 0) stats.Add(t1 - t0);  // the first call configures everything
    for (int i = 0; i < op1streams::kBlockSize; ++i) {
      h = Fnv(h, static_cast<uint16_t>(l[i]) | (stereo ? static_cast<uint32_t>(static_cast<uint16_t>(r[i])) << 16 : 0));
    }
  }
  printf("CHK core %-22s %-6s %08lx\n", s.name, stereo ? "stereo" : "mono",
         static_cast<unsigned long>(h));
#ifdef __bfin__
  double avg = static_cast<double>(stats.total) / stats.n;
  // 44100 / 64 = 689 calls per second.
  printf("CYC core %-22s %-6s avg %6.0f max %6lu cycles/call  %5.2f MHz avg %5.2f MHz peak (simulator)",
         s.name, stereo ? "stereo" : "mono", avg, static_cast<unsigned long>(stats.max),
         avg * 44100.0 / 64 / 1e6, stats.max * 44100.0 / 64 / 1e6);
#ifdef OP1STREAMS_PROFILE
  printf("  [detect %.0f proc %.0f coef %.0f audio %.0f]",
         static_cast<double>(core.profile[0]) / blocks, static_cast<double>(core.profile[1]) / blocks,
         static_cast<double>(core.profile[2]) / blocks, static_cast<double>(core.profile[3]) / blocks);
#endif
  printf("\n");
#endif
}
#endif  // BENCH_CORE

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 1.0;
  EnableCycleCounter();
#ifdef BENCH_PROCESSORS
  BenchProcessors(seconds);
#endif
#ifdef BENCH_CORE
  for (int k = 0; k < kNumScenarios; ++k) {
    BenchCore(kScenarios[k], kScenarios[k].stereo ? 1 : 0, seconds);
  }
  for (int k = 0; k < kNumScenarios; ++k) {
    BenchCore(kScenarios[k], kScenarios[k].stereo ? 0 : 1, seconds);
  }
#endif
  return 0;
}
