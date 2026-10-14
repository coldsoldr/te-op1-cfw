// Benchmarks the fixed-point ModalResonator (coefficients + mode bank) and
// prints a checksum that must match between the host and Blackfin builds.
//   steady:  parameters constant (coefficients cached)
//   knob:    damping changes every block (coefficients + state translation)
//   strum:   frequency changes every block

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "src/fixed.h"
#include "src/ffloat.h"
#include "src/modal_resonator.h"

using namespace op1rings;

static const int kBlock = 32;
static const int kBlocks = 120;
static ModalResonator resonator;

static uint32_t Run(const char* name, int mode) {
  resonator.Init();
  resonator.set_resolution(60);
  resonator.set_frequency(FFloat(110.0 / 44100.0).ToQ31());
  resonator.set_structure(FFloat(0.3).ToQ31());
  resonator.set_brightness(FFloat(0.36).ToQ31());
  resonator.set_damping(FFloat(0.7).ToQ31());
  resonator.set_position(FFloat(0.3).ToQ31());
  int32_t in[kBlock], out[kBlock], aux[kBlock];
  uint32_t checksum = 0;
  harness::CycleStats stats;
  for (int b = 0; b < kBlocks; ++b) {
    for (int s = 0; s < kBlock; ++s) {
      in[s] = (b % 40 == 0 && s == 0) ? kQ27One : 0;
    }
    if (mode == 1) resonator.set_damping(FFloat(0.7).ToQ31() + b * 1000000);
    if (mode == 2) resonator.set_frequency(FFloat(110.0 / 44100.0).ToQ31() + (b & 7) * 200000);
    uint64_t t0 = harness::ReadCycles();
    resonator.Process(in, out, aux, kBlock);
    uint64_t t1 = harness::ReadCycles();
    if (b > 0) stats.Add(t1 - t0);   // skip the first (initial) block
    for (int s = 0; s < kBlock; ++s) {
      checksum = checksum * 31u + static_cast<uint32_t>(out[s]);
      checksum = checksum * 31u + static_cast<uint32_t>(aux[s]);
    }
  }
  printf("%-8s checksum %08x  modes %d\n", name, static_cast<unsigned>(checksum),
         resonator.num_modes());
#ifdef __bfin__
  stats.Print(name, kBlock);
#endif
  return checksum;
}

int main() {
  harness::EnableCycleCounter();
  Run("steady", 0);
  Run("knob", 1);
  Run("strum", 2);
  return 0;
}
