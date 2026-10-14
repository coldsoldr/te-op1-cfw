// Cycle counting for benchmarks. On Blackfin this reads the core CYCLES
// counter (the simulator counts roughly one cycle per instruction plus some
// stalls; it doesn't model cache misses or SDRAM latency). On the host it
// returns 0, since host timings say nothing about the OP-1.

#ifndef PORTS_RINGS_HARNESS_CYCLES_H_
#define PORTS_RINGS_HARNESS_CYCLES_H_

#include <cstdint>
#include <cstdio>

namespace harness {

inline uint64_t ReadCycles() {
#ifdef __bfin__
  uint32_t lo, hi;
  // Reading CYCLES latches CYCLES2.
  __asm__ __volatile__("%0 = CYCLES; %1 = CYCLES2;" : "=d"(lo), "=d"(hi));
  return (static_cast<uint64_t>(hi) << 32) | lo;
#else
  return 0;
#endif
}

inline void EnableCycleCounter() {
#ifdef __bfin__
  uint32_t syscfg;
  __asm__ __volatile__("%0 = SYSCFG;" : "=d"(syscfg));
  syscfg |= 2;  // CCEN
  __asm__ __volatile__("SYSCFG = %0; CSYNC;" : : "d"(syscfg));
#endif
}

struct CycleStats {
  CycleStats() : n(0), total(0), min(~0ULL), max(0) { }
  void Add(uint64_t c) {
    ++n;
    total += c;
    if (c < min) min = c;
    if (c > max) max = c;
  }
  // Prints cycles per block and the MHz needed at 44.1 kHz for one instance.
  void Print(const char* name, size_t block) const {
    if (!n) return;
    double avg = static_cast<double>(total) / n;
    double mhz_avg = avg / block * 44100.0 / 1e6;
    double mhz_max = static_cast<double>(max) / block * 44100.0 / 1e6;
    printf("%-26s blocks %6lu  cyc/block avg %9.0f min %9lu max %9lu"
           "  => %7.1f MHz avg, %7.1f MHz peak @44.1k\n",
           name, static_cast<unsigned long>(n), avg,
           static_cast<unsigned long>(min), static_cast<unsigned long>(max),
           mhz_avg, mhz_max);
  }
  uint64_t n, total, min, max;
};

}  // namespace harness

#endif  // PORTS_RINGS_HARNESS_CYCLES_H_
