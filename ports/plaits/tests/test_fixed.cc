// Fixed-point primitives on Blackfin against their portable references, and
// checksums of every helper the engines use: the Blackfin build (bfin-elf-run)
// and the host build must print the same lines (`make test-fixed`).

#include <cstdio>

#include "fixed/fixed_math.h"
#include "fixed/svf.h"

namespace {

uint32_t rng = 12345;
uint32_t Next() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}

uint32_t Hash(uint32_t h, uint32_t x) { return (h ^ x) * 16777619u; }

int32_t Pick() {
  // A mix of uniform values and edge cases.
  static const int32_t kEdges[] = {
    0, 1, -1, INT32_MAX, INT32_MIN, 1 << 30, -(1 << 30), 0x4000, 0x7fff, 0x8000, 0xffff,
    0x10000, -0x10000, 0x7fff0000, -0x7fff0000,
  };
  const uint32_t r = Next();
  if ((r & 7) == 0) return kEdges[(r >> 8) % (sizeof(kEdges) / sizeof(kEdges[0]))];
  return static_cast<int32_t>(Next());
}

}  // namespace

int main() {
  using namespace op1plaits;
  const int kCases = 200000;
  int mismatches = 0;
  uint32_t h_round = 2166136261u, h_mul = 2166136261u, h_frac = 2166136261u;
  for (int i = 0; i < kCases; ++i) {
    const int32_t a = Pick(), b = Pick();
    const int32_t r = MulRound(a, b);
    if (r != MulRoundReference(a, b)) {
      ++mismatches;
      printf("mismatch %ld * %ld: %ld, reference %ld\n", static_cast<long>(a), static_cast<long>(b),
             static_cast<long>(r), static_cast<long>(MulRoundReference(a, b)));
    }
    h_round = Hash(h_round, static_cast<uint32_t>(r));
    h_mul = Hash(h_mul, static_cast<uint32_t>(Mul(a, b)));
    uint32_t den = Next() | 1u;
    uint32_t num = Next() % den;
    h_frac = Hash(h_frac, static_cast<uint32_t>(Fraction(num, den)));
  }
  printf("MulRound vs reference: %d mismatches in %d\n", mismatches, kCases);
  printf("checksum MulRound %08x\n", h_round);
  printf("checksum Mul %08x\n", h_mul);
  printf("checksum Fraction %08x\n", h_frac);

  uint32_t h = 2166136261u;
  for (uint32_t x = 1; x != 0 && x < 0xf0000000u; x = x * 3 + 1) {
    h = Hash(h, static_cast<uint32_t>(CountLeadingZeros(x)));
    if (CountLeadingZeros(x) != __builtin_clz(x)) {
      ++mismatches;
      printf("clz mismatch %lx: %d vs %d\n", static_cast<unsigned long>(x), CountLeadingZeros(x), __builtin_clz(x));
    }
  }
  printf("checksum CountLeadingZeros %08x\n", h);

  h = 2166136261u;
  for (int32_t n = -(130 << 16); n < (140 << 16); n += 1237) {
    h = Hash(h, NoteToFrequency(n));
    if (n > -(120 << 16) && n < (120 << 16)) {
      const FFloat s = SemitonesToRatio(n);
      h = Hash(h, static_cast<uint32_t>(s.mantissa()));
      h = Hash(h, static_cast<uint32_t>(s.exponent()));
    }
  }
  printf("checksum pitch %08x\n", h);

  h = 2166136261u;
  for (uint32_t p = 0; p < 0xfff00000u; p += 0x00012345u) {
    h = Hash(h, static_cast<uint32_t>(Sine(p)));
  }
  printf("checksum Sine %08x\n", h);

  h = 2166136261u;
  for (uint32_t f = 1000; f < 0x78000000u; f += f / 7 + 1) {
    const int32_t g = TanQ27<FREQUENCY_ACCURATE>(f);
    h = Hash(h, static_cast<uint32_t>(g));
    h = Hash(h, static_cast<uint32_t>(TanQ27<FREQUENCY_FAST>(f)));
    h = Hash(h, static_cast<uint32_t>(TanQ27<FREQUENCY_DIRTY>(f)));
    for (int32_t q = 1 << 15; q < (2000 << 16); q *= 3) {
      h = Hash(h, static_cast<uint32_t>(SvfH(g, ResonanceToR(q))));
    }
  }
  printf("checksum filter coefficients %08x\n", h);
  printf("clz mismatches %d\n", mismatches);
  return 0;
}
