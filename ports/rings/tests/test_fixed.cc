// Checks that the host emulation of the fixed-point primitives matches the
// Blackfin builtins bit for bit. Run in bfin-elf-run.

#include <cstdint>
#include <cstdio>

#include "src/fixed.h"

using namespace op1rings;

static uint32_t rng = 12345;
static int32_t Rand() {
  rng = rng * 1664525u + 1013904223u;
  return static_cast<int32_t>(rng);
}

int main() {
  const int32_t edges[] = { 0, 1, -1, INT32_MAX, INT32_MIN, 0x10000, -0x10000,
                            0xffff, -0xffff, 0x7fff0000, 0x8000, -0x8000 };
  int failures = 0, checked = 0;
  for (int32_t a : edges) {
    for (int32_t b : edges) {
      ++checked;
      if (MulQ31(a, b) != MulQ31Reference(a, b)) {
        if (failures++ < 10)
          printf("MISMATCH a %08x b %08x: hw %08x ref %08x\n", (unsigned)a,
                 (unsigned)b, (unsigned)MulQ31(a, b), (unsigned)MulQ31Reference(a, b));
      }
    }
  }
  for (int i = 0; i < 200000; ++i) {
    int32_t a = Rand(), b = Rand();
    if (i & 1) b >>= (i >> 1) & 31;
    ++checked;
    if (MulQ31(a, b) != MulQ31Reference(a, b)) {
      if (failures++ < 10)
        printf("MISMATCH a %08x b %08x: hw %08x ref %08x\n", (unsigned)a,
               (unsigned)b, (unsigned)MulQ31(a, b), (unsigned)MulQ31Reference(a, b));
    }
  }
  printf("MulQ31: %d checked, %d mismatches\n", checked, failures);
  return failures != 0;
}
