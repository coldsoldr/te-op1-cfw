// Benchmarks the modal resonator's per-sample loop for a worst-case 60-mode
// bank. Prints a checksum per implementation: the host build (C) and the
// Blackfin build (C and assembly) must all print the same value. A third of
// the modes have a still weight (increment 0: the assembly's other loop),
// and a second run sums both outputs into one buffer (out == aux).

//   make bench-mode-bank   (Blackfin; compares with the host build)

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "src/fixed.h"
#include "src/mode_bank.h"

using namespace op1rings;

static const double kPi = 3.14159265358979323846;

static const int kModes = 60;
static const int kBlock = 24;
static const int kBlocks = 200;

static Mode modes[kModes];

static void InitModes() {
  // A 110 Hz harmonic series at 44.1 kHz, moderate damping, varying weights.
  for (int m = 0; m < kModes; ++m) {
    double fc = 110.0 * (m + 1) / 44100.0;
    if (fc > 0.49) fc = 0.49;
    double f = 2.0 * sin(kPi * fc);
    modes[m].half_f = static_cast<int32_t>(f * 0.5 * 2147483647.0);
    modes[m].half_q = static_cast<int32_t>(0.00025 * (m + 1) * 2147483647.0 / kModes);
    modes[m].weight = static_cast<int32_t>(0.4 * cos(0.3 * m) * 2147483647.0);
    modes[m].weight_increment = m % 3 == 0 ? 0 : ((m & 1) ? 1000 : -1000);
    modes[m].lp = modes[m].bp = 0;
  }
}

// mono: out and aux are the same buffer (the OP-1 voice's mono render).
template<typename Fn>
static void Run(const char* name, Fn process, bool mono) {
  InitModes();
  int32_t in[kBlock], out[kBlock], aux[kBlock];
  int32_t previous_input = 0;
  uint32_t checksum = 0;
  harness::CycleStats stats;
  for (int b = 0; b < kBlocks; ++b) {
    for (int s = 0; s < kBlock; ++s) {
      in[s] = (b % 50 == 0 && s == 0) ? (1 << 24) : ((s * 7919 + b) & 0xff) - 128;
    }
    for (int m = 0; m < kModes; ++m) {
      modes[m].weight_increment = -modes[m].weight_increment;
    }
    uint64_t t0 = harness::ReadCycles();
    process(modes, kModes, in, &previous_input, out, mono ? out : aux, kBlock);
    uint64_t t1 = harness::ReadCycles();
    stats.Add(t1 - t0);
    for (int s = 0; s < kBlock; ++s) {
      checksum = checksum * 31u + static_cast<uint32_t>(out[s]);
      if (!mono) checksum = checksum * 31u + static_cast<uint32_t>(aux[s]);
    }
  }
  for (int m = 0; m < kModes; ++m) {
    checksum = checksum * 31u + static_cast<uint32_t>(modes[m].lp);
    checksum = checksum * 31u + static_cast<uint32_t>(modes[m].bp);
    checksum = checksum * 31u + static_cast<uint32_t>(modes[m].weight);
  }
  printf("%-10s checksum %08x\n", name, static_cast<unsigned>(checksum));
#ifdef __bfin__
  stats.Print(name, kBlock);
  printf("  %.1f cycles per mode per sample\n",
         static_cast<double>(stats.total) / stats.n / kBlock / kModes);
#endif
}

static void ProcessC(Mode* m, int32_t n, const int32_t* in, int32_t* prev,
                     int32_t* out, int32_t* aux, int32_t size) {
  ModeBankProcessC(m, n, in, prev, out, aux, static_cast<size_t>(size));
}

int main() {
  harness::EnableCycleCounter();
  Run("C", ProcessC, false);
#ifdef __bfin__
  Run("asm", ModeBankProcessAsm, false);
#endif
  Run("C mono", ProcessC, true);
#ifdef __bfin__
  Run("asm mono", ModeBankProcessAsm, true);
#endif
  return 0;
}
