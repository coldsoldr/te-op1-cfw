// Per-frame motion on the effect screens (the sweep dials, the tape reels
// and marks): a phase advanced by the time since the previous frame, and a
// sine. Screen code only (not on the audio path).

#ifndef OP1FX_OP1_SCREEN_ANIM_H_
#define OP1FX_OP1_SCREEN_ANIM_H_

#include <cstdint>

#include "op1/op1.h"
#include "src/core/clouds_tables.h"

namespace op1fx {

// sin(2 pi phase / 2^32) in Q15, from Clouds' sine table (kSinQ31: sin(2 pi
// i / 1024), i = 0..513).
inline int32_t SinQ15(uint32_t phase) {
  const uint32_t p = phase & 0x7fffffffu;   // 0..pi
  const int32_t i = static_cast<int32_t>(p >> 22);
  const int32_t frac = static_cast<int32_t>((p >> 6) & 0xffff);
  const int32_t a = op1clouds::kSinQ31[i] >> 16, b = op1clouds::kSinQ31[i + 1] >> 16;
  const int32_t v = a + (((b - a) * frac) >> 16);
  return phase & 0x80000000u ? -v : v;
}

inline int32_t CosQ15(uint32_t phase) { return SinQ15(phase + 0x40000000u); }

// The time between frames, in CPU cycles (at most a fifth of a second, so a
// screen shown again doesn't jump).
class FrameClock {
 public:
  uint32_t Tick() {
    const uint32_t now = op1::Cycles();
    uint32_t d = now - last_;
    last_ = now;
    return d > kMaxCycles ? kMaxCycles : d;
  }

 private:
  static const uint32_t kMaxCycles = 80000000;
  uint32_t last_;
};

// A phase turning at hz_q16 Hz (Q16) for `cycles` CPU cycles at 400 MHz:
// 2^32 x hz x cycles / 4e8 = hz_q16 x cycles x (2^16 / 4e8 x 2^32 = 703687) >> 32,
// in two steps so it fits 64 bits (hz_q16 < 2^20, cycles < 2^27).
inline uint32_t PhaseStep(int32_t hz_q16, uint32_t cycles) {
  const uint64_t x = (static_cast<uint64_t>(hz_q16) * cycles) >> 8;
  return static_cast<uint32_t>((x * 703687u) >> 24);
}

}  // namespace op1fx

#endif  // OP1FX_OP1_SCREEN_ANIM_H_
