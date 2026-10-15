// The fixed-point string models (src/string_voice.*, src/string_fixed.*)
// against upstream's float String and Plucker (rings/dsp/string.cc,
// plucker.h), both at 48 kHz in 32-sample blocks. The reference drives
// upstream's classes as Part::RenderStringVoice does for one voice with the
// internal exciter, with the same choices as the port (the tonic is the
// note; the mono signal is out - aux). Both draw noise from the same
// generator in the same order, so the waveforms are comparable. Both render
// silent blocks before the first strum, so upstream's interpolators settle
// (the port starts settled, see string_fixed.h); it isn't in the output.
//
//   make test-string   (exits 1 if a scenario fails its SNR threshold)
//   test_string N      prints scenario N's SNR over time (drift diagnosis)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rings/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

// plucker.h uses stmlib names unqualified (part.h brings them in upstream).
namespace rings { using namespace stmlib; }

#include "rings/dsp/plucker.h"
#include "rings/dsp/string.h"
#include "src/ffloat.h"
#include "src/string_voice.h"

namespace rings {
extern float chords[4][11][8];   // part.cc (kMaxPolyphony = 4)
}

namespace {

using op1rings::ResonatorModel;
using op1rings::MODEL_SYMPATHETIC_STRING;
using op1rings::MODEL_STRING;
using op1rings::MODEL_SYMPATHETIC_STRING_QUANTIZED;
using op1rings::MODEL_STRING_AND_REVERB;

const double kRate = 48000.0;
const int kBlock = 32;
// Silent blocks before the first strum (enough for upstream to settle at 15 Hz,
// where its interpolators only step on the string's own samples).
const int kWarmUpBlocks = 20;

struct Scenario {
  const char* name;
  ResonatorModel model;
  int polyphony;
  double frequency;     // Hz
  double structure;
  double brightness;
  double damping;
  double position;
  double seconds;
  double restrike;      // seconds between strums (0: one strum)
  double ratio;         // frequency ratio at each restrike
  double min_snr;       // dB, over the first 0.25 s
};

const Scenario kScenarios[] = {
  { "string, no dispersion",   MODEL_STRING, 1, 220.0, 0.25, 0.5, 0.5, 0.3, 1.0, 0, 1, 40 },
  { "string, stiff",           MODEL_STRING, 1, 110.0, 0.5,  0.7, 0.7, 0.5, 1.0, 0, 1, 40 },
  { "string, noisy",           MODEL_STRING, 1, 440.0, 0.9,  0.6, 0.6, 0.2, 1.0, 0, 1, 30 },
  { "string, curved bridge",   MODEL_STRING, 1, 220.0, 0.05, 0.8, 0.8, 0.7, 1.0, 0, 1, 30 },
  { "string, low",             MODEL_STRING, 1, 55.0,  0.25, 0.3, 0.4, 0.5, 1.0, 0, 1, 40 },
  { "string, high",            MODEL_STRING, 1, 1760.0, 0.25, 0.9, 0.5, 0.4, 1.0, 0, 1, 40 },
  { "string, 15 Hz (src)",     MODEL_STRING, 1, 15.0,  0.25, 0.5, 0.5, 0.5, 1.0, 0, 1, 40 },
  { "string, infinite",        MODEL_STRING, 1, 220.0, 0.25, 1.0, 1.0, 0.3, 1.0, 0, 1, 40 },
  { "string, short",           MODEL_STRING, 1, 220.0, 0.3,  0.6, 0.1, 0.6, 1.0, 0, 1, 40 },
  { "string, restrikes",       MODEL_STRING, 1, 220.0, 0.4,  0.6, 0.8, 0.4, 1.0, 0.13, 1.122, 35 },
  { "string, stress",          MODEL_STRING, 1, 110.0, 0.25, 1.0, 1.0, 0.0, 2.0, 0.05, 1, 30 },
  { "string+reverb damping",   MODEL_STRING_AND_REVERB, 1, 220.0, 0.6, 0.5, 0.6, 0.5, 1.0, 0, 1, 40 },
  { "sympathetic, poly 1",     MODEL_SYMPATHETIC_STRING, 1, 220.0, 0.4, 0.6, 0.6, 0.4, 1.0, 0, 1, 35 },
  { "sympathetic, poly 2",     MODEL_SYMPATHETIC_STRING, 2, 147.0, 0.7, 0.5, 0.8, 0.6, 1.0, 0, 1, 35 },
  { "sympathetic, poly 4",     MODEL_SYMPATHETIC_STRING, 4, 330.0, 0.2, 0.8, 0.5, 0.3, 1.0, 0, 1, 35 },
  { "sympathetic, restrikes",  MODEL_SYMPATHETIC_STRING, 1, 196.0, 0.55, 0.7, 0.7, 0.5, 1.0, 0.2, 1.335, 30 },
  { "quantized, poly 1",       MODEL_SYMPATHETIC_STRING_QUANTIZED, 1, 220.0, 0.5, 0.6, 0.7, 0.4, 1.0, 0, 1, 35 },
  { "quantized, poly 2",       MODEL_SYMPATHETIC_STRING_QUANTIZED, 2, 262.0, 0.85, 0.5, 0.6, 0.5, 1.0, 0, 1, 35 },
  { "quantized, poly 4",       MODEL_SYMPATHETIC_STRING_QUANTIZED, 4, 196.0, 0.15, 0.7, 0.6, 0.5, 1.0, 0, 1, 35 },
};

int32_t Q31(double x) { return op1rings::FFloat(x).ToQ31(); }

// --- Upstream reference ------------------------------------------------------

float Squash(float x) {
  if (x < 0.5f) {
    x *= 2.0f; x *= x; x *= x; x *= x; x *= x; x *= 0.5f;
  } else {
    x = 2.0f - 2.0f * x; x *= x; x *= x; x *= x; x *= x; x = 1.0f - 0.5f * x;
  }
  return x;
}

// Part::ComputeSympatheticStringsNotes, with polyphony as a parameter.
void ComputeSympatheticStringsNotes(float tonic, float note, float parameter, float* destination,
                                    size_t num_strings, int polyphony) {
  float notes[9] = { tonic, note - 12.0f, note - 7.01955f, note, note + 7.01955f,
                     note + 12.0f, note + 19.01955f, note + 24.0f, note + 24.0f };
  const float detunings[4] = { 0.013f, 0.011f, 0.007f, 0.017f };
  if (parameter >= 2.0f) {
    int32_t chord_index = parameter - 2.0f;
    const float* chord = rings::chords[polyphony - 1][chord_index];
    for (size_t i = 0; i < num_strings; ++i) destination[i] = chord[i] + note;
    return;
  }
  size_t num_detuned_strings = (num_strings - 1) >> 1;
  size_t first_detuned_string = num_strings - num_detuned_strings;
  for (size_t i = 0; i < first_detuned_string; ++i) {
    float note = 3.0f;
    if (i != 0) {
      note = parameter * 7.0f;
      parameter += (1.0f - parameter) * 0.2f;
    }
    MAKE_INTEGRAL_FRACTIONAL(note);
    note_fractional = Squash(note_fractional);
    float a = notes[note_integral];
    float b = notes[note_integral + 1];
    note = a + (b - a) * note_fractional;
    destination[i] = note;
    if (i + first_detuned_string < num_strings) {
      destination[i + first_detuned_string] = destination[i] + detunings[i & 3];
    }
  }
}

class RefVoice {
 public:
  void Init(ResonatorModel model, int polyphony) {
    model_ = model;
    polyphony_ = polyphony;
    const bool sympathetic = model == MODEL_SYMPATHETIC_STRING ||
                             model == MODEL_SYMPATHETIC_STRING_QUANTIZED;
    num_strings_ = sympathetic ? 2 * 4 / polyphony : 1;
    const float lfo_frequencies[8] = { 0.5f, 0.4f, 0.35f, 0.23f, 0.211f, 0.2f, 0.171f };
    for (int i = 0; i < 8; ++i) {
      strings_[i].Init(model == MODEL_STRING || model == MODEL_STRING_AND_REVERB);
      lfo_[i].Init<stmlib::COSINE_OSCILLATOR_APPROXIMATE>(
          float(kBlock) / float(kRate) * lfo_frequencies[i]);
    }
    plucker_.Init();
    dc_blocker_.Init(1.0f - 10.0f / float(kRate));
    chord_ = 0;
  }

  void Process(float f0, float structure, float brightness, float damping, float position,
               bool strum, float* out) {
    float input[kBlock] = { 0 }, noise[kBlock], o[kBlock] = { 0 }, a[kBlock] = { 0 };
    float sympathetic_input[kBlock];
    float cutoff = brightness * (2.0f - brightness);
    float filter_cutoff = std::min(f0 * stmlib::SemitonesToRatio((cutoff - 0.5f) * 96.0f), 0.499f);

    float frequencies[8];
    const bool sympathetic = num_strings_ > 1 || model_ == MODEL_SYMPATHETIC_STRING ||
                             model_ == MODEL_SYMPATHETIC_STRING_QUANTIZED;
    if (sympathetic) {
      float parameter = structure;
      if (model_ == MODEL_SYMPATHETIC_STRING_QUANTIZED) {
        float chord = structure * 10.0f;
        float hysteresis = chord - chord_ > 0.0f ? -0.1f : +0.1f;
        chord_ = static_cast<int32_t>(chord + hysteresis + 0.5f);
        CONSTRAIN(chord_, 0, 10);
        parameter = 2.0f + chord_;
      }
      float notes[8];
      ComputeSympatheticStringsNotes(0.0f, 0.0f, parameter, notes, num_strings_, polyphony_);
      for (int i = 0; i < num_strings_; ++i) frequencies[i] = f0 * stmlib::SemitonesToRatio(notes[i]);
    } else {
      frequencies[0] = f0;
    }

    if (strum) plucker_.Trigger(f0, filter_cutoff * 8.0f, position);
    plucker_.Process(noise, kBlock);
    for (int i = 0; i < kBlock; ++i) input[i] += noise[i];
    dc_blocker_.Process(input, kBlock);

    float dispersion = structure < 0.24f
        ? (structure - 0.24f) * 4.166f
        : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
    for (int string = 0; string < num_strings_; ++string) {
      rings::String& s = strings_[string];
      float lfo_value = lfo_[string].Next();
      float b = brightness, d = damping, p = position, glide = 1.0f;
      float string_index = static_cast<float>(string) / static_cast<float>(num_strings_);
      const float* in = input;
      if (model_ == MODEL_STRING_AND_REVERB) d *= (2.0f - d);
      if (string > 0) {
        b *= (2.0f - b);
        b *= (2.0f - b);
        d = 0.7f + damping * 0.27f;
        float amount = (0.5f - fabs(0.5f - position)) * 0.9f;
        p = position + lfo_value * amount;
        glide = stmlib::SemitonesToRatio((b - 1.0f) * 36.0f);
        in = sympathetic_input;
      }
      s.set_dispersion(dispersion);
      s.set_frequency(frequencies[string], glide);
      s.set_brightness(b);
      s.set_position(p);
      s.set_damping(d + string_index * (0.95f - d));
      s.Process(in, o, a, kBlock);
      if (string == 0) {
        float gain = 0.2f / static_cast<float>(num_strings_);
        for (int i = 0; i < kBlock; ++i) sympathetic_input[i] = gain * (o[i] - a[i]);
      }
    }
    for (int i = 0; i < kBlock; ++i) out[i] = o[i] - a[i];
  }

 private:
  ResonatorModel model_;
  int polyphony_;
  int num_strings_;
  int32_t chord_;
  rings::String strings_[8];
  stmlib::CosineOscillator lfo_[8];
  rings::Plucker plucker_;
  stmlib::DCBlocker dc_blocker_;
};

// The frequency at time t (seconds): ratio^(number of restrikes so far).
double FrequencyAt(const Scenario& s, int block, bool* strum) {
  const int restrike_blocks = s.restrike > 0 ? static_cast<int>(s.restrike * kRate / kBlock) : 0;
  *strum = block == 0 || (restrike_blocks && block % restrike_blocks == 0);
  const int n = restrike_blocks ? block / restrike_blocks : 0;
  return s.frequency * std::pow(s.ratio, n);
}

std::vector<double> RenderUpstream(const Scenario& s) {
  static RefVoice v;
  // Upstream String::Init leaves src_phase_ unset: start every scenario from
  // zeroed state, as the port does.
  memset(static_cast<void*>(&v), 0, sizeof(v));
  stmlib::Random::Seed(0x21);
  v.Init(s.model, s.polyphony);
  std::vector<double> y;
  float out[kBlock];
  const int blocks = static_cast<int>(s.seconds * kRate) / kBlock;
  for (int b = 0; b < kWarmUpBlocks; ++b) {
    v.Process(s.frequency / kRate, s.structure, s.brightness, s.damping, s.position, false, out);
  }
  for (int b = 0; b < blocks; ++b) {
    bool strum;
    const double f = FrequencyAt(s, b, &strum);
    v.Process(f / kRate, s.structure, s.brightness, s.damping, s.position, strum, out);
    for (int i = 0; i < kBlock; ++i) y.push_back(out[i]);
  }
  return y;
}

std::vector<double> RenderPort(const Scenario& s) {
  static op1rings::StringVoice v;
  op1rings::RandomFixed::Seed(0x21);
  v.Init();
  v.Configure(s.model, s.polyphony);
  std::vector<double> y;
  int32_t out[kBlock];
  op1rings::VoicePatch patch = { Q31(s.structure), Q31(s.brightness), Q31(s.damping), Q31(s.position) };
  const int blocks = static_cast<int>(s.seconds * kRate) / kBlock;
  for (int b = 0; b < kWarmUpBlocks; ++b) v.Process(Q31(s.frequency / kRate), patch, out, kBlock);
  for (int b = 0; b < blocks; ++b) {
    bool strum;
    const double f = FrequencyAt(s, b, &strum);
    if (strum) {
      const double cutoff = s.brightness * (2.0 - s.brightness);
      const double fc = std::min(f / kRate * std::pow(2.0, (cutoff - 0.5) * 8.0), 0.499);
      v.Strum(Q31(f / kRate), Q31(fc), patch.position);
    }
    v.Process(Q31(f / kRate), patch, out, kBlock);
    for (int i = 0; i < kBlock; ++i) y.push_back(out[i] / 134217728.0);
  }
  return y;
}

double Rms(const std::vector<double>& x, size_t from, size_t n) {
  double e = 0.0;
  for (size_t i = from; i < from + n; ++i) e += x[i] * x[i];
  return std::sqrt(e / n);
}

double Peak(const std::vector<double>& x) {
  double p = 0.0;
  for (double v : x) p = std::max(p, std::fabs(v));
  return p;
}

double SnrDb(const std::vector<double>& ref, const std::vector<double>& x, size_t from, size_t n) {
  double e = 0.0, d = 0.0;
  for (size_t i = from; i < from + n; ++i) {
    e += ref[i] * ref[i];
    d += (ref[i] - x[i]) * (ref[i] - x[i]);
  }
  return 10 * std::log10(e / (d + 1e-30));
}

// Magnitude spectrum difference (dB, mean absolute over the bands within
// 60 dB of the loudest) of 8192 samples from 2048 (after the strum's
// attack): ignores phase, so it shows timbre and pitch.
double SpectralDifferenceDb(const std::vector<double>& a, const std::vector<double>& b) {
  const int n = 8192;
  const size_t from = 2048;
  const int kMaxBands = 48;
  double energy_a[kMaxBands], energy_b[kMaxBands];
  int num_bands = 0;
  for (int band = 0; band < kMaxBands; ++band) {
    // Quarter-octave bands from 40 Hz.
    const double lo = 40.0 * std::pow(2.0, band / 4.0), hi = lo * std::pow(2.0, 0.25);
    if (hi > 16000.0) break;
    double& ea = energy_a[band];
    double& eb = energy_b[band];
    ea = eb = 0.0;
    num_bands = band + 1;
    for (int k = static_cast<int>(lo * n / kRate); k < static_cast<int>(hi * n / kRate) + 1; ++k) {
      double ra = 0, ia = 0, rb = 0, ib = 0;
      for (int i = 0; i < n; i += 2) {   // decimated DFT: enough for a check
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
        const double c = std::cos(2 * M_PI * k * i / n), s = std::sin(2 * M_PI * k * i / n);
        ra += w * a[from + i] * c; ia -= w * a[from + i] * s;
        rb += w * b[from + i] * c; ib -= w * b[from + i] * s;
      }
      ea += ra * ra + ia * ia;
      eb += rb * rb + ib * ib;
    }
  }
  double loudest = 0.0;
  for (int band = 0; band < num_bands; ++band) loudest = std::max(loudest, energy_a[band]);
  double total = 0.0;
  int count = 0;
  for (int band = 0; band < num_bands; ++band) {
    if (energy_a[band] < loudest * 1e-6) continue;
    total += std::fabs(10 * std::log10((energy_b[band] + 1e-30) / energy_a[band]));
    ++count;
  }
  return count ? total / count : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  const int num = sizeof(kScenarios) / sizeof(kScenarios[0]);
  if (argc > 2 && std::string(argv[1]) == "tune") {   // diagnostic: string tunings
    const Scenario& s = kScenarios[atoi(argv[2])];
    std::vector<double> p = RenderPort(s);
    static op1rings::StringVoice v;
    v.Init();
    v.Configure(s.model, s.polyphony);
    int32_t out[kBlock];
    op1rings::VoicePatch patch = { Q31(s.structure), Q31(s.brightness), Q31(s.damping), Q31(s.position) };
    v.Process(Q31(s.frequency / kRate), patch, out, kBlock);
    float notes[8];
    float parameter = s.structure;
    if (s.model == MODEL_SYMPATHETIC_STRING_QUANTIZED) {
      float chord = s.structure * 10.0f;
      int32_t c = static_cast<int32_t>(chord + (chord > 0.0f ? -0.1f : 0.1f) + 0.5f);
      parameter = 2.0f + c;
    }
    ComputeSympatheticStringsNotes(0.0f, 0.0f, parameter, notes, v.num_strings(), s.polyphony);
    for (int k = 0; k < v.num_strings(); ++k) {
      printf("string %d  notes %+.6f  ratio up %.9f  port %.9f\n", k, notes[k],
             stmlib::SemitonesToRatio(notes[k]), v.ratio(k).ToDouble());
    }
    return 0;
  }
  if (argc > 1) {   // diagnostic: SNR over time for one scenario
    const Scenario& s = kScenarios[atoi(argv[1])];
    std::vector<double> u = RenderUpstream(s), p = RenderPort(s);
    for (size_t from = 0; from + 1024 <= u.size(); from += 1024) {
      printf("%s  t %.2f s  SNR %6.1f dB  level up %7.1f dB  port %7.1f dB\n", s.name, from / kRate,
             SnrDb(u, p, from, 1024), 20 * std::log10(Rms(u, from, 1024) + 1e-12),
             20 * std::log10(Rms(p, from, 1024) + 1e-12));
    }
    return 0;
  }
  printf("%-26s %9s %9s %9s %9s %8s\n", "scenario", "SNR 0.25s", "SNR all", "level dB", "spec dB", "peak");
  bool pass = true;
  for (int i = 0; i < num; ++i) {
    const Scenario& s = kScenarios[i];
    std::vector<double> u = RenderUpstream(s), p = RenderPort(s);
    const size_t quarter = static_cast<size_t>(0.25 * kRate);
    const double snr = SnrDb(u, p, 0, quarter);
    const double snr_all = SnrDb(u, p, 0, u.size());
    const double level = 20 * std::log10(Rms(p, 0, p.size()) / Rms(u, 0, u.size()));
    const double spec = SpectralDifferenceDb(u, p);
    const bool ok = snr > s.min_snr;
    if (!ok) pass = false;
    printf("%-26s %9.1f %9.1f %9.2f %9.2f %8.3f  %s\n", s.name, snr, snr_all, level, spec, Peak(u),
           ok ? "ok" : "FAIL");
  }
  return pass ? 0 : 1;
}
