// The checks every further engine gets on its host (sim_fx.cc): called as
// the OP-1 calls an effect, the marker in values[4], phone's encoder ranges
// (sim_ranges), a saw with a few quiet calls in front.
//   - mix 0: the dry signal (bit-exact for an in-place core; a half-rate one
//     passes the soft limiter: ~10 LSB at -12 dBFS);
//   - mix 1: something else than the dry signal, and no runaway (the peak);
//   - the cost, stereo and mono, against a budget (MHz at 689 calls a
//     second, as the simulator counts).
// An engine whose OP1FX_ flag isn't set isn't in the build and isn't tested.

#ifndef OP1CLOUDS_TESTS_SIM_ENGINES_H_
#define OP1CLOUDS_TESTS_SIM_ENGINES_H_

#include <cstdint>
#include <cstdio>

#include "harness/cycles.h"
#include "fx/op1/fx_host.h"

namespace sim_engines {

typedef void (*MonoFn)(void* self, int16_t* buf, const int16_t* params);
typedef void (*StereoFn)(void* self, int16_t* l, int16_t* r, const int16_t* params);

struct Case {
  const char* name;
  MonoFn mono;
  StereoFn stereo;
  int32_t id;
  int16_t knobs[3];      // blue, green, white (encoder values, phone's ranges)
  bool in_place;         // mix 0 must be bit-exact
  double budget_stereo;  // MHz
  double budget_mono;
};

struct Result {
  int64_t dry_error;     // sum |out - dry| at mix 0, after the first call
  int64_t wet_change;    // sum |out - dry| at mix 1
  int32_t peak;
  double mhz;
};

inline Result Run(const Case& c, int16_t mix, bool stereo, int32_t calls) {
  int16_t v[8] = { c.knobs[0], c.knobs[1], c.knobs[2], mix,
                   static_cast<int16_t>(op1fx::kMarker | c.id << op1fx::kEngineShift), 0, 0, 0 };
  Result res = { 0, 0, 0, 0 };
  {
    // One stock call first: the host stops the engine, so this run starts
    // it afresh (no tail or mix ramp from the previous run).
    int16_t stock[8] = { c.knobs[0], c.knobs[1], c.knobs[2], mix, 0, 0, 0, 0 };
    int16_t l[op1fx::kFxBlockSize] = { 0 }, r[op1fx::kFxBlockSize] = { 0 };
    if (stereo) {
      c.stereo(nullptr, l, r, stock);
    } else {
      c.mono(nullptr, l, stock);
    }
  }
  uint64_t cycles = 0;
  uint32_t phase = 0, seed = 1;
  for (int32_t n = 0; n < calls; ++n) {
    int16_t l[op1fx::kFxBlockSize], r[op1fx::kFxBlockSize], dry[op1fx::kFxBlockSize];
    for (int32_t i = 0; i < op1fx::kFxBlockSize; ++i) {
      phase += 1 << 24;   // ~170 Hz saw, -12 dBFS, and a little noise in the right
      seed = seed * 1664525u + 1013904223u;
      const int16_t x = n < 4 ? 0 : static_cast<int16_t>(static_cast<int32_t>(phase) >> 18);
      dry[i] = l[i] = x;
      r[i] = static_cast<int16_t>(x + (static_cast<int32_t>(seed) >> 24));
    }
    const uint64_t t0 = harness::ReadCycles();
    if (stereo) {
      c.stereo(nullptr, l, r, v);
    } else {
      c.mono(nullptr, l, v);
    }
    if (n > 0) cycles += harness::ReadCycles() - t0;
    for (int32_t i = 0; i < op1fx::kFxBlockSize; ++i) {
      const int32_t d = l[i] - dry[i];
      const int32_t ad = d < 0 ? -d : d;
      if (n > 0) {
        res.dry_error += ad;
        res.wet_change += ad;
      }
      const int32_t a = l[i] < 0 ? -l[i] : l[i];
      if (a > res.peak) res.peak = a;
    }
  }
  res.mhz = cycles / static_cast<double>(calls - 1) * 689 / 1e6;
  return res;
}

// Returns the number of failed checks.
inline int Test(const Case& c) {
  int failed = 0;
  auto check = [&](bool ok, const char* what) {
    printf("%s %s: %s\n", ok ? "ok  " : "FAIL", c.name, what);
    if (!ok) ++failed;
  };
  const int32_t kCalls = 60;
  for (int32_t s = 0; s < 2; ++s) {
    const bool stereo = s == 1;
    const Result dry = Run(c, 0, stereo, kCalls);
    const Result wet = Run(c, 32767, stereo, kCalls);
    printf("     %s %s: mix 0 |out - dry| %ld; mix 1 change %ld, peak %ld\n", c.name,
           stereo ? "stereo" : "mono", static_cast<long>(dry.dry_error),
           static_cast<long>(wet.wet_change), static_cast<long>(wet.peak));
    if (c.in_place) {
      check(dry.dry_error == 0, stereo ? "stereo mix 0 is the input, bit-exact" : "mono mix 0 is the input, bit-exact");
    } else {
      // The soft limiter colours the dry side by ~10 LSB a sample at -12 dBFS.
      check(dry.dry_error < 16 * op1fx::kFxBlockSize * kCalls,
            stereo ? "stereo mix 0 is the dry signal" : "mono mix 0 is the dry signal");
    }
    check(wet.wet_change > 100 * op1fx::kFxBlockSize, stereo ? "stereo mix 1 changes the sound" : "mono mix 1 changes the sound");
    const Result cost = Run(c, 16384, stereo, 400);
    printf("     %s %s: %.2f MHz (budget %.1f)\n", c.name, stereo ? "stereo" : "mono", cost.mhz,
           stereo ? c.budget_stereo : c.budget_mono);
    check(cost.mhz <= (stereo ? c.budget_stereo : c.budget_mono), stereo ? "stereo within budget" : "mono within budget");
  }
  return failed;
}

}  // namespace sim_engines

#endif  // OP1CLOUDS_TESTS_SIM_ENGINES_H_
