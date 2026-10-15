// The fixed-point reverb (src/reverb_fixed.*) against upstream's float
// Reverb (rings/dsp/fx/reverb.h, 16-bit buffer), at 48 kHz in 32-sample
// blocks. Upstream gets the dry signal on the left and silence on the
// right; its mono output is left + right, which is what the port computes.
//
//   make test-reverb   (exits 1 if a scenario is below 25 dB SNR, or its wet
//                       level is off by more than 0.1 dB)
//
// The waveforms can't match closely in long tails: the 16-bit buffer rounds
// inside a loop with gain up to 0.98, so one-LSB differences recirculate.
// The level and colour of the tail do match.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "rings/dsp/dsp.h"
#include "stmlib/dsp/units.h"

namespace rings { using namespace stmlib; }

#include "rings/dsp/fx/reverb.h"
#include "src/ffloat.h"
#include "src/reverb_fixed.h"

namespace {

const double kRate = 48000.0;
const int kBlock = 32;
const int kLength = 96000;   // 2 s

struct Scenario {
  const char* name;
  double damping;
  double brightness;
  bool stale;   // port: fill the buffer with noise, then Clear
};

const Scenario kScenarios[] = {
  { "short, dark",          0.2, 0.3, false },
  { "medium",               0.6, 0.6, false },
  { "long, bright",         1.0, 0.9, false },
  { "after a Clear",        0.7, 0.5, true },
};

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

// Dry signal: decaying plucks (a sine with harmonics) every 0.5 s.
double Dry(int i) {
  const int t = i % 24000;
  const double env = std::exp(-t / 4000.0);
  const double f = 196.0 * (1 + (i / 24000) % 3);
  double x = 0.0;
  for (int h = 1; h <= 4; ++h) x += std::sin(2 * M_PI * f * h * t / kRate) / h;
  return 0.4 * env * x;
}

uint16_t upstream_buffer[32768];

std::vector<double> RenderUpstream(const Scenario& s) {
  static rings::Reverb r;
  r.Init(upstream_buffer);
  std::vector<double> y;
  float l[kBlock], rr[kBlock];
  for (int b = 0; b < kLength; b += kBlock) {
    for (int i = 0; i < kBlock; ++i) {
      l[i] = Dry(b + i);
      rr[i] = 0.0f;
    }
    r.set_amount(0.1f + s.damping * 0.5f);
    r.set_diffusion(0.625f);
    r.set_time(0.35f + 0.63f * s.damping);
    r.set_input_gain(0.2f);
    r.set_lp(0.3f + s.brightness * 0.6f);
    r.Process(l, rr, kBlock);
    for (int i = 0; i < kBlock; ++i) y.push_back(l[i] + rr[i]);
  }
  return y;
}

std::vector<double> RenderPort(const Scenario& s) {
  static op1rings::ReverbFixed r;
  memset(static_cast<void*>(&r), 0, sizeof(r));
  r.Init();
  int32_t x[kBlock];
  if (s.stale) {
    uint32_t rng = 7;
    r.set_amount(Q31(0.5));
    r.set_time(Q31(0.9));
    for (int b = 0; b < 2000; ++b) {
      for (int i = 0; i < kBlock; ++i) {
        rng = rng * 1664525u + 1013904223u;
        x[i] = static_cast<int32_t>(rng) >> 4;
      }
      r.Process(x, kBlock);
    }
    r.Clear();
  }
  std::vector<double> y;
  for (int b = 0; b < kLength; b += kBlock) {
    for (int i = 0; i < kBlock; ++i) x[i] = static_cast<int32_t>(Dry(b + i) * 134217728.0);
    r.set_amount(Q31(0.1 + s.damping * 0.5));
    r.set_diffusion(Q31(0.625));
    r.set_time(Q31(0.35 + 0.63 * s.damping));
    r.set_input_gain(Q31(0.2));
    r.set_lp(Q31(0.3 + s.brightness * 0.6));
    r.Process(x, kBlock);
    for (int i = 0; i < kBlock; ++i) y.push_back(x[i] / 134217728.0);
  }
  return y;
}

double SnrDb(const std::vector<double>& ref, const std::vector<double>& x, size_t from, size_t n) {
  double e = 0.0, d = 0.0;
  for (size_t i = from; i < from + n; ++i) {
    e += ref[i] * ref[i];
    d += (ref[i] - x[i]) * (ref[i] - x[i]);
  }
  return 10 * std::log10(e / (d + 1e-30));
}

// The wet part alone (output minus the dry share), to judge the reverb
// itself rather than the dry signal it carries.
std::vector<double> Wet(const std::vector<double>& y, double amount) {
  std::vector<double> w(y.size());
  for (size_t i = 0; i < y.size(); ++i) w[i] = y[i] - (1.0 - amount) * Dry(static_cast<int>(i));
  return w;
}

}  // namespace

int main() {
  printf("%-16s %10s %10s %12s\n", "scenario", "SNR dB", "wet SNR", "wet level dB");
  bool pass = true;
  for (const Scenario& s : kScenarios) {
    std::vector<double> u = RenderUpstream(s), p = RenderPort(s);
    const double amount = 0.1 + s.damping * 0.5;
    std::vector<double> wu = Wet(u, amount), wp = Wet(p, amount);
    const double snr = SnrDb(u, p, 0, u.size());
    const double wet_snr = SnrDb(wu, wp, 0, wu.size());
    double eu = 0.0, ep = 0.0;
    for (size_t i = 0; i < wu.size(); ++i) {
      eu += wu[i] * wu[i];
      ep += wp[i] * wp[i];
    }
    const double wet_level = 10 * std::log10(ep / eu);
    const bool ok = snr > 25.0 && std::fabs(wet_level) < 0.1;
    if (!ok) pass = false;
    printf("%-16s %10.1f %10.1f %12.2f  %s\n", s.name, snr, wet_snr, wet_level,
           ok ? "ok" : "FAIL");
  }
  return pass ? 0 : 1;
}
