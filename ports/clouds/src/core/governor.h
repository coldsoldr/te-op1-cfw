// CPU governor for Clouds on the OP-1: keeps the granular mode's grain
// count within the CPU the audio thread can spare, so Clouds degrades (fewer
// grains) instead of the audio crackling.
//
// Called once per effect call (64 samples) with cycle counts:
//   interval: since the previous call's start (the audio period; a late
//             call means the audio thread overran);
//   own:      Clouds' cycles in the previous call;
//   engines:  the synth engines' render cycles since the previous call
//             (our image measures its engines; stock engines, drums and
//             tape it can't see).
//
// Feed-forward: Clouds' target is a share of the period (default
// kDefaultShare; Test 16: ~77% of a period free with stock phone running and
// nothing playing, phone itself ~9%, and the UI thread runs in that free
// time too: 0.75 keeps ~10% in reserve) minus the engines' smoothed cycles.
// Above the target the grain cap drops in proportion at once; well under
// it, it grows by one grain every kGrowCalls calls.
//
// Feedback, for the load it can't see: overload shows as late calls. A
// sustained overrun of a few percent makes every call only slightly late,
// so (as probe v3 found) the test is on the mean interval (a running mean
// over ~16 calls above period + 1/64), plus any single call later than 5/4
// of the period. An overload sets a ceiling on Clouds' own cycles at 7/8 of
// what it used, cuts the cap to fit it, and starts 16 calls of cooldown.
// The ceiling relaxes by 1/1024 every kGrowCalls calls (~1% a second). If
// it passes the point where the overload happened without another one, the
// hidden load has shrunk and the ceiling goes; with a hidden load that
// stays, Clouds re-tests the limit every ~13 s.
//
// Plain integer code; tests/test_governor.cc runs it against a simulated
// OP-1.

#ifndef OP1CLOUDS_CORE_GOVERNOR_H_
#define OP1CLOUDS_CORE_GOVERNOR_H_

#include <cstdint>

namespace op1clouds {

class Governor {
 public:
  static const int32_t kDefaultShare = 49152;   // Q16 of the period: 0.75
  static const int32_t kGrowCalls = 64;         // ~0.1 s
  static const uint32_t kNoCeiling = 0xffffffffu;

  void Init(int32_t max_grains, uint32_t nominal_period, int32_t share = kDefaultShare) {
    share_ = share;
    max_grains_ = max_grains;
    cap_ = max_grains;
    period_ = nominal_period;
    engines_ = 0;
    mean_interval_ = nominal_period;
    cooldown_ = 0;
    ceiling_ = kNoCeiling;
    overload_point_ = 0;
    calls_ = 0;
    grow_counter_ = 0;
    late_calls_ = 0;
    previous_own_ = 0;
  }

  // Returns the grain cap for the next call.
  int32_t Update(uint32_t interval, uint32_t own, uint32_t engines) {
    // The engines' cost, smoothed (rises at once, falls over ~16 calls).
    if (engines > engines_) {
      engines_ = engines;
    } else {
      engines_ -= (engines_ - engines) >> 4;
    }
    // Overload: a late call, or the mean interval over the period.
    mean_interval_ += static_cast<int32_t>(interval - mean_interval_) >> 4;
    if (cooldown_) --cooldown_;
    const bool late = interval > period_ / 4 * 5 || mean_interval_ > period_ + (period_ >> 6);
    if (late && !cooldown_) {
      ++late_calls_;
      cooldown_ = 16;
      mean_interval_ = period_;
      // `own` is the call just made; the overrun built up over the ones
      // before, so take the larger.
      const uint32_t used = own > previous_own_ ? own : previous_own_;
      overload_point_ = used;
      ceiling_ = used - (used >> 3);
      Cut(own, ceiling_);
      grow_counter_ = 0;
      previous_own_ = own;
      return cap_;
    }
    previous_own_ = own;

    // The ceiling relaxes; once above the feed-forward target it's gone.
    const uint32_t share = static_cast<uint32_t>((static_cast<uint64_t>(period_) * share_) >> 16);
    uint32_t target = share > engines_ ? share - engines_ : 0;
    if (ceiling_ != kNoCeiling) {
      if (++calls_ >= kGrowCalls) {
        calls_ = 0;
        ceiling_ += (ceiling_ >> 10) + 1;
      }
      if (ceiling_ >= target || ceiling_ > overload_point_ + (overload_point_ >> 5)) {
        ceiling_ = kNoCeiling;
      } else {
        target = ceiling_;
      }
    }

    if (own > target) {
      Cut(own, target);
      grow_counter_ = 0;
    } else if (own < target - (target >> 2) && cap_ < max_grains_) {
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
  uint32_t ceiling() const { return ceiling_; }

 private:
  // The cap in proportion to target / own (own cycles roughly scale with
  // grains), down by one at least. Cycle counts are < 2^20 (a period is
  // ~580k): the product fits 32 bits.
  void Cut(uint32_t own, uint32_t target) {
    if (!own) return;
    int32_t cap = static_cast<int32_t>(static_cast<uint32_t>(cap_) * target / own);
    if (cap >= cap_) cap = cap_ - 1;
    cap_ = cap < 1 ? 1 : cap;
  }

  int32_t share_;
  int32_t max_grains_;
  int32_t cap_;
  uint32_t period_;
  uint32_t engines_;
  uint32_t mean_interval_;
  int32_t cooldown_;
  uint32_t ceiling_;
  uint32_t overload_point_;
  int32_t calls_;
  int32_t grow_counter_;
  int32_t late_calls_;
  uint32_t previous_own_;
};

}  // namespace op1clouds

#endif  // OP1CLOUDS_CORE_GOVERNOR_H_
