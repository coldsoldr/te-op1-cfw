// CPU meter for device tests: the share of time the engine's render calls
// take, measured with the cycle counter over windows of four audio blocks
// (5.8 ms). The peak is held and decays by 1 point per window (~0.6 s from
// 100%). A click on the white encoder (op1/input.cc) shows it on the Rings
// screen's six rings (op1/screen.cc): ring i lights at (i + 1) x 15%, so
// five rings (75%) is near the ~81% the OP-1 leaves free.
//
// The engine's share only: the OP-1's own audio work (envelopes, FX, mixing)
// comes on top.

#ifndef OP1RINGS_OP1_CPU_METER_H_
#define OP1RINGS_OP1_CPU_METER_H_

#include <cstdint>

#include "op1/op1.h"

namespace op1 {

class CpuMeter {
 public:
  static const uint32_t kWindow = 4 * 580500;   // four blocks at 400 MHz

  void Begin() { start_ = Cycles(); }

  void End() {
    const uint32_t now = Cycles();
    busy_ += now - start_;
    const uint32_t elapsed = now - window_start_;
    if (elapsed >= kWindow) {
      // Percent, without overflow (elapsed < 2^32 / 2^8 x 2^8).
      int32_t load = static_cast<int32_t>((busy_ >> 8) * 100 / ((elapsed >> 8) + 1));
      if (elapsed > 8 * kWindow) load = 0;   // first call after a pause
      display_ = load > display_ - 1 ? load : display_ - 1;
      if (display_ < 0) display_ = 0;
      busy_ = 0;
      window_start_ = now;
    }
  }

  int32_t percent() const { return display_; }

 private:
  uint32_t start_;
  uint32_t window_start_;
  uint32_t busy_;
  int32_t display_;
};

extern CpuMeter cpu_meter;
extern bool cpu_meter_shown;

}  // namespace op1

#endif  // OP1RINGS_OP1_CPU_METER_H_
