// The fixed-point oscillators against upstream's float ones, sample by
// sample (48 kHz, 12-sample blocks), over frequency and shape sweeps. Prints
// the SNR per case.

#include <cmath>
#include <cstdio>

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/oscillator/string_synth_oscillator.h"
#include "fixed/chord_oscillators.h"
#include "fixed/oscillator.h"

namespace {

struct Stats {
  double sig = 0.0, err = 0.0, max_err = 0.0;
  void Add(double ref, double test) {
    sig += ref * ref;
    err += (test - ref) * (test - ref);
    if (std::fabs(test - ref) > max_err) max_err = std::fabs(test - ref);
  }
  void Print(const char* name) const {
    printf("%-34s SNR %6.1f dB  max err %.5f\n", name,
           10.0 * std::log10(sig / (err + 1e-30)), max_err);
  }
};

template<plaits::OscillatorShape ref_shape, op1plaits::OscillatorShape fixed_shape>
void TestOscillator(const char* name, bool sweep_pw) {
  plaits::Oscillator ref;
  op1plaits::OscillatorFixed fixed;
  ref.Init();
  fixed.Init();
  Stats stats;
  const int kBlock = 12;
  const int kBlocks = 48000 * 2 / kBlock;
  for (int b = 0; b < kBlocks; ++b) {
    // Frequency: an exponential sweep 20 Hz .. 12 kHz, quantised to Q32 so
    // both get the same value.
    const double x = static_cast<double>(b) / kBlocks;
    const uint32_t f_q32 = static_cast<uint32_t>(20.0 * std::pow(600.0, x) / 48000.0 * 4294967296.0);
    const float f = static_cast<float>(f_q32 / 4294967296.0);
    const uint32_t pw_q32 = sweep_pw
        ? static_cast<uint32_t>((0.5 + 0.45 * std::fabs(std::sin(b * 0.003))) * 4294967296.0)
        : 0x80000000u;
    const float pw = static_cast<float>(pw_q32 / 4294967296.0);
    float out_ref[kBlock];
    int32_t out_fixed[kBlock];
    ref.Render<ref_shape>(f, pw, out_ref, kBlock);
    fixed.Render<fixed_shape>(f_q32, pw_q32, out_fixed, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      stats.Add(out_ref[i], out_fixed[i] / 1073741824.0);
    }
  }
  stats.Print(name);
}

void TestStringSynth(const char* name, const float* registration) {
  plaits::StringSynthOscillator ref;
  op1plaits::StringSynthOscillatorFixed fixed;
  ref.Init();
  fixed.Init();
  int32_t reg_q31[7];
  for (int i = 0; i < 7; ++i) {
    reg_q31[i] = static_cast<int32_t>(registration[i] * 2147483647.0);
  }
  Stats stats;
  const int kBlock = 12;
  const int kBlocks = 48000 * 2 / kBlock;
  for (int b = 0; b < kBlocks; ++b) {
    const double x = static_cast<double>(b) / kBlocks;
    const uint32_t f_q32 = static_cast<uint32_t>(30.0 * std::pow(100.0, x) / 48000.0 * 4294967296.0);
    const float f = static_cast<float>(f_q32 / 4294967296.0);
    float out_ref[kBlock] = { 0 };
    int32_t out_fixed[kBlock] = { 0 };
    ref.Render(f, registration, 0.25f, out_ref, kBlock);
    fixed.Render(f_q32, reg_q31, static_cast<int32_t>(0.25 * 2147483648.0), out_fixed, kBlock);
    for (int i = 0; i < kBlock; ++i) {
      stats.Add(out_ref[i], out_fixed[i] / 268435456.0);
    }
  }
  stats.Print(name);
}

}  // namespace

int main() {
  {
    const float saw[7] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    const float square[7] = { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    const float mix[7] = { 0.0f, 0.1f, 0.1f, 0.0f, 0.2f, 0.6f, 0.0f };
    TestStringSynth("string synth, saw", saw);
    TestStringSynth("string synth, square", square);
    TestStringSynth("string synth, mix", mix);
  }
  TestOscillator<plaits::OSCILLATOR_SHAPE_SAW, op1plaits::OSCILLATOR_SHAPE_SAW>("saw", false);
  TestOscillator<plaits::OSCILLATOR_SHAPE_SQUARE, op1plaits::OSCILLATOR_SHAPE_SQUARE>("square", false);
  TestOscillator<plaits::OSCILLATOR_SHAPE_SQUARE, op1plaits::OSCILLATOR_SHAPE_SQUARE>("square, pw sweep", true);
  TestOscillator<plaits::OSCILLATOR_SHAPE_SLOPE, op1plaits::OSCILLATOR_SHAPE_TRIANGLE>("slope pw 0.5 / triangle", false);
  TestOscillator<plaits::OSCILLATOR_SHAPE_SLOPE, op1plaits::OSCILLATOR_SHAPE_SLOPE>("slope, pw 0.5", false);
  TestOscillator<plaits::OSCILLATOR_SHAPE_SLOPE, op1plaits::OSCILLATOR_SHAPE_SLOPE>("slope, pw sweep", true);
  return 0;
}
