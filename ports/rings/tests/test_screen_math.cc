// op1/screen_math.h against the screen spec's float formulas
// (incoming/rings-handover.md) over every knob value.
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
    // Spec: k = 1 + round(structure * 5); handleY = 114 - v * 100 (drawn at
    // 44 / 64); x = 16 + v * 92 (drawn at 44).
    const int lit = 1 + static_cast<int>(std::floor(v * 5 + 0.5));
    // Green's handle spans the encoder's range, 0..29491 (op1/screen_math.h).
    const double vb = std::fmin(k, op1::kBrightnessMax) / static_cast<double>(op1::kBrightnessMax);
    const double brightness_ty = 114 - vb * 100 - 44;
    const double damping_ty = 114 - v * 100 - 64;
    const double posdot_tx = 16 + v * 92 - 44;
    const int pluck = static_cast<int>(std::floor(v * 23 + 0.5));
    double px = std::fabs(s.brightness_ty / 1024.0 - brightness_ty);
    px = std::fmax(px, std::fabs(s.damping_ty / 1024.0 - damping_ty));
    px = std::fmax(px, std::fabs(s.posdot_tx / 1024.0 - posdot_tx));
    if (px > worst_px) worst_px = px;
    if (s.lit_rings != lit || s.pluck != pluck || px > 0.01) {
      if (errors++ < 5) {
        printf("k %d: lit %d (want %d), pluck %d (want %d), position error %.4f px\n",
               k, s.lit_rings, lit, s.pluck, pluck, px);
      }
    }
  }
  const int16_t zero[4] = { 0, 0, 0, 0 };
  const int16_t full[4] = { 32767, 32767, 32767, 32767 };
  const op1::ScreenState a = op1::ComputeScreenState(zero);
  const op1::ScreenState b = op1::ComputeScreenState(full);
  printf("knobs 0:     rings %d, brightness y %+.2f, damping y %+.2f, posdot x %+.2f, pluck %d\n",
         a.lit_rings, a.brightness_ty / 1024.0, a.damping_ty / 1024.0, a.posdot_tx / 1024.0, a.pluck);
  printf("knobs 32767: rings %d, brightness y %+.2f, damping y %+.2f, posdot x %+.2f, pluck %d\n",
         b.lit_rings, b.brightness_ty / 1024.0, b.damping_ty / 1024.0, b.posdot_tx / 1024.0, b.pluck);
  // Green at the encoder's maximum: the handle at the top (y = 14).
  const int16_t green_max[4] = { 0, static_cast<int16_t>(op1::kBrightnessMax), 0, 0 };
  const op1::ScreenState g = op1::ComputeScreenState(green_max);
  const bool green_top = std::fabs(g.brightness_ty / 1024.0 + 30.0) < 0.01;
  if (!green_top) ++errors;
  printf("green %d: brightness y %+.2f (top -30.00) %s\n", static_cast<int>(op1::kBrightnessMax),
         g.brightness_ty / 1024.0, green_top ? "ok" : "WRONG");
  printf("worst position error %.4f px; %s\n", worst_px, errors ? "FAIL" : "PASS");
  return errors ? 1 : 0;
}
