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

  // Saturating shift and add: builtins vs the portable formulas.
  int shl_fail = 0, add_fail = 0;
  for (int i = 0; i < 200000; ++i) {
    int32_t a = Rand(), b = Rand();
    if (i & 1) a >>= (i >> 1) & 31;
    int shift = 1 + (i % 4);
    int64_t r = static_cast<int64_t>(a) << shift;
    int32_t want = r > INT32_MAX ? INT32_MAX
                 : (r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r));
    // Constant shifts too: GCC 14's bfin builtin got these wrong.
    int64_t r1 = static_cast<int64_t>(a) * 2;
    int32_t want1 = r1 > INT32_MAX ? INT32_MAX
                  : (r1 < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r1));
    if (ShlSat(a, 1) != want1 && shl_fail++ < 5)
      printf("ShlSat(%08x, 1): %08x want %08x\n", (unsigned)a,
             (unsigned)ShlSat(a, 1), (unsigned)want1);
    if (ShlSat(a, shift) != want && shl_fail++ < 5)
      printf("ShlSat(%08x, %d): %08x want %08x\n", (unsigned)a, shift,
             (unsigned)ShlSat(a, shift), (unsigned)want);
    int64_t s64 = static_cast<int64_t>(a) + b;
    int32_t want_add = s64 > INT32_MAX ? INT32_MAX
                     : (s64 < INT32_MIN ? INT32_MIN : static_cast<int32_t>(s64));
    if (AddSat(a, b) != want_add && add_fail++ < 5)
      printf("AddSat(%08x, %08x): %08x want %08x\n", (unsigned)a, (unsigned)b,
             (unsigned)AddSat(a, b), (unsigned)want_add);
  }
  printf("ShlSat: %d mismatches, AddSat: %d mismatches\n", shl_fail, add_fail);
  return (failures + shl_fail + add_fail) != 0;
}
