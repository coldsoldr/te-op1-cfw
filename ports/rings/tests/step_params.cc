// Clicks from parameter steps (encoder turns) on a ringing note.
//
// Rings' resonator changes its filter coefficients once per block, so an
// instantaneous parameter step reaches the audio as a coefficient step.
// Upstream never sees raw steps: its pots go through a one-pole low-pass
// (cv_scaler.cc, coefficient 0.01 per 2 kHz control block). This test rings
// a note, steps one parameter, and measures the click:
//   click = loudest 0.7 ms burst above 9 kHz within 90 ms of the step, in dB
//           relative to the note (the note's partials are all below 9 kHz)
// printed next to the same measure over the 90 ms before the step (floor).
//
// Part 1: upstream float Resonator vs the port's ModalResonator, same input
//         and steps, 32-sample blocks (is the port worse than upstream?).
// Part 2: the OP-1 voice (RingsVoice::RenderMono, 64 samples per call as in
//         op1/engine.cc), stepped raw vs through a one-pole smoother.
//
// Check: a sustained note with fixed parameters must have no burst above
// -60 dB (the mono limiter once switched gain by 25% when its peak crossed
// 1.0, which popped).
//
//   make step-params   (exits 1 if the check fails)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "rings/dsp/resonator.h"
#include "src/ffloat.h"
#include "src/fixed.h"
#include "src/modal_resonator.h"
#include "src/op1_voice.h"

namespace {

const double kRate = 44100.0;
const int kModes = 16;
const int kStepAt = 13230;   // 0.3 s
const int kLength = 22050;   // 0.5 s

enum Param { STRUCTURE, BRIGHTNESS, DAMPING, POSITION };
const char* kNames[] = { "structure", "brightness", "damping", "position" };
const double kBase[4] = { 0.25, 0.6, 0.7, 0.3 };   // as tests/sim_engine.cc

// High-pass FIR (windowed sinc, Blackman), cutoff 9 kHz: the test note's
// partials stay well below it, so what passes is discontinuity.
std::vector<double> HighPass(const std::vector<double>& x) {
  const int kTaps = 63;
  const double fc = 9000.0 / kRate;
  std::vector<double> h(kTaps);
  for (int i = 0; i < kTaps; ++i) {
    const double m = i - (kTaps - 1) / 2.0;
    const double sinc = m == 0 ? 2 * fc : std::sin(2 * M_PI * fc * m) / (M_PI * m);
    const double w = 0.42 - 0.5 * std::cos(2 * M_PI * i / (kTaps - 1)) +
                     0.08 * std::cos(4 * M_PI * i / (kTaps - 1));
    h[i] = -sinc * w;
  }
  h[(kTaps - 1) / 2] += 1.0;
  std::vector<double> y(x.size(), 0.0);
  for (size_t n = kTaps; n < x.size(); ++n) {
    double acc = 0.0;
    for (int i = 0; i < kTaps; ++i) acc += h[i] * x[n - i];
    y[n] = acc;
  }
  return y;
}

// Loudest 32-sample frame of the high-passed signal in [from, to), in dB
// relative to the full-band RMS of the 2048 samples before the step.
double HfDb(const std::vector<double>& x, int from, int to) {
  std::vector<double> hf = HighPass(x);
  double sum = 0.0;
  for (int n = kStepAt - 2048; n < kStepAt; ++n) sum += x[n] * x[n];
  const double rms = std::sqrt(sum / 2048);
  double worst = 0.0;
  for (int n = from; n + 32 <= to; n += 8) {
    double e = 0.0;
    for (int i = 0; i < 32; ++i) e += hf[n + i] * hf[n + i];
    if (e > worst) worst = e;
  }
  return 20 * std::log10(std::sqrt(worst / 32) / rms + 1e-12);
}

// Click: worst high-frequency burst from the step to 90 ms after it; floor:
// the same over the 90 ms before the step.
double Click(const std::vector<double>& x) { return HfDb(x, kStepAt - 32, kStepAt + 3968); }
double Floor(const std::vector<double>& x) { return HfDb(x, kStepAt - 4000, kStepAt - 64); }

// Parameter values per block: base, then base + delta from the step on,
// either at once or through a one-pole smoother (coefficient per block).
double Value(int p, double delta, int block_start, double* smoothed, double coefficient) {
  double target = kBase[p] + (block_start >= kStepAt ? delta : 0.0);
  if (coefficient <= 0.0) return target;
  *smoothed += coefficient * (target - *smoothed);
  return *smoothed;
}

std::vector<double> RenderUpstream(int p, double delta) {
  static rings::Resonator r;
  r.Init();
  r.set_resolution(kModes);
  r.set_frequency(220.0 / kRate);
  std::vector<double> y;
  float in[32], out[32], aux[32];
  for (int b = 0; b < kLength; b += 32) {
    double v[4] = { kBase[0], kBase[1], kBase[2], kBase[3] };
    if (b >= kStepAt) v[p] += delta;
    r.set_structure(v[0]);
    r.set_brightness(v[1] * v[1]);
    r.set_damping(v[2]);
    r.set_position(v[3]);
    for (int i = 0; i < 32; ++i) in[i] = (b == 0 && i == 0) ? 1.0f : 0.0f;
    r.Process(in, out, aux, 32);
    for (int i = 0; i < 32; ++i) y.push_back(out[i] + aux[i]);
  }
  return y;
}

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

std::vector<double> RenderPort(int p, double delta) {
  static op1rings::ModalResonator r;
  r.Init();
  r.set_resolution(kModes);
  r.set_frequency(Q31(220.0 / kRate));
  std::vector<double> y;
  int32_t in[32], out[32], aux[32];
  for (int b = 0; b < kLength; b += 32) {
    double v[4] = { kBase[0], kBase[1], kBase[2], kBase[3] };
    if (b >= kStepAt) v[p] += delta;
    r.set_structure(Q31(v[0]));
    r.set_brightness(Q31(v[1] * v[1]));
    r.set_damping(Q31(v[2]));
    r.set_position(Q31(v[3]));
    for (int i = 0; i < 32; ++i) in[i] = (b == 0 && i == 0) ? op1rings::kQ27One : 0;
    r.Process(in, out, aux, 32);
    for (int i = 0; i < 32; ++i) y.push_back((out[i] + static_cast<double>(aux[i])) / 134217728.0);
  }
  return y;
}

std::vector<double> RenderVoice(int p, double delta, double coefficient) {
  static op1rings::RingsVoice voice;
  voice.Init(kModes);
  voice.NoteOn();
  const uint32_t inc = static_cast<uint32_t>(220.0 / kRate * 4294967296.0);
  double smoothed = kBase[p];
  std::vector<double> y;
  int32_t out[64];
  for (int b = 0; b < kLength; b += 64) {
    double v[4] = { kBase[0], kBase[1], kBase[2], kBase[3] };
    v[p] = Value(p, delta, b, &smoothed, coefficient);
    op1rings::VoicePatch patch;
    patch.structure = Q31(v[0]);
    patch.brightness = Q31(v[1]);
    patch.damping = Q31(v[2]);
    patch.position = Q31(v[3]);
    voice.RenderMono(&inc, patch, out, 64);
    for (int i = 0; i < 64; ++i) y.push_back(out[i] / 134217728.0);
  }
  return y;
}

}  // namespace

// Check: a sustained note with fixed parameters has no discontinuities.
// Worst 32-sample burst above 9 kHz from 100 ms on, in dB relative to the
// RMS of the surrounding 2048 samples. Returns the worst value.
double SustainWorst(int* where) {
  std::vector<double> x = RenderVoice(0, 0.0, 0.0);
  std::vector<double> hf = HighPass(x);
  double worst = -200.0;
  for (int n = 4410; n + 32 + 1024 <= static_cast<int>(x.size()); n += 8) {
    double e = 0.0, f = 0.0;
    for (int i = 0; i < 32; ++i) e += hf[n + i] * hf[n + i];
    for (int i = -1024; i < 1024; ++i) f += x[n + i] * x[n + i];
    double db = 10 * std::log10((e / 32) / (f / 2048) + 1e-30);
    if (db > worst) {
      worst = db;
      *where = n - 31;   // the FIR delays by 31 samples
    }
  }
  return worst;
}

int main() {
  int where = 0;
  const double sustain = SustainWorst(&where);
  const bool pass = sustain < -60.0;
  printf("Sustained note, fixed parameters: worst HF burst %.1f dB at sample %d (call %d +%d): %s\n\n",
         sustain, where, where / 64, where % 64, pass ? "PASS (< -60 dB)" : "FAIL (>= -60 dB)");
  const double kDeltas[] = { 0.01, 0.05, 0.2 };
  printf("Part 1: resonator only, instant step: HF burst, dB re note (floor / after step)\n");
  printf("%-11s %6s %18s %18s\n", "param", "delta", "upstream", "port");
  for (int p = 0; p < 4; ++p) {
    for (double delta : kDeltas) {
      std::vector<double> u = RenderUpstream(p, delta), q = RenderPort(p, delta);
      printf("%-11s %6.2f %8.1f %8.1f  %8.1f %8.1f\n", kNames[p], delta,
             Floor(u), Click(u), Floor(q), Click(q));
    }
  }
  printf("\nPart 2: OP-1 voice, 64-sample calls: HF burst after the step, dB re note\n");
  printf("%-11s %6s %8s %8s %12s %12s\n", "param", "delta", "floor", "raw", "smooth 0.1", "smooth 0.03");
  for (int p = 0; p < 4; ++p) {
    for (double delta : kDeltas) {
      std::vector<double> raw = RenderVoice(p, delta, 0.0);
      printf("%-11s %6.2f %8.1f %8.1f %12.1f %12.1f\n", kNames[p], delta, Floor(raw),
             Click(raw), Click(RenderVoice(p, delta, 0.1)), Click(RenderVoice(p, delta, 0.03)));
    }
  }
  return pass ? 0 : 1;
}
