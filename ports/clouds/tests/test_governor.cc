// The governor (src/core/governor.h) against a simulated OP-1 (make
// test-governor): a 580,500-cycle audio period; the OP-1's own work 15% of
// it plus 5% for stock phone's absence of noise; synth engines whose load
// follows a script; Clouds costing a fixed part plus a cost per grain, with
// the density asking for more grains than fit. A period whose work exceeds
// it delays the next call by the overrun (a late call: what crackles).
//
// Phases: idle (no engine), heavy Rings arriving at once (65% of a
// period), heavier (75%), back to idle; then load the governor can't see
// (72%: stock engines, drums), where only late calls guide it. Checks: few late calls, and only
// in the first calls after a sudden change; the cap settles where the work
// fits with some room; it recovers to the maximum once the load goes.

#include <cstdint>
#include <cstdio>

#include "src/core/governor.h"

namespace {

const uint32_t kPeriod = 580500;
const uint32_t kBase = kPeriod * 15 / 100;
const uint32_t kCloudsFixed = 26000;      // granular without grains (simulator)
const uint32_t kPerGrain = 1100;          // a stereo grain, 64 samples (simulator)
const int32_t kDemand = 32;               // grains the density asks for

struct Phase {
  const char* name;
  int32_t calls;
  uint32_t engines;                       // cycles per period
  bool measured;                          // seen by the governor (our engines)
};

const Phase kPhases[] = {
  { "idle", 3000, 0, true },
  { "heavy Rings", 3000, kPeriod * 65 / 100, true },
  { "heavier", 3000, kPeriod * 75 / 100, true },
  { "idle again", 6000, 0, true },
  // Stock engines, drums, tape: load the governor can't see; only late
  // calls tell it.
  { "unmeasured", 6000, kPeriod * 72 / 100, false },
  { "idle last", 15000, 0, true },
};

}  // namespace

int main() {
  op1clouds::Governor g;
  g.Init(kDemand, kPeriod);
  int failures = 0;
  uint32_t interval = kPeriod;
  uint32_t own = kCloudsFixed + kDemand * kPerGrain;
  uint32_t device_ratio_x10 = 16;          // the device runs ~1.6x the simulator
  for (const Phase& ph : kPhases) {
    int32_t late = 0, late_after_settle = 0, min_cap = kDemand, max_cap = 0;
    uint32_t worst = 0;
    for (int32_t c = 0; c < ph.calls; ++c) {
      const int32_t cap = g.Update(interval, own, ph.measured ? ph.engines : 0);
      const int32_t grains = cap < kDemand ? cap : kDemand;
      own = (kCloudsFixed + grains * kPerGrain) * device_ratio_x10 / 10;
      const uint32_t work = kBase + ph.engines + own;
      interval = work > kPeriod ? work : kPeriod;
      if (work > kPeriod) {
        ++late;
        if (c > 200) ++late_after_settle;
      }
      if (c > ph.calls / 2) {
        if (cap < min_cap) min_cap = cap;
        if (cap > max_cap) max_cap = cap;
        if (work > worst) worst = work;
      }
    }
    printf("%-12s late calls %4ld (after settling %ld), cap %ld..%ld, worst load %3lu%% of a period\n",
           ph.name, static_cast<long>(late), static_cast<long>(late_after_settle),
           static_cast<long>(min_cap), static_cast<long>(max_cap),
           static_cast<unsigned long>(100ull * worst / kPeriod));
    if (ph.measured && late_after_settle) ++failures;
    // A few: one burst per re-test of the ceiling (~13 s apart).
    if (!ph.measured && late_after_settle > 20) ++failures;
    // Where the load can fit (Clouds' fixed part plus a grain), it must fit
    // with room for the UI.
    const bool feasible = kBase + ph.engines + (kCloudsFixed + kPerGrain) * device_ratio_x10 / 10 < kPeriod * 90 / 100;
    // (Load it can't see, it can't keep a reserve against: there only the late
    // calls count.)
    if (ph.measured && feasible && 100ull * worst / kPeriod > 92) ++failures;
    if (ph.measured && !feasible && min_cap != 1) ++failures;
  }
  if (g.cap() != kDemand) {
    printf("cap didn't recover: %ld\n", static_cast<long>(g.cap()));
    ++failures;
  }
  printf("%s\n", failures ? "FAIL" : "ok");
  return failures ? 1 : 0;
}
