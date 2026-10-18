// The governor (src/core/governor.h) against a simulated OP-1 (make
// test-governor): a 580,500-cycle audio period; the OP-1's own work 15% of
// it plus 5% for stock phone's absence of noise; synth engines whose load
// follows a script; Clouds costing a fixed part plus a cost per grain (the
// device's figures, Tests 30-31), with the density asking for more grains
// than fit. A period whose work exceeds it delays the next call by the
// overrun (a late call: what crackles).
//
// Phases: idle (no engine), heavy Rings arriving at once (45% of a
// period), heavier (55%), back to idle; then load the governor can't see
// (55%: stock engines, drums), where only late calls guide it. Checks: few
// late calls, and only in the first calls after a sudden change (hidden
// load: a short burst per re-test of the ceiling); the cap settles where
// the work fits with some room; it recovers to the maximum once a measured
// load goes within a second, and once hidden load goes within ~20 s (only
// a re-test can tell).

#include <cstdint>
#include <cstdio>

#include "src/core/governor.h"

namespace {

const uint32_t kPeriod = 580500;
const uint32_t kBase = kPeriod * 15 / 100;
const uint32_t kCloudsFixed = 116000;     // granular without grains (device: ~80 MHz)
const uint32_t kPerGrain = 3500;          // a stereo grain per call (device)
const int32_t kDemand = 32;               // grains the density asks for

struct Phase {
  const char* name;
  int32_t calls;
  uint32_t engines;                       // cycles per period
  bool measured;                          // seen by the governor (our engines)
};

const Phase kPhases[] = {
  { "idle", 3000, 0, true },
  { "heavy Rings", 3000, kPeriod * 45 / 100, true },
  { "heavier", 3000, kPeriod * 55 / 100, true },
  { "idle again", 3000, 0, true },
  // Stock engines, drums, tape: load the governor can't see; only late
  // calls tell it.
  { "unmeasured", 6000, kPeriod * 55 / 100, false },
  { "idle last", 15000, 0, true },
};

}  // namespace

int main() {
  op1clouds::Governor g;
  g.Init(kDemand, kPeriod);
  int failures = 0;
  uint32_t interval = kPeriod;
  int32_t grains = kDemand;
  bool previous_measured = true;
  for (const Phase& ph : kPhases) {
    int32_t late = 0, late_after_settle = 0, min_cap = kDemand, max_cap = 0;
    int32_t recovered_at = -1;
    uint32_t worst = 0;
    for (int32_t c = 0; c < ph.calls; ++c) {
      const uint32_t grain_cycles = grains * kPerGrain;
      const uint32_t own = kCloudsFixed + grain_cycles;
      const int32_t cap = g.Update(interval, own, grain_cycles, grains, ph.measured ? ph.engines : 0);
      grains = cap < kDemand ? cap : kDemand;
      const uint32_t work = kBase + ph.engines + kCloudsFixed + grains * kPerGrain;
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
      if (recovered_at < 0 && cap == kDemand) recovered_at = c;
    }
    printf("%-12s late calls %4ld (after settling %ld), cap %ld..%ld, worst load %3lu%% of a period, full cap at call %ld\n",
           ph.name, static_cast<long>(late), static_cast<long>(late_after_settle),
           static_cast<long>(min_cap), static_cast<long>(max_cap),
           static_cast<unsigned long>(100ull * worst / kPeriod), static_cast<long>(recovered_at));
    if (ph.measured && late_after_settle) ++failures;
    // A few: one burst per re-test of the ceiling.
    if (!ph.measured && late_after_settle > 40) ++failures;
    // Where the load can fit (Clouds' fixed part plus a grain), it must fit
    // with room for the UI.
    const bool feasible = kBase + ph.engines + kCloudsFixed + kPerGrain < kPeriod * 90 / 100;
    if (ph.measured && feasible && 100ull * worst / kPeriod > 92) ++failures;
    if (ph.measured && !feasible && min_cap != 1) ++failures;
    // The grains come back within a second of a measured load going.
    if (ph.engines == 0 && previous_measured && (recovered_at < 0 || recovered_at > 700)) ++failures;
    previous_measured = ph.measured;
  }
  if (g.cap() != kDemand) {
    printf("cap didn't recover: %ld\n", static_cast<long>(g.cap()));
    ++failures;
  }
  printf("%s\n", failures ? "FAIL" : "ok");
  return failures ? 1 : 0;
}
