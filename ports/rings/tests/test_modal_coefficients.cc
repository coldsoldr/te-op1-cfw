// Compares ModalResonator's fixed-point per-mode coefficients (f/2, q/2,
// gain) with upstream's float computation (resonator.cc ComputeFilters +
// the Chamberlin conversion), over a grid of parameters. Host only.

#include <cmath>
#include <cstdio>

#include "src/fixed.h"
#include "src/modal_resonator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "rings/resources.h"

using namespace op1rings;

struct Ref { float half_f, half_q, gain; };

static int Upstream(float frequency, float structure, float brightness,
                    float damping, int resolution, Ref* out) {
  float stiffness = stmlib::Interpolate(rings::lut_stiffness, structure, 256.0f);
  float harmonic = frequency;
  float stretch_factor = 1.0f;
  float q = 500.0f * stmlib::Interpolate(rings::lut_4_decades, damping, 256.0f);
  float brightness_attenuation = 1.0f - structure;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  float b = brightness * (1.0f - 0.2f * brightness_attenuation);
  float q_loss = b * (2.0f - b) * 0.85f + 0.15f;
  float q_loss_damping_rate = structure * (2.0f - structure) * 0.1f;
  int num_modes = 0;
  for (int i = 0; i < resolution; ++i) {
    float pf = harmonic * stretch_factor;
    if (pf >= 0.49f) pf = 0.49f; else num_modes = i + 1;
    float g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(pf);
    float r = 1.0f / (1.0f + pf * q);
    float h = 1.0f / (1.0f + r * g + g * g);
    float sh = sqrtf(h);
    out[i].half_f = g * sh;
    out[i].half_q = 0.5f * r * sh;
    out[i].gain = 0.5f * sh;
    stretch_factor += stiffness;
    stiffness *= stiffness < 0.0f ? 0.93f : 0.98f;
    q_loss += q_loss_damping_rate * (1.0f - q_loss);
    harmonic += frequency;
    q *= q_loss;
  }
  return num_modes;
}

int main() {
  const float freqs[] = { 30.0f / 48000, 110.0f / 48000, 440.0f / 48000, 2000.0f / 48000 };
  const float values[] = { 0.0f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f };
  double worst[3] = { 0, 0, 0 };
  const char* names[3] = { "half_f", "half_q", "gain" };
  int bad = 0;
  for (float f : freqs) for (float st : values) for (float br : values) for (float dm : values) {
    Ref ref[64];
    int n_ref = Upstream(f, st, br * br, dm, 60, ref);
    ModalResonator m;
    m.Init();
    m.set_resolution(60);
    m.set_frequency(FloatToQ31(f));
    m.set_structure(FloatToQ31(st));
    m.set_brightness(FloatToQ31(br * br));
    m.set_damping(FloatToQ31(dm));
    int n = m.UpdateCoefficients();
    if (n != n_ref) {
      if (bad++ < 5) printf("num_modes %d vs %d at f %g st %g br %g dm %g\n", n, n_ref, f, st, br, dm);
    }
    for (int i = 0; i < 60; ++i) {
      double got[3] = { Q31ToFloat(m.mode(i).half_f), Q31ToFloat(m.mode(i).half_q),
                        Q31ToFloat(m.gain(i)) };
      double want[3] = { ref[i].half_f, ref[i].half_q, ref[i].gain };
      for (int k = 0; k < 3; ++k) {
        double rel = fabs(got[k] - want[k]) / fmax(fabs(want[k]), 1e-9);
        if (rel > worst[k]) {
          worst[k] = rel;
          if (rel > 0.01)
            printf("%s mode %d: %.6g vs %.6g (rel %.3g) f %g st %g br %g dm %g\n",
                   names[k], i, got[k], want[k], rel, f, st, br, dm);
        }
      }
    }
  }
  for (int k = 0; k < 3; ++k) printf("%-7s worst relative error %.3g\n", names[k], worst[k]);
  printf("num_modes mismatches: %d\n", bad);
  return 0;
}
