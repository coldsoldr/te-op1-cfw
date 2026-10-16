// CPU meter for device tests: the share of time the engine's render calls
// take, measured with the cycle counter over windows of four audio blocks
// (5.8 ms). The peak is held and decays by 1 point per window (~0.6 s from
// 100%). A click on the white encoder (op1/input.cc) shows it on the Rings
// screen's six rings (op1/screen.cc): ring i lights at (i + 1) x 15%, so
// five rings (75%) is near the ~81% the OP-1 leaves free.
//
// The engine's share only: the OP-1's own audio work (envelopes, FX, mixing)
// comes on top.
//
// A second reading, for device tests: the share spent recomputing the modal
// model's coefficients (op1rings::profile_update_cycles, counted by
// ModalResonator while op1rings::profile_clock is set). The white click
// cycles hidden -> engine -> coefficient updates.

#ifndef OP1RINGS_OP1_CPU_METER_H_
#define OP1RINGS_OP1_CPU_METER_H_

#include <cstdint>

#include "op1/op1.h"
#include "src/modal_resonator.h"

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
      const uint32_t updates = op1rings::profile_update_cycles - updates_start_;
      int32_t update_load = static_cast<int32_t>((updates >> 8) * 100 / ((elapsed >> 8) + 1));
      if (elapsed > 8 * kWindow) load = update_load = 0;   // first call after a pause
      display_ = load > display_ - 1 ? load : display_ - 1;
      if (display_ < 0) display_ = 0;
      update_display_ = update_load > update_display_ - 1 ? update_load : update_display_ - 1;
      if (update_display_ < 0) update_display_ = 0;
      busy_ = 0;
      window_start_ = now;
      updates_start_ = op1rings::profile_update_cycles;
    }
  }

  int32_t percent() const { return display_; }
  int32_t update_percent() const { return update_display_; }

 private:
  uint32_t start_;
  uint32_t window_start_;
  uint32_t busy_;
  int32_t display_;
  uint32_t updates_start_;
  int32_t update_display_;
};

// What the rings show: structure, or a meter reading.
enum MeterMode { METER_OFF = 0, METER_ENGINE = 1, METER_UPDATES = 2 };
const int32_t kNumMeterModes = 3;

extern CpuMeter cpu_meter;
extern int32_t cpu_meter_mode;

}  // namespace op1

#endif  // OP1RINGS_OP1_CPU_METER_H_
