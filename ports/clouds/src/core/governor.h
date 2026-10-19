// CPU governor for Clouds on the OP-1: keeps the granular mode's grain
// count within the CPU the audio thread can spare, so Clouds degrades (fewer
// grains) instead of the audio crackling.
//
// Called once per effect call (64 samples) with:
//   blocks:       audio blocks (the firmware's block counter) since the
//                 previous call: 1, or more when the audio thread fell a
//                 block behind;
//   interval:     cycles since the previous call's start;
//   own:          Clouds' cycles in the previous call;
//   grain_cycles: of those, the grain overlap-add (the part that scales
//                 with the grain count);
//   grains:       the grains that overlap-add mixed;
//   engines:      the synth engines' render cycles since the previous call
//                 (our image measures its engines; stock engines, drums and
//                 tape it can't see).
//
// Clouds' cost is a fixed part (recording, resampling, reverb, diffuser:
// ~80 MHz on the device, Tests 30-31) plus a cost per grain. The first
// version cut the cap in proportion to the whole cost, so one Rings peak
// collapsed it to 1 and a ceiling held it there for ~17 s (Test 31: one
// grain next to Rings). Now the fixed part and the cost per grain are
// tracked apart, and the cap is what the grain budget buys:
//   budget = share x period - engines - fixed;  cap = budget / per_grain.
// The share (kDefaultShare: Test 16 found ~77% of a period free with stock
// phone running, the UI thread included; Test 32 ran at the edge with
// 0.75) leaves the firmware's own work and a reserve. A smaller cap
// applies at once; a larger one grows by a grain every kGrowCalls calls
// (~12 ms).
//
// Feedback, for the load it can't see: an overrun shows as a call that
// comes two audio blocks after the previous one (the block counter the
// firmware's audio callback keeps), or as a mean call interval over ~64
// calls above the nominal period by 2% (Test 33: the block counter
// counts the blocks the audio thread processes, so when it drops blocks
// under overload the counter falls behind real time instead of
// skipping, and only the interval shows it: the probe's clock read 460
// for 399 MHz). Test 32 tried the interval against a learnt period with a
// 1/1024 threshold: the firmware's jitter made it fire thousands of
// times a minute and pin the cap at 1; the nominal period and a 2%
// margin are above the jitter (the 64-call mean's noise is ~0.1%). An
// overrun sets a ceiling on the grain cycles at 7/8 of what they were,
// with a short cooldown. Only a re-test can tell that hidden load has gone, so the
// ceiling relaxes slowly (1/1024 every kRelaxCalls calls, ~1% a second):
// a steady hidden load costs a short burst every few seconds; the ceiling
// goes once it passes the point of the last overload by 1/32, or the
// budget.
//
// Plain integer code; tests/test_governor.cc runs it against a simulated
// OP-1.

#ifndef OP1CLOUDS_CORE_GOVERNOR_H_
#define OP1CLOUDS_CORE_GOVERNOR_H_

#include <cstdint>

namespace op1clouds {

class Governor {
 public:
  static const int32_t kDefaultShare = 45875;   // Q16 of the period: 0.70
  static const int32_t kGrowCalls = 8;          // ~12 ms
  static const int32_t kRelaxCalls = 64;        // ~0.1 s
  static const uint32_t kNoCeiling = 0xffffffffu;
  static const uint32_t kInitialPerGrain = 3000;   // a stereo grain, 32 samples (device)

  void Init(int32_t max_grains, uint32_t nominal_period, int32_t share = kDefaultShare) {
    share_ = share;
    max_grains_ = max_grains;
    cap_ = max_grains;
    period_ = nominal_period;
    engines_ = 0;
    fixed_ = 0;
    per_grain_ = kInitialPerGrain;
    mean_interval_ = nominal_period;
    cooldown_ = 0;
    ceiling_ = kNoCeiling;
    overload_point_ = 0;
    calls_ = 0;
    grow_counter_ = 0;
    late_calls_ = 0;
  }

  // Returns the grain cap for the next call.
  int32_t Update(uint32_t blocks, uint32_t interval, uint32_t own, uint32_t grain_cycles,
                 int32_t grains, uint32_t engines) {
    // The engines' and the fixed costs, smoothed (they rise at once and
    // fall over ~16 calls); the cost per grain, from the calls that had
    // grains (falls over ~8 calls).
    Track(&engines_, engines);
    Track(&fixed_, own > grain_cycles ? own - grain_cycles : 0);
    if (grains > 0) {
      const uint32_t per_grain = grain_cycles / static_cast<uint32_t>(grains);
      if (per_grain > per_grain_) {
        per_grain_ = per_grain;
      } else {
        per_grain_ -= (per_grain_ - per_grain) >> 3;
      }
      if (per_grain_ < 1) per_grain_ = 1;
    }

    // Overload: the audio thread fell a block behind, or runs its blocks
    // slower than real time (a 5 ms gap restarts the instance before it
    // gets here, so one interval can't swamp the mean).
    mean_interval_ += (static_cast<int32_t>(interval) - static_cast<int32_t>(mean_interval_)) >> 6;
    if (cooldown_) --cooldown_;
    const bool slow = mean_interval_ > period_ + period_ / 50;
    if ((blocks > 1 || slow) && !cooldown_) {
      ++late_calls_;
      cooldown_ = slow ? 64 : 16;
      if (slow) mean_interval_ = period_;
      const uint32_t used = grain_cycles > per_grain_ ? grain_cycles : per_grain_;
      overload_point_ = used;
      ceiling_ = used - (used >> 3);
      grow_counter_ = 0;
    }

    // The grain budget, under the ceiling while there is one.
    const uint32_t share = static_cast<uint32_t>((static_cast<uint64_t>(period_) * share_) >> 16);
    const uint32_t spent = engines_ + fixed_;
    uint32_t budget = share > spent ? share - spent : 0;
    if (ceiling_ != kNoCeiling) {
      if (++calls_ >= kRelaxCalls) {
        calls_ = 0;
        ceiling_ += (ceiling_ >> 10) + 1;
      }
      if (ceiling_ >= budget || ceiling_ > overload_point_ + (overload_point_ >> 5)) {
        ceiling_ = kNoCeiling;
      } else {
        budget = ceiling_;
      }
    }
    int32_t want = static_cast<int32_t>(budget / per_grain_);
    if (want < 1) want = 1;
    if (want > max_grains_) want = max_grains_;

    if (want < cap_) {
      cap_ = want;
      grow_counter_ = 0;
    } else if (want > cap_) {
      if (++grow_counter_ >= kGrowCalls) {
        ++cap_;
        grow_counter_ = 0;
      }
    } else {
      grow_counter_ = 0;
    }
    return cap_;
  }

  int32_t cap() const { return cap_; }
  int32_t late_calls() const { return late_calls_; }
  // The mean call interval, in permille of the nominal period.
  uint32_t mean_interval_permille() const {
    return static_cast<uint32_t>((static_cast<uint64_t>(mean_interval_) * 1000) / period_);
  }
  uint32_t ceiling() const { return ceiling_; }
  uint32_t fixed() const { return fixed_; }
  uint32_t per_grain() const { return per_grain_; }

 private:
  static void Track(uint32_t* smoothed, uint32_t value) {
    if (value > *smoothed) {
      *smoothed = value;
    } else {
      *smoothed -= (*smoothed - value) >> 4;
    }
  }

  int32_t share_;
  int32_t max_grains_;
  int32_t cap_;
  uint32_t period_;
  uint32_t engines_;
  uint32_t fixed_;
  uint32_t per_grain_;
  uint32_t mean_interval_;
  int32_t cooldown_;
  uint32_t ceiling_;
  uint32_t overload_point_;
  int32_t calls_;
  int32_t grow_counter_;
  int32_t late_calls_;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_GOVERNOR_H_
