// Checks that the Chamberlin mode filter has the same response as the
// upstream TPT SVF band-pass for fixed coefficients (impulse and noise input).

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "src/resonator.h"

using op1rings::ModeSvf;

static double Run(float f, float resonance, int input_kind) {
  ModeSvf a, b;
  a.Init(); b.Init();
  a.set_f_q_fast(f, resonance, 0.0f);
  b.set_f_q_fast(f, resonance, 0.0f);
  float prev = 0.0f;
  double sig = 0.0, err = 0.0;
  srand(1);
  for (int n = 0; n < 48000; ++n) {
    float x = input_kind == 0 ? (n == 0 ? 0.125f : 0.0f)
                              : 0.125f * (rand() / (float)RAND_MAX - 0.5f);
    float y_tpt = a.ProcessBandPass(x);
    float y_cham = b.gain_c * b.ProcessChamberlin(x + prev);
    prev = x;
    sig += (double)y_tpt * y_tpt;
    err += (double)(y_tpt - y_cham) * (y_tpt - y_cham);
  }
  return err > 0 ? 10.0 * log10(sig / err) : 999.0;
}

int main() {
  const float freqs[] = { 0.0005f, 0.002f, 0.01f, 0.05f, 0.2f, 0.4f, 0.49f };
  const float qs[] = { 1.5f, 10.0f, 1000.0f, 50000.0f };
  int failures = 0;
  for (float f : freqs) {
    for (float q : qs) {
      double snr_imp = Run(f, q, 0);
      double snr_noise = Run(f, q, 1);
      // Both are float32 implementations, so very high Q limits the match to
      // rounding noise (~50-70 dB); a structural mismatch would be < 20 dB.
      bool ok = snr_imp > 45.0 && snr_noise > 45.0;
      failures += !ok;
      printf("f %-7g Q %-7g  impulse SNR %6.1f dB  noise SNR %6.1f dB %s\n",
             f, q, snr_imp, snr_noise, ok ? "" : "  <-- FAIL");
    }
  }
  printf("%s\n", failures ? "FAILED" : "all equivalent (> 45 dB)");
  return failures != 0;
}
