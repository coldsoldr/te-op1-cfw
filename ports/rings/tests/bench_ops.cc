// Cycle cost of primitive operations on Blackfin (bfin-elf-run): soft-float
// arithmetic, 64-bit multiply, and the fract32 multiply builtins. Used to
// decide what may stay in floating point in the per-block code.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"

using harness::ReadCycles;

static const int kN = 1000;
volatile float vf[kN];
volatile int32_t vi[kN];
volatile float sink_f;
volatile int32_t sink_i;
volatile int64_t sink_l;

#define MEASURE(name, setup, body)                                    \
  {                                                                   \
    setup;                                                            \
    uint64_t t0 = ReadCycles();                                       \
    for (int i = 0; i < kN; ++i) { body; }                            \
    uint64_t t1 = ReadCycles();                                       \
    printf("%-34s %7.1f cycles/op (incl. loop+loads)\n", name,        \
           static_cast<double>(t1 - t0) / kN);                        \
  }

int main() {
  harness::EnableCycleCounter();
  for (int i = 0; i < kN; ++i) {
    vf[i] = 0.001f * (i + 1);
    vi[i] = (i + 1) * 123457;
  }
  MEASURE("loop + 2 volatile loads (int)", , sink_i = vi[i] ^ vi[kN - 1 - i]);
  MEASURE("int32 add", , sink_i = vi[i] + vi[kN - 1 - i]);
  MEASURE("int32 mul", , sink_i = vi[i] * vi[kN - 1 - i]);
  MEASURE("int64 = int32 * int32", , sink_l = (int64_t)vi[i] * vi[kN - 1 - i]);
  MEASURE("(int64 mul) >> 31", , sink_i = (int32_t)(((int64_t)vi[i] * vi[kN - 1 - i]) >> 31));
#ifdef __bfin__
  MEASURE("__builtin_bfin_mult_fr1x32x32", , sink_i = __builtin_bfin_mult_fr1x32x32(vi[i], vi[kN - 1 - i]));
  MEASURE("__builtin_bfin_mult_fr1x32x32NS", , sink_i = __builtin_bfin_mult_fr1x32x32NS(vi[i], vi[kN - 1 - i]));
#endif
  MEASURE("int32 / int32", , sink_i = vi[i] / (vi[kN - 1 - i] | 1));
  MEASURE("float add", , sink_f = vf[i] + vf[kN - 1 - i]);
  MEASURE("float mul", , sink_f = vf[i] * vf[kN - 1 - i]);
  MEASURE("float div", , sink_f = vf[i] / vf[kN - 1 - i]);
  MEASURE("sqrtf", , sink_f = sqrtf(vf[i]));
  MEASURE("float -> int32", , sink_i = static_cast<int32_t>(vf[i] * 1000.0f));
  MEASURE("int32 -> float", , sink_f = static_cast<float>(vi[i]));
  MEASURE("float compare", , sink_i = vf[i] < vf[kN - 1 - i]);
  return 0;
}
