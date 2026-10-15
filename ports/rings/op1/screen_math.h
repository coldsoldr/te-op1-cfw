// Rings screen geometry from the engine's knob values, per the screen spec
// (incoming/rings-handover.md). Integer maths only (the screen runs in the
// OP-1's Gfx thread; no soft-float), tested on the host
// (tests/test_screen_math.cc).
//
// Knob values are the int16 params the engine sees, 0..32767, read as
// value / 32768 like the engine does (op1/engine.cc). Translations are in
// the SVG transform's fixed point, 10 fractional bits
// (docs/screen-interface.md §2).

#ifndef OP1RINGS_OP1_SCREEN_MATH_H_
#define OP1RINGS_OP1_SCREEN_MATH_H_

#include <cstdint>

namespace op1 {

// rings.svg has pluck0..pluck23: the pluck polyline with its middle vertex
// at x = 16 + 4 i, one of which is shown.
const int32_t kPluckVariants = 24;

struct ScreenState {
  int32_t lit_rings;        // 1..6: ring0..ring(lit - 1) lit
  int32_t brightness_ty;    // #brightness translate y (drawn at y = 44), Q10
  int32_t damping_ty;       // #damping translate y (drawn at y = 64), Q10
  int32_t posdot_tx;        // #posdot translate x (drawn at x = 44), Q10
  int32_t pluck;            // visible pluck variant, 0..23
};

// The phase engine's stock range for green (brightness) stops at 29491
// (docs/screen-interface.md §4c): its handle is drawn over that range, so it
// reaches the top. Display only; the engine reads the knob as it is.
const int32_t kBrightnessMax = 29491;

inline int32_t KnobValue(int16_t k) { return k < 0 ? 0 : k; }

// Brightness rescaled to 0..32768 over the encoder's range.
inline int32_t BrightnessDisplayValue(int16_t k) {
  const int32_t v = KnobValue(k) > kBrightnessMax ? kBrightnessMax : KnobValue(k);
  return (v * 32768 + kBrightnessMax / 2) / kBrightnessMax;
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
  // handleY = 114 - value * 100; translate = handleY - drawn y, in Q10:
  // value * 100 * 1024 = k * 25 / 8.
  s.brightness_ty = (70 << 10) - ((brightness * 25) >> 3);
  s.damping_ty = (50 << 10) - ((damping * 25) >> 3);
  // x = 16 + position * 92; translate = x - 44, in Q10: k * 23 / 8.
  s.posdot_tx = ((position * 23) >> 3) - (28 << 10);
  // Nearest variant: round(position * 92 / 4).
  s.pluck = (position * 23 + 16384) >> 15;
  if (s.pluck >= kPluckVariants) s.pluck = kPluckVariants - 1;
  return s;
}

}  // namespace op1

#endif  // OP1RINGS_OP1_SCREEN_MATH_H_
