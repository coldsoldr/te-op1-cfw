// Cycle counting for benchmarks (same approach as ports/rings/harness/cycles.h).
// On Blackfin this reads the core CYCLES counter; the simulator counts about
// one cycle per instruction plus some stalls and doesn't model the cache or
// SDRAM latency (the device has run ~1.5-1.8x the simulator's counts). On the
// host it returns 0.

#ifndef PORTS_MARBLES_HARNESS_CYCLES_H_
#define PORTS_MARBLES_HARNESS_CYCLES_H_

#include <cstdint>

namespace harness {

inline uint64_t ReadCycles() {
#ifdef __bfin__
  uint32_t lo, hi;
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
  CycleStats() : n(0), total(0), max(0) { }
  void Add(uint64_t c) {
    ++n;
    total += c;
    if (c > max) max = c;
  }
  uint64_t n, total, max;
};

}  // namespace harness

#endif  // PORTS_MARBLES_HARNESS_CYCLES_H_
