// FFloat tests.
//   Host:     accuracy of every operation against double, plus a checksum.
//   Blackfin: the same checksum (must equal the host's) and cycles per op.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "src/fixed.h"
#include "src/ffloat.h"

using namespace op1rings;

static uint32_t rng = 1;
static uint32_t Rand() {
  rng = rng * 1664525u + 1013904223u;
  return rng;
}

// A random value with a random exponent in [-40, 40] and random sign.
static FFloat RandomFFloat(bool positive = false) {
  int32_t m = static_cast<int32_t>(Rand() >> 1) | (1 << 30);
  if (!positive && (Rand() & 1)) m = -m;
  int32_t e = static_cast<int32_t>(Rand() % 81) - 40 - 30;
  return FFloat::FromParts(m, e);
}

static const int kN = 20000;
static FFloat a_vals[kN], b_vals[kN];

static uint32_t Mix(uint32_t h, FFloat x) {
  h = h * 31u + static_cast<uint32_t>(x.mantissa());
  h = h * 31u + static_cast<uint32_t>(x.is_zero() ? 0 : x.exponent());
  return h;
}

int main() {
  harness::EnableCycleCounter();
  for (int i = 0; i < kN; ++i) {
    a_vals[i] = RandomFFloat();
    b_vals[i] = RandomFFloat();
  }
  // Some exact edge cases.
  a_vals[0] = FFloat(1.0); b_vals[0] = FFloat(-1.0);
  a_vals[1] = FFloat(0.5); b_vals[1] = FFloat(0.5);
  a_vals[2] = FFloat(); b_vals[2] = FFloat(3.0);
  a_vals[3] = FFloat(1e-9); b_vals[3] = FFloat(1e9);

  uint32_t h = 0;
  for (int i = 0; i < kN; ++i) {
    FFloat a = a_vals[i], b = b_vals[i];
    h = Mix(h, a + b);
    h = Mix(h, a - b);
    h = Mix(h, a * b);
    if (!b.is_zero()) h = Mix(h, a / b);
    h = Mix(h, FFloat::Sqrt(FFloat::Abs(a)));
    h = Mix(h, FFloat::ReciprocalSqrt(FFloat::Abs(b)));
    h = h * 31u + (a < b ? 1u : 0u);
    h = h * 31u + static_cast<uint32_t>(a.ToQ(20));
  }
  printf("checksum %08x\n", static_cast<unsigned>(h));

#ifndef __bfin__
  // Accuracy against double: worst relative error per operation.
  double worst[6] = { 0, 0, 0, 0, 0, 0 };
  const char* names[6] = { "add", "sub", "mul", "div", "sqrt", "rsqrt" };
  for (int i = 0; i < kN; ++i) {
    FFloat a = a_vals[i], b = b_vals[i];
    double da = a.ToDouble(), db = b.ToDouble();
    double exact[6] = { da + db, da - db, da * db, db != 0 ? da / db : 0,
                        sqrt(fabs(da)), db != 0 ? 1.0 / sqrt(fabs(db)) : 0 };
    FFloat got[6] = { a + b, a - b, a * b, b.is_zero() ? FFloat() : a / b,
                      FFloat::Sqrt(FFloat::Abs(a)),
                      FFloat::ReciprocalSqrt(FFloat::Abs(b)) };
    for (int k = 0; k < 6; ++k) {
      if (exact[k] == 0.0) continue;
      double err = fabs(got[k].ToDouble() - exact[k]);
      // Add/sub: error relative to the larger operand (cancellation is exact
      // in float too, but the truncated LSB of the inputs is lost).
      double scale = (k < 2) ? fmax(fabs(da), fabs(db)) : fabs(exact[k]);
      double rel = err / scale;
      if (rel > worst[k]) worst[k] = rel;
    }
  }
  int failures = 0;
  for (int k = 0; k < 6; ++k) {
    bool ok = worst[k] < 1e-8;  // float's epsilon is 1.2e-7
    failures += !ok;
    printf("%-6s worst relative error %.3g %s\n", names[k], worst[k], ok ? "" : "<-- FAIL");
  }
  printf("FFloat(0.1) = %.12f\n", FFloat(0.1).ToDouble());
  return failures != 0;
#else
  // Cycle cost per operation (loop overhead included).
  volatile int32_t sink = 0;
  struct { const char* name; int op; } ops[] = {
    { "add", 0 }, { "mul", 1 }, { "div", 2 }, { "rsqrt", 3 }, { "sqrt", 4 },
    { "compare", 5 }, { "ToQ", 6 },
  };
  for (auto& o : ops) {
    uint64_t t0 = harness::ReadCycles();
    for (int i = 0; i < 1000; ++i) {
      FFloat a = a_vals[i], b = b_vals[i], r;
      switch (o.op) {
        case 0: r = a + b; break;
        case 1: r = a * b; break;
        case 2: r = a / b; break;
        case 3: r = FFloat::ReciprocalSqrt(FFloat::Abs(b)); break;
        case 4: r = FFloat::Sqrt(FFloat::Abs(a)); break;
        case 5: r = FFloat::FromInt(a < b); break;
        case 6: r = FFloat::FromInt(a.ToQ(20)); break;
      }
      sink = sink + r.mantissa();
    }
    uint64_t t1 = harness::ReadCycles();
    printf("%-8s %6.1f cycles/op (incl. loop, loads, switch)\n", o.name,
           static_cast<double>(t1 - t0) / 1000.0);
  }
  return 0;
#endif
}
