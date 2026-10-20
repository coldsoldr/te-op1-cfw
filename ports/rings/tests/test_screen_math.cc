// op1/screen_math.h against the redesign's float formulas
// (docs/module-briefs/designs/rings.md) over every knob value.
//
//   make test-screen   (exits 1 on a mismatch)

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "op1/screen_math.h"

int main() {
  int errors = 0;
  double worst_px = 0.0;
  for (int32_t k = 0; k <= 32767; ++k) {
    const int16_t knobs[4] = { static_cast<int16_t>(k), static_cast<int16_t>(k),
                               static_cast<int16_t>(k), static_cast<int16_t>(k) };
    const op1::ScreenState s = op1::ComputeScreenState(knobs);
    const double v = k / 32768.0;
    // Spec: k = 1 + round(structure * 5); bars 80 px tall scaled by the value
    // (at least 0.02); posdot x = 42.5 + v * 115; pluck round(v * 23).
    const int lit = 1 + static_cast<int>(std::floor(v * 5 + 0.5));
    // Green's bar spans the encoder's range, 0..29491 (op1/screen_math.h).
    const double vb = std::fmin(k, op1::kBrightnessMax) / static_cast<double>(op1::kBrightnessMax);
    const double brightness_h = 80 * std::fmax(vb, 0.02);
    const double damping_h = 80 * std::fmax(v, 0.02);
    const double posdot_tx = v * 115;
    const int pluck = static_cast<int>(std::floor(v * 23 + 0.5));
    double px = std::fabs(80 * s.brightness_sy / 1024.0 - brightness_h);
    px = std::fmax(px, std::fabs(80 * s.damping_sy / 1024.0 - damping_h));
    px = std::fmax(px, std::fabs(s.posdot_tx / 1024.0 - posdot_tx));
    if (px > worst_px) worst_px = px;
    if (s.lit_rings != lit || s.pluck != pluck || px > 0.1) {
      if (errors++ < 5) {
        printf("k %d: lit %d (want %d), pluck %d (want %d), geometry error %.4f px\n",
               k, s.lit_rings, lit, s.pluck, pluck, px);
      }
    }
  }
  const int16_t zero[4] = { 0, 0, 0, 0 };
  const int16_t full[4] = { 32767, 32767, 32767, 32767 };
  const op1::ScreenState a = op1::ComputeScreenState(zero);
  const op1::ScreenState b = op1::ComputeScreenState(full);
  printf("knobs 0:     rings %d, brightness %.3f, damping %.3f, posdot x %+.2f, pluck %d\n",
         a.lit_rings, a.brightness_sy / 1024.0, a.damping_sy / 1024.0, a.posdot_tx / 1024.0, a.pluck);
  printf("knobs 32767: rings %d, brightness %.3f, damping %.3f, posdot x %+.2f, pluck %d\n",
         b.lit_rings, b.brightness_sy / 1024.0, b.damping_sy / 1024.0, b.posdot_tx / 1024.0, b.pluck);
  // Green at the encoder's maximum: the bar full.
  const int16_t green_max[4] = { 0, static_cast<int16_t>(op1::kBrightnessMax), 0, 0 };
  const op1::ScreenState g = op1::ComputeScreenState(green_max);
  const bool green_full = g.brightness_sy == 0x400;
  if (!green_full) ++errors;
  printf("green %d: brightness %.3f (full 1.000) %s\n", static_cast<int>(op1::kBrightnessMax),
         g.brightness_sy / 1024.0, green_full ? "ok" : "WRONG");
  printf("worst geometry error %.4f px; %s\n", worst_px, errors ? "FAIL" : "PASS");
  return errors ? 1 : 0;
}
