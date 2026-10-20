// The half-rate path of a wet-only core (plate, hall, ensemble, tape echo),
// as Clouds runs (../../clouds/op1/clouds_fx.cc): the 64-sample call is
// decimated 2:1 by Clouds' halfband filters, the core makes 32 wet samples
// at 22.05 kHz, they are interpolated back and mixed with the full-rate dry
// input (equal power, soft limit, src/core/halfband.h).
//
// Mix: Clouds' crossfade tables are 0.707 at their ends, so x sqrt 2: mix 0
// is the dry signal, mix 1 the wet. MixSoftLimit counts the half-scale wet
// twice, so its side x sqrt 2 / 2. Q15.
//
// Restart: a position's first call after the host's stop, or after a pause
// over 5 ms, re-initialises the filters and passes the input through; the
// engine re-initialises its core when Begin returns false.

#ifndef OP1FX_OP1_HALF_RATE_H_
#define OP1FX_OP1_HALF_RATE_H_

#include <cstdint>

#include "fx/op1/fx_host.h"
#include "op1/op1.h"
#include "src/core/clouds_tables.h"
#include "src/core/dsp_fixed.h"
#include "src/core/halfband.h"

namespace op1fx {

const int32_t kHalfN = kFxBlockSize / 2;

class HalfRate {
 public:
  // Once per call, first: false on a restart (the input stays as it is; the
  // engine re-initialises its core and returns, adding its cycles).
  bool Begin(uint32_t now) {
    if (running_ && now - last_call_ > kGapCycles) running_ = false;
    last_call_ = now;
    if (running_) return true;
    for (int32_t c = 0; c < 2; ++c) {
      decimator_[c].Init();
      interpolator_[c].Init();
    }
    if (!soft_limit_ready_) {
      op1clouds::InitSoftLimitTable(&soft_limit_);
      soft_limit_ready_ = true;
    }
    mix_ = 0;
    running_ = true;
    return false;
  }

  void Stop() { running_ = false; }

  // The call's input at half rate: in(0), in(1) (r null: mono).
  void Decimate(const int16_t* l, const int16_t* r) {
    decimator_[0].Process(l, half_[0], kHalfN);
    if (r) decimator_[1].Process(r, half_[1], kHalfN);
  }
  const int16_t* in(int32_t c) const { return half_[c]; }
  // The core writes its wet output here (at the input's scale).
  int16_t* wet(int32_t c) { return wet_half_[c]; }

  // The wet back to full rate and mixed into l (and r), mix 0..32767,
  // smoothed by a quarter per call (~6 ms).
  void Mix(int16_t* l, int16_t* r, int32_t mix) {
    const int32_t channels = r ? 2 : 1;
    for (int32_t c = 0; c < channels; ++c) {
      int16_t* w = wet_half_[c];
      for (int32_t i = 0; i < kHalfN; ++i) w[i] = static_cast<int16_t>(w[i] >> 1);   // half scale
      interpolator_[c].Process(w, wet_full_[c], kHalfN);
    }
    const int32_t target = (mix << 1) + (mix >> 14);   // Q16
    mix_ += (target - mix_) >> 2;
    const int16_t fade_out = FadeOut(mix_), fade_in = FadeIn(mix_);
    op1clouds::MixSoftLimit(l, wet_full_[0], kFxBlockSize, fade_out, fade_in, &soft_limit_);
    if (r) op1clouds::MixSoftLimit(r, wet_full_[1], kFxBlockSize, fade_out, fade_in, &soft_limit_);
  }

 private:
  static const uint32_t kGapCycles = 2000000;   // 5 ms at 400 MHz

  static int16_t FadeOut(int32_t mix_q16) {
    const int32_t f = op1clouds::InterpolateQ16(op1clouds::kXfadeOutQ31, mix_q16, 4) >> 16;
    const int32_t g = (f * 46341) >> 15;
    return static_cast<int16_t>(g > 32767 ? 32767 : g);
  }

  static int16_t FadeIn(int32_t mix_q16) {
    const int32_t f = op1clouds::InterpolateQ16(op1clouds::kXfadeInQ31, mix_q16, 4) >> 16;
    return static_cast<int16_t>((f * 23170) >> 15);
  }

  op1clouds::HalfbandDecimator decimator_[2];
  op1clouds::HalfbandInterpolator interpolator_[2];
  int16_t half_[2][kHalfN];
  int16_t wet_half_[2][kHalfN];
  int16_t wet_full_[2][kFxBlockSize];
  bool running_;
  uint32_t last_call_;
  int32_t mix_;   // Q16

  // Shared by every instance (C++17 inline: zero-initialised, no constructor).
  static inline op1clouds::SoftLimitTable soft_limit_;
  static inline bool soft_limit_ready_;
};

}  // namespace op1fx

#endif  // OP1FX_OP1_HALF_RATE_H_
