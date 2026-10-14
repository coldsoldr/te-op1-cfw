// The fixed-point FM voice (src/fm_voice_fixed.*) against upstream's float
// FMVoice (rings/dsp/fm_voice.cc), both at 48 kHz in 32-sample blocks, with
// the internal envelope triggered at the start and a silent input (the
// internal exciter). Compares the mono sum (upstream out + aux) over 1 s.
//
//   make test-fm   (exits 1 if a scenario is below 40 dB SNR)
//   test_fm_voice N  prints scenario N's SNR over time (drift diagnosis)
//
// The modulator ratio must be floored to 1/256 semitone as upstream's
// SemitonesToRatio does: tuned exactly, it drifts off upstream's phase by
// ~2e-4 per cycle (5 dB SNR at 1760 Hz over 1 s, 51 dB with the floor).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "rings/dsp/fm_voice.h"
#include "src/ffloat.h"
#include "src/fm_voice_fixed.h"

namespace {

const double kRate = 48000.0;
const int kLength = 48000;
const int kBlock = 32;

struct Scenario {
  const char* name;
  double frequency;     // Hz
  double ratio;         // structure
  double brightness;
  double damping;
  double feedback;      // position; 0.5 = none
};

const Scenario kScenarios[] = {
  { "ratio 1, mid",         220.0, 0.25, 0.5, 0.5, 0.5 },
  { "ratio 0.5, bright",    110.0, 0.02, 0.9, 0.6, 0.5 },
  { "ratio 2, dull",        440.0, 0.45, 0.2, 0.4, 0.5 },
  { "ratio 3.14, bright",   220.0, 0.62, 0.8, 0.7, 0.5 },
  { "ratio 8, mid",         110.0, 0.99, 0.5, 0.5, 0.5 },
  { "high note",           1760.0, 0.25, 0.6, 0.5, 0.5 },
  { "short decay",          220.0, 0.3,  0.7, 0.1, 0.5 },
  { "long decay",           220.0, 0.3,  0.7, 0.85, 0.5 },
  { "oscillator (damp 1)",  220.0, 0.3,  0.6, 1.0, 0.5 },
  { "feedback +",           220.0, 0.25, 0.5, 0.5, 0.8 },
  { "feedback -",           220.0, 0.25, 0.5, 0.5, 0.15 },
};

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

std::vector<double> RenderUpstream(const Scenario& s) {
  static rings::FMVoice v;
  v.Init();
  std::vector<double> y;
  float in[kBlock] = { 0 }, out[kBlock], aux[kBlock];
  for (int b = 0; b < kLength; b += kBlock) {
    if (b == 0) v.TriggerInternalEnvelope();
    v.set_frequency(s.frequency / kRate);
    v.set_ratio(s.ratio);
    v.set_brightness(s.brightness);
    v.set_damping(s.damping);
    v.set_feedback_amount(s.feedback);
    v.Process(in, out, aux, kBlock);
    for (int i = 0; i < kBlock; ++i) y.push_back(out[i] + aux[i]);
  }
  return y;
}

std::vector<double> RenderPort(const Scenario& s) {
  static op1rings::FmVoiceFixed v;
  v.Init();
  std::vector<double> y;
  int32_t out[kBlock];
  for (int b = 0; b < kLength; b += kBlock) {
    if (b == 0) v.TriggerInternalEnvelope();
    v.set_frequency(Q31(s.frequency / kRate));
    v.set_ratio(Q31(s.ratio));
    v.set_brightness(Q31(s.brightness));
    v.set_damping(Q31(s.damping));
    v.set_feedback_amount(Q31(s.feedback));
    v.Process(out, kBlock);
    for (int i = 0; i < kBlock; ++i) y.push_back(out[i] / 134217728.0);
  }
  return y;
}

double Rms(const std::vector<double>& x) {
  double e = 0.0;
  for (double v : x) e += v * v;
  return std::sqrt(e / x.size());
}

double SnrDb(const std::vector<double>& ref, const std::vector<double>& x) {
  double e = 0.0, d = 0.0;
  for (size_t i = 0; i < ref.size(); ++i) {
    e += ref[i] * ref[i];
    d += (ref[i] - x[i]) * (ref[i] - x[i]);
  }
  return 10 * std::log10(e / (d + 1e-30));
}

// Spectral centroid (Hz) from a 4096-point DFT magnitude over the first
// 0.25 s (plain O(N^2/8) DFT on 512 bins up to 6 kHz: enough for a check).
double Centroid(const std::vector<double>& x) {
  const int n = 4096;
  double weighted = 0.0, total = 0.0;
  for (int k = 1; k < 512; ++k) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i) {
      double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
      re += w * x[i] * std::cos(2 * M_PI * k * i / n);
      im -= w * x[i] * std::sin(2 * M_PI * k * i / n);
    }
    double m = std::sqrt(re * re + im * im);
    weighted += m * k * kRate / n;
    total += m;
  }
  return weighted / (total + 1e-30);
}

double SnrWindowDb(const std::vector<double>& ref, const std::vector<double>& x, int from, int n) {
  std::vector<double> a(ref.begin() + from, ref.begin() + from + n);
  std::vector<double> b(x.begin() + from, x.begin() + from + n);
  return SnrDb(a, b);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) {   // diagnostic: SNR over time for one scenario
    const Scenario& s = kScenarios[atoi(argv[1])];
    std::vector<double> u = RenderUpstream(s), p = RenderPort(s);
    for (int from = 0; from + 1024 <= kLength; from += 4800) {
      printf("%s  t %.2f s  SNR %.1f dB\n", s.name, from / kRate, SnrWindowDb(u, p, from, 1024));
    }
    return 0;
  }
  printf("%-22s %8s %9s %12s %12s\n", "scenario", "SNR dB", "level dB", "centroid up", "centroid port");
  bool pass = true;
  for (const Scenario& s : kScenarios) {
    std::vector<double> u = RenderUpstream(s), p = RenderPort(s);
    const double snr = SnrDb(u, p);
    const double level = 20 * std::log10(Rms(p) / Rms(u));
    const double cu = Centroid(u), cp = Centroid(p);
    const bool ok = snr > 40.0;
    if (!ok) pass = false;
    printf("%-22s %8.1f %9.2f %12.0f %12.0f  %s\n", s.name, snr, level, cu, cp,
           ok ? "ok" : "FAIL");
  }
  printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
