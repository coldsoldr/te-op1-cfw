// Braids as an OP-1 synth voice: one MacroOscillator behind the OP-1's
// render call (docs/braids-port.md has the design).
//
// - Rate: the OP-1 renders voices at 88.2 kHz, 128 samples per call; Braids
//   runs unchanged at that rate, in its own 24-sample blocks. A small FIFO
//   joins the two: each call renders 5 or 6 blocks (6, 5, 5, repeating) and
//   keeps the rest of the last one for the next call (at most 23 samples,
//   0.26 ms).
// - Pitch: the OP-1 gives a phase increment (f / 88200 * 2^32).
//   PitchFromIncrement inverts Braids' own increment table, which is
//   computed for 96 kHz, so the pitch it returns is the note plus ~188/128
//   semitone (128 x 12 x log2(96000 / 88200) = 187.8): at 88.2 kHz that
//   plays the note at its true frequency, and every pitch-derived table
//   (oscillator increments, delay lengths, filter cutoffs that track the
//   pitch) is retuned with it. The DSP code is untouched.
// - Amplitude: none here. The OP-1 core applies the slot's ADSR after
//   render, as it does for phase and Rings.
// - Lo-fi (optional, off by default): upstream's "BITS" and "RATE"
//   settings (braids.cc RenderBlock), applied per 24-sample block exactly
//   as the module does.

#ifndef OP1BRAIDS_OP1_BRAIDS_VOICE_H_
#define OP1BRAIDS_OP1_BRAIDS_VOICE_H_

#include <cstdint>

#include "src/macro_oscillator.h"

namespace op1braids {

const int32_t kBraidsBlockSize = 24;
const int32_t kOp1BlockSize = 128;
const int32_t kNumAccessibleShapes = MACRO_OSC_SHAPE_LAST - 1;   // 47: the module's menu (no "????")

// The pitch (MIDI note x 128, Braids' 96 kHz tuning) whose phase increment,
// by AnalogOscillator::ComputePhaseIncrement, is nearest to `increment`.
// No division; a binary search over the table (once per call).
int16_t PitchFromIncrement(uint32_t increment);

// AnalogOscillator::ComputePhaseIncrement (private upstream), for tests.
uint32_t IncrementFromPitch(int16_t pitch);

class Op1Voice {
 public:
  // Zeroes the voice (upstream's Init leaves some members unset).
  void Init();

  // Live, as on the module: Braids strikes on a shape change.
  void set_shape(int32_t shape);
  int32_t shape() const { return shape_; }

  // Upstream's resolution (0 = 2 bits .. 6 = 16 bits) and sample-rate
  // reduction (0 = /24 .. 6 = none) settings. Default 6 and 6: off.
  void set_lofi(int32_t resolution, int32_t rate);

  // Note-on: drops the samples still queued from the last note, so the new
  // note starts with its strike, and strikes.
  void NoteOn();

  // One OP-1 call: 128 samples. timbre and color 0..32767.
  void Render(uint32_t increment, int16_t timbre, int16_t color, int16_t* out);

 private:
  void RenderBlock(int16_t* out);

  MacroOscillator osc_;
  int16_t pending_[kBraidsBlockSize];
  int32_t pending_count_;   // samples at the end of pending_ not yet output
  int32_t shape_;
  uint16_t bit_mask_;
  int32_t decimation_;
};

}  // namespace op1braids

#endif  // OP1BRAIDS_OP1_BRAIDS_VOICE_H_
