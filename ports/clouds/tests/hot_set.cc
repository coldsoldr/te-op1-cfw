// The granular master effect's per-call code footprint (make hot-set):
// 40 calls of the master_gr_full scenario (density .8, texture .9, feedback
// .3, reverb .5), with a marker call around each of the last four, traced
// with bfin-elf-run --trace-insn; tools/hot_set.py counts the distinct
// 32-byte code lines a call executes (what the OP-1's 16 KB instruction
// cache must hold) and attributes them to functions.

#include <cstdint>

#include "op1/clouds_fx.h"

extern "C" __attribute__((noinline)) void HotSetMarker() {
  __asm__ __volatile__("NOP;" ::: "memory");
}

int main() {
  int16_t l[op1fx::kFxBlockSize], r[op1fx::kFxBlockSize];
  const int16_t values[8] = { 6300, 10240, 9216, 26214, (1 << 1) | (819 << 4), 0, 9830, 16384 };
  uint32_t x = 12345, phase = 0;
  for (int c = 0; c < 40; ++c) {
    for (int i = 0; i < op1fx::kFxBlockSize; ++i) {
      x = x * 1664525u + 1013904223u;
      phase += 1 << 25;
      l[i] = static_cast<int16_t>((static_cast<int32_t>(phase) >> 18) + (static_cast<int32_t>(x) >> 21));
      r[i] = static_cast<int16_t>((static_cast<int32_t>(phase) >> 19) - (static_cast<int32_t>(x) >> 21));
    }
    if (c >= 36) HotSetMarker();
    CloudsFxStereo(nullptr, l, r, values);
  }
  HotSetMarker();
  return 0;
}
