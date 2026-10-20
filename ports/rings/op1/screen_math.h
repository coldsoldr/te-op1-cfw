// Rings screen geometry from the engine's knob values, per the redesign
// (docs/module-briefs/designs/rings.md). Integer maths only (the screen runs
// in the OP-1's Gfx thread; no soft-float), tested on the host
// (tests/test_screen_math.cc).
//
// Knob values are the int16 params the engine sees, 0..32767, read as
// value / 32768 like the engine does (op1/engine.cc). Scales and
// translations are in the SVG transform's fixed point, 10 fractional bits
// (docs/screen-interface.md §2).

#ifndef OP1RINGS_OP1_SCREEN_MATH_H_
#define OP1RINGS_OP1_SCREEN_MATH_H_

#include <cstdint>

namespace op1 {

// rings.svg has pluck0..pluck23: the pluck polyline with its middle vertex
// at x = 42.5 + 5 i, one of which is shown.
const int32_t kPluckVariants = 24;

// The bars (rects y 36..116) scale in y about their foot, y 116; at least
// 0.02 so they stay visible.
const int32_t kBarFoot = 116;
const int32_t kBarMinScale = 20;   // Q10

struct ScreenState {
  int32_t lit_rings;        // 1..6: ring0..ring(lit - 1) lit
  int32_t brightness_sy;    // #brightness y scale about y 116, Q10
  int32_t damping_sy;       // #damping y scale about y 116, Q10
  int32_t posdot_tx;        // #posdot translate x (drawn at x = 42.5), Q10
  int32_t pluck;            // visible pluck variant, 0..23
};

// The phase engine's stock range for green (brightness) stops at 29491
// (docs/screen-interface.md §4c): its bar is drawn over that range, so it
// fills at the encoder's top. Display only; the engine reads the knob as it is.
const int32_t kBrightnessMax = 29491;

inline int32_t KnobValue(int16_t k) { return k < 0 ? 0 : k; }

// Brightness rescaled to 0..32768 over the encoder's range.
inline int32_t BrightnessDisplayValue(int16_t k) {
  const int32_t v = KnobValue(k) > kBrightnessMax ? kBrightnessMax : KnobValue(k);
  return (v * 32768 + kBrightnessMax / 2) / kBrightnessMax;
}

// value / 32768 as a bar's scale, Q10 (rounded), at least kBarMinScale.
inline int32_t BarScale(int32_t v) {
  const int32_t s = (v + 16) >> 5;
  return s < kBarMinScale ? kBarMinScale : s;
}

// knobs: structure, brightness, damping, position.
inline ScreenState ComputeScreenState(const int16_t* knobs) {
  const int32_t structure = KnobValue(knobs[0]);
  const int32_t brightness = BrightnessDisplayValue(knobs[1]);
  const int32_t damping = KnobValue(knobs[2]);
  const int32_t position = KnobValue(knobs[3]);
  ScreenState s;
  // k = 1 + round(structure * 5).
  s.lit_rings = 1 + ((structure * 5 + 16384) >> 15);
  s.brightness_sy = BarScale(brightness);
  s.damping_sy = BarScale(damping);
  // x = 42.5 + position * 115; translate = position * 115, in Q10:
  // k * 115 / 32.
  s.posdot_tx = (position * 115) >> 5;
  // Nearest variant: round(position * 115 / 5).
  s.pluck = (position * 23 + 16384) >> 15;
  if (s.pluck >= kPluckVariants) s.pluck = kPluckVariants - 1;
  return s;
}

}  // namespace op1

#endif  // OP1RINGS_OP1_SCREEN_MATH_H_
